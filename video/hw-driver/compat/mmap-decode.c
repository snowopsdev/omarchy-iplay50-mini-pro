/* Controlled stateful decoder proof; consumer MMAP OUTPUT and CAPTURE.
 * Run only with the opt-in MT6789 DMABUF bridge on this tablet.
 * Input: little-endian uint32 length + Annex-B H.264 access unit, repeated.
 * Never loads drivers/daemons. The orchestrator must verify daemon readiness.
 * cc -O2 -Wall -Wextra -Werror v4l2-decode.c -o v4l2-decode
 * v4l2-decode /dev/videoN sample.au [new-dump-prefix]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define MAX_BUFFERS 32
#define MAX_UNITS 2048
struct mapping { void *ptr; size_t length; int fd, owns_fd; };
struct queue {
    enum v4l2_buf_type type;
    enum v4l2_memory memory;
    unsigned count, planes;
    unsigned plane_sizes[VIDEO_MAX_PLANES];
    int streaming;
    struct mapping maps[MAX_BUFFERS][VIDEO_MAX_PLANES];
};
struct unit { unsigned char *data; uint32_t size; };
static int mtk_no_general_buffer;
static uint32_t requested_capture_format;
static int native_capture_format;
static unsigned capture_hold_count;
static uint64_t reported_first_plane_hash;
static const char *capture_heap_path = "/dev/dma_heap/mtk_mm";

static int xio(int fd, unsigned long request, void *arg, const char *name) {
    printf("ENTER %s\n", name);
    int ret;
    do { ret = ioctl(fd, request, arg); } while (ret < 0 && errno == EINTR);
    int saved = ret < 0 ? errno : 0;
    printf("RETURN %s ret=%d errno=%d\n", name, ret, saved);
    errno = saved;
    return ret;
}
#define IO(fd, cmd, arg) xio(fd, cmd, arg, #cmd)

static int setup(int fd, struct queue *q, unsigned count, unsigned planes) {
    struct v4l2_requestbuffers req = {
        .type = q->type, .memory = q->memory, .count = count
    };
    if (IO(fd, VIDIOC_REQBUFS, &req) < 0 || req.count == 0 ||
        req.count > MAX_BUFFERS || planes == 0 || planes > VIDEO_MAX_PLANES)
        return -1;
    q->count = req.count;
    q->planes = planes;
    int heap_fd = -1;
    if (q->memory == V4L2_MEMORY_DMABUF) {
        printf("ENTER open DMA heap %s\n", capture_heap_path);
        heap_fd = open(capture_heap_path, O_RDWR | O_CLOEXEC);
        printf("RETURN open DMA heap fd=%d errno=%d\n", heap_fd, heap_fd < 0 ? errno : 0);
        if (heap_fd < 0) return -1;
    }
    for (unsigned i = 0; i < q->count; i++) {
        struct v4l2_plane p[VIDEO_MAX_PLANES] = {{0}};
        struct v4l2_buffer b = {.type = q->type, .memory = q->memory,
            .index = i, .length = planes, .m.planes = p};
        if (q->memory == V4L2_MEMORY_MMAP &&
            (IO(fd, VIDIOC_QUERYBUF, &b) < 0 || b.length != planes)) return -1;
        for (unsigned j = 0; j < planes; j++) {
            int map_fd = fd;
            off_t map_offset = p[j].m.mem_offset;
            size_t length = p[j].length;
            if (q->memory == V4L2_MEMORY_DMABUF) {
                long page_size = sysconf(_SC_PAGESIZE);
                if (page_size <= 0 || !q->plane_sizes[j] ||
                    q->plane_sizes[j] > 64 * 1024 * 1024) { close(heap_fd); return -1; }
                length = ((size_t)q->plane_sizes[j] + page_size - 1) / page_size * page_size;
                struct dma_heap_allocation_data allocation = {
                    .len = length, .fd_flags = O_RDWR | O_CLOEXEC, .heap_flags = 0};
                if (IO(heap_fd, DMA_HEAP_IOCTL_ALLOC, &allocation) < 0) {
                    close(heap_fd); return -1;
                }
                map_fd = allocation.fd;
                map_offset = 0;
                q->maps[i][j].fd = map_fd;
                q->maps[i][j].owns_fd = 1;
                printf("DMABUF buffer=%u plane=%u fd=%d requested_size=%u allocation=%zu\n",
                       i, j, map_fd, q->plane_sizes[j], length);
            }
            if (!length || length > 64 * 1024 * 1024) {
                if (heap_fd >= 0) close(heap_fd);
                return -1;
            }
            printf("ENTER mmap queue=%u memory=%u buffer=%u plane=%u fd=%d length=%zu offset=%ld\n",
                   q->type, q->memory, i, j, map_fd, length, (long)map_offset);
            void *ptr = mmap(NULL, length, PROT_READ | PROT_WRITE,
                            MAP_SHARED, map_fd, map_offset);
            printf("RETURN mmap queue=%u buffer=%u plane=%u ptr=%p\n", q->type, i, j, ptr);
            if (ptr == MAP_FAILED) { if (heap_fd >= 0) close(heap_fd); return -1; }
            q->maps[i][j].ptr = ptr;
            q->maps[i][j].length = length;
        }
    }
    if (heap_fd >= 0) close(heap_fd);
    return 0;
}

static int enqueue(int fd, struct queue *q, unsigned index,
                   const struct unit *unit, unsigned timestamp) {
    if (index >= q->count) return -1;
    struct v4l2_plane p[VIDEO_MAX_PLANES] = {{0}};
    for (unsigned j = 0; j < q->planes; j++) {
        p[j].length = q->maps[index][j].length;
        if (q->memory == V4L2_MEMORY_DMABUF) {
            if (!q->maps[index][j].owns_fd) return -1;
            p[j].m.fd = q->maps[index][j].fd;
        }
    }
    if (unit) {
        if (unit->size > p[0].length) return -1;
        memcpy(q->maps[index][0].ptr, unit->data, unit->size);
        p[0].bytesused = unit->size;
    }
    struct v4l2_buffer b = {.type = q->type, .memory = q->memory,
        .index = index, .length = q->planes, .m.planes = p,
        .timestamp = {.tv_sec = timestamp / 10,
                      .tv_usec = (timestamp % 10) * 100000},
        .flags = V4L2_BUF_FLAG_TIMESTAMP_COPY};
    /* Exact vendor ABI: reserved/request_fd is an optional general DMABUF,
     * with -1 meaning none. Standard clients set zero; this driver treats fd0
     * as a real supplemental DMA buffer. Only enable after exact driver check.
     */
    if (mtk_no_general_buffer && q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
        b.request_fd = -1;
    printf("QUEUE type=%u memory=%u index=%u bytes=%u timestamp=%u plane0_fd=%d\n",
           q->type, q->memory, index, p[0].bytesused, timestamp,
           q->memory == V4L2_MEMORY_DMABUF ? p[0].m.fd : -1);
    return IO(fd, VIDIOC_QBUF, &b);
}

static int streamon(int fd, struct queue *q) {
    if (IO(fd, VIDIOC_STREAMON, &q->type) < 0) return -1;
    q->streaming = 1;
    return 0;
}

static void teardown(int fd, struct queue *q) {
    if (q->streaming) IO(fd, VIDIOC_STREAMOFF, &q->type);
    for (unsigned i = 0; i < q->count; i++)
        for (unsigned j = 0; j < q->planes; j++) {
            if (q->maps[i][j].ptr)
                munmap(q->maps[i][j].ptr, q->maps[i][j].length);
            if (q->maps[i][j].owns_fd) close(q->maps[i][j].fd);
        }
    if (q->count) {
        struct v4l2_requestbuffers req = {
            .type = q->type, .memory = q->memory, .count = 0};
        IO(fd, VIDIOC_REQBUFS, &req);
    }
}

static int load(const char *path, struct unit units[MAX_UNITS]) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    unsigned n = 0;
    for (;;) {
        unsigned char len[4];
        size_t got = fread(len, 1, 4, fp);
        if (!got && feof(fp)) break;
        if (got != 4 || n == MAX_UNITS) { fclose(fp); return -1; }
        uint32_t size = (uint32_t)len[0] | ((uint32_t)len[1] << 8) |
                        ((uint32_t)len[2] << 16) | ((uint32_t)len[3] << 24);
        if (!size || size > 2 * 1024 * 1024) { fclose(fp); return -1; }
        unsigned char *data = malloc(size);
        if (!data || fread(data, 1, size, fp) != size) {
            free(data); fclose(fp); return -1;
        }
        units[n++] = (struct unit){data, size};
    }
    fclose(fp);
    return (int)n;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int capture_setup(int fd, struct queue *q) {
    struct v4l2_format fmt = {.type = q->type};
    if (IO(fd, VIDIOC_G_FMT, &fmt) < 0) return -1;
    struct v4l2_pix_format_mplane *f = &fmt.fmt.pix_mp;
    /* Native mode preserves the actual firmware format, with no S_FMT call.
     * It proves transport/decode only; no pixel-layout compatibility is implied.
     */
    if (native_capture_format) {
        printf("REQUEST_CAPTURE native: preserving post-header G_FMT; skipping S_FMT\n");
    } else {
        int has_nv12 = 0, has_mm21 = 0;
        for (unsigned i = 0; i < 128; i++) {
            struct v4l2_fmtdesc desc = {.type = q->type, .index = i};
            if (IO(fd, VIDIOC_ENUM_FMT, &desc) < 0) break;
            has_nv12 |= desc.pixelformat == V4L2_PIX_FMT_NV12;
            has_mm21 |= desc.pixelformat == V4L2_PIX_FMT_MM21;
        }
        uint32_t selected = requested_capture_format ? requested_capture_format :
                            has_nv12 ? V4L2_PIX_FMT_NV12 :
                            has_mm21 ? V4L2_PIX_FMT_MM21 : 0;
        if (!selected || (selected == V4L2_PIX_FMT_NV12 && !has_nv12) ||
            (selected == V4L2_PIX_FMT_MM21 && !has_mm21)) {
            fprintf(stderr, "no advertised supported capture format\n");
            return -1;
        }
        struct v4l2_format desired = {.type = q->type};
        desired.fmt.pix_mp = (struct v4l2_pix_format_mplane){
            .width = f->width, .height = f->height,
            .pixelformat = selected, .field = V4L2_FIELD_NONE};
        printf("REQUEST_CAPTURE fourcc=%c%c%c%c\n", selected & 255,
               (selected >> 8) & 255, (selected >> 16) & 255, selected >> 24);
        if (IO(fd, VIDIOC_S_FMT, &desired) < 0) return -1;
        uint32_t returned = desired.fmt.pix_mp.pixelformat;
        printf("S_FMT_CAPTURE fourcc=%c%c%c%c hex=%08x size=%ux%u planes=%u\n",
               returned & 255, (returned >> 8) & 255, (returned >> 16) & 255,
               returned >> 24, returned, desired.fmt.pix_mp.width,
               desired.fmt.pix_mp.height, desired.fmt.pix_mp.num_planes);
        fmt = (struct v4l2_format){.type = q->type};
        if (IO(fd, VIDIOC_G_FMT, &fmt) < 0) return -1;
        returned = f->pixelformat;
        printf("G_FMT_CAPTURE fourcc=%c%c%c%c hex=%08x size=%ux%u planes=%u\n",
               returned & 255, (returned >> 8) & 255, (returned >> 16) & 255,
               returned >> 24, returned, f->width, f->height, f->num_planes);
        for (unsigned j = 0; j < f->num_planes && j < VIDEO_MAX_PLANES; j++)
            printf("G_FMT_PLANE %u sizeimage=%u stride=%u\n", j,
                   f->plane_fmt[j].sizeimage, f->plane_fmt[j].bytesperline);
        if (desired.fmt.pix_mp.pixelformat != selected || returned != selected) {
            fprintf(stderr, "capture format coercion: requested=%08x S_FMT=%08x G_FMT=%08x; refusing allocation\n",
                    selected, desired.fmt.pix_mp.pixelformat, returned);
            return -1;
        }
    }
    printf("CAPTURE_FMT fourcc=%c%c%c%c hex=%08x size=%ux%u planes=%u\n",
           f->pixelformat & 255, (f->pixelformat >> 8) & 255,
           (f->pixelformat >> 16) & 255, f->pixelformat >> 24,
           f->pixelformat, f->width, f->height, f->num_planes);
    for (unsigned j = 0; j < f->num_planes && j < VIDEO_MAX_PLANES; j++)
        printf("PLANE %u sizeimage=%u stride=%u\n", j,
               f->plane_fmt[j].sizeimage, f->plane_fmt[j].bytesperline);
    if (f->num_planes == 0 || f->num_planes > VIDEO_MAX_PLANES) return -1;
    for (unsigned j = 0; j < f->num_planes; j++)
        q->plane_sizes[j] = f->plane_fmt[j].sizeimage;
    struct v4l2_selection selection = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                       .target = V4L2_SEL_TGT_COMPOSE};
    if (IO(fd, VIDIOC_G_SELECTION, &selection) == 0)
        printf("VISIBLE %d,%d %ux%u\n", selection.r.left, selection.r.top,
               selection.r.width, selection.r.height);
    struct v4l2_control ctrl = {.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE};
    unsigned count = 12;
    if (IO(fd, VIDIOC_G_CTRL, &ctrl) == 0 && ctrl.value >= 0 && ctrl.value < 28)
        count = ctrl.value + 4;
    if (count > MAX_BUFFERS || setup(fd, q, count, f->num_planes) < 0) return -1;
    if (capture_hold_count == MAX_BUFFERS) capture_hold_count=q->count-1;
    if (capture_hold_count >= q->count) {
        fprintf(stderr,"hold count must leave at least one queued view\n"); return -1;
    }
    printf("HOLD_CAPTURE count=%u available_views=%u total=%u\n",
           capture_hold_count,q->count-capture_hold_count,q->count);
    for (unsigned i = 0; i < q->count; i++)
        if (enqueue(fd, q, i, NULL, 0) < 0) return -1;
    return streamon(fd, q);
}

/* Exercise Chromium's additional MMAP-frame path: EXPBUF, duplicate last
 * color-plane FD, map exported DMA memory, synchronize and compare bytes. */
static int verify_export(int fd, struct queue *q, const struct v4l2_buffer *b,
                         const struct v4l2_plane *plane, unsigned index) {
    struct v4l2_exportbuffer exported = {.type=q->type,.index=b->index,
        .plane=index,.flags=O_CLOEXEC};
    if (IO(fd,VIDIOC_EXPBUF,&exported) < 0) return -1;
    int duplicated = dup(exported.fd);
    if (duplicated < 0) { close(exported.fd); return -1; }
    size_t length = q->maps[b->index][index].length;
    void *mapped = mmap(NULL,length,PROT_READ,MAP_SHARED,duplicated,0);
    if (mapped == MAP_FAILED) { close(duplicated); close(exported.fd); return -1; }
    struct dma_buf_sync sync = {.flags=DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ};
    int result = IO(duplicated,DMA_BUF_IOCTL_SYNC,&sync);
    if (!result) {
        if (plane->bytesused > length || memcmp(mapped,q->maps[b->index][index].ptr,
                                               plane->bytesused) != 0) result=-1;
        sync.flags=DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
        if (IO(duplicated,DMA_BUF_IOCTL_SYNC,&sync) < 0) result=-1;
    }
    munmap(mapped,length); close(duplicated); close(exported.fd);
    printf("EXPORT_VERIFY frame=0 plane=%u bytes=%u result=%d\n",index,plane->bytesused,result);
    return result;
}

static int report_frame(int fd, struct queue *q, const struct v4l2_buffer *b,
                        const struct v4l2_plane p[VIDEO_MAX_PLANES],
                        unsigned frame, const char *prefix) {
    if (b->index >= q->count || b->length > q->planes) return -1;
    printf("FRAME n=%u index=%u sequence=%u flags=%08x timestamp=%ld.%06ld\n",
           frame, b->index, b->sequence, b->flags,
           (long)b->timestamp.tv_sec, (long)b->timestamp.tv_usec);
    if (b->flags & V4L2_BUF_FLAG_ERROR) return -1;
    for (unsigned j = 0; j < b->length; j++) {
        if (p[j].bytesused > q->maps[b->index][j].length ||
            p[j].data_offset > p[j].bytesused) return -1;
        if (frame == 0 && verify_export(fd,q,b,&p[j],j) < 0) return -1;
        const unsigned char *data = q->maps[b->index][j].ptr;

        uint64_t sum = 0, hash = UINT64_C(14695981039346656037);
        unsigned length = p[j].bytesused - p[j].data_offset;
        for (unsigned k = p[j].data_offset; k < p[j].bytesused; k++) {
            sum += data[k];
            hash = (hash ^ data[k]) * UINT64_C(1099511628211);
        }
        if (j == 0) reported_first_plane_hash=hash;
        printf("FRAME_PLANE frame=%u plane=%u bytes=%u offset=%u mean=%.4f fnv1a=%016llx\n",
               frame, j, length, p[j].data_offset,
               length ? (double)sum / length : 0, (unsigned long long)hash);
        if (prefix && frame == 0) {
            char path[4096];
            int chars = snprintf(path, sizeof(path), "%s.plane%u.bin", prefix, j);
            if (chars < 0 || chars >= (int)sizeof(path)) return -1;
            int out = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (out < 0) { perror(path); return -1; }
            size_t written = 0;
            while (written < length) {
                ssize_t n = write(out, data + p[j].data_offset + written, length - written);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) { close(out); return -1; }
                written += (size_t)n;
            }
            if (close(out) < 0) return -1;
        }

    }
    return 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *bridge = getenv("TABLET_V4L2_BRIDGE");
    int (*bridge_abi)(void) = dlsym(RTLD_DEFAULT, "tablet_v4l2_bridge_abi");
    if (!bridge || strcmp(bridge, "1") != 0 || !bridge_abi || bridge_abi() != 1) {
        fprintf(stderr, "requires TABLET_V4L2_BRIDGE=1 and the staged bridge preload\n");
        return 2;
    }
    while (argc > 1) {
        if (strcmp(argv[1], "--mtk") == 0) {
            mtk_no_general_buffer = 1;
            printf("COMPAT MT6789 capture general-buffer sentinel=-1\n");
        } else if (strcmp(argv[1], "--capture=native") == 0) {
            native_capture_format = 1;
        } else if (strcmp(argv[1], "--capture=NV12") == 0) {
            native_capture_format = 0;
            requested_capture_format = V4L2_PIX_FMT_NV12;
        } else if (strcmp(argv[1], "--capture=MM21") == 0) {
            native_capture_format = 0;
            requested_capture_format = V4L2_PIX_FMT_MM21;
        } else if (strcmp(argv[1],"--hold-capture=all-but-one") == 0) {
            capture_hold_count=MAX_BUFFERS;
        } else if (strncmp(argv[1],"--hold-capture=",15) == 0 && argv[1][15]) {
            char *end; errno=0;
            unsigned long hold=strtoul(argv[1]+15,&end,10);
            if (errno || *end || hold >= MAX_BUFFERS) return 2;
            capture_hold_count=(unsigned)hold;
        } else if (strncmp(argv[1], "--heap=", 7) == 0 && argv[1][7]) {
            capture_heap_path = argv[1] + 7;
        } else {
            break;
        }
        argc--; argv++;
    }
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: v4l2-decode [--mtk] [--capture=native|--capture=NV12|--capture=MM21] [--hold-capture=N|--hold-capture=all-but-one] [--heap=/dev/dma_heap/mtk_mm] /dev/videoN length-prefixed-h264.au [new-dump-prefix]\n");
        return 2;
    }
    struct unit units[MAX_UNITS] = {{0}};
    int total = load(argv[2], units);
    if (total < 1) { fprintf(stderr, "invalid framed input\n"); return 2; }
    printf("INPUT access_units=%d\nENTER open %s\n", total, argv[1]);
    int fd = open(argv[1], O_RDWR | O_NONBLOCK | O_CLOEXEC);
    printf("RETURN open fd=%d errno=%d\n", fd, fd < 0 ? errno : 0);
    if (fd < 0) return 1;
    int result = 1;
    struct queue output = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           .memory = V4L2_MEMORY_MMAP};
    struct queue capture = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                            .memory = V4L2_MEMORY_MMAP};
    struct v4l2_capability caps = {0};
    if (IO(fd, VIDIOC_QUERYCAP, &caps) < 0) goto done;
    if (mtk_no_general_buffer &&
        (strncmp((const char *)caps.driver, "mtk-vcodec-dec", sizeof(caps.driver)) != 0 ||
         strncmp((const char *)caps.card, "platform:mt6789", sizeof(caps.card)) != 0)) {
        fprintf(stderr, "MTK compatibility requested for a different driver\n");
        goto done;
    }
    struct v4l2_format fmt = {.type = output.type};
    fmt.fmt.pix_mp = (struct v4l2_pix_format_mplane){.width = 320, .height = 240,
        .pixelformat = V4L2_PIX_FMT_H264, .field = V4L2_FIELD_NONE, .num_planes = 1};
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = 2 * 1024 * 1024;
    if (IO(fd, VIDIOC_S_FMT, &fmt) < 0 ||
        fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_H264 ||
        fmt.fmt.pix_mp.num_planes != 1 ||
        !fmt.fmt.pix_mp.plane_fmt[0].sizeimage) goto done;
    output.plane_sizes[0] = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    printf("OUTPUT_FMT fourcc=H264 planes=1 sizeimage=%u memory=MMAP\n",
           output.plane_sizes[0]);
    struct v4l2_event_subscription sub = {.type = V4L2_EVENT_SOURCE_CHANGE};
    if (IO(fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0 ||
        setup(fd, &output, 4, 1) < 0 || streamon(fd, &output) < 0) goto done;
    unsigned queued = 0, consumed = 0, frames = 0;
    unsigned held[MAX_BUFFERS]={0},held_count=0,held_bytes[MAX_BUFFERS]={0};
    uint64_t held_hash[MAX_BUFFERS]={0};
    int stop_sent = 0, last = 0;
    for (; queued < output.count && queued < (unsigned)total; queued++)
        if (enqueue(fd, &output, queued, &units[queued], queued) < 0) goto done;
    double deadline = now() + 90;
    while (!last && now() < deadline) {
        struct pollfd pollfd = {.fd = fd, .events = POLLIN | POLLPRI |
                                (consumed < queued ? POLLOUT : 0)};
        int ret = poll(&pollfd, 1, 250);
        if (ret < 0 && errno == EINTR) continue;
        if (ret < 0 || (pollfd.revents & (POLLHUP | POLLNVAL))) goto done;
        if (!ret) continue;
        if (pollfd.revents & POLLPRI) {
            struct v4l2_event event = {0};
            if (IO(fd, VIDIOC_DQEVENT, &event) < 0) goto done;
            printf("EVENT type=%u changes=%u\n", event.type, event.u.src_change.changes);
            if (event.type == V4L2_EVENT_SOURCE_CHANGE) {
                if (capture.streaming || capture_setup(fd, &capture) < 0) goto done;
            }
        }
        /* DQBUF is nonblocking; poll bits can include error before CAPTURE exists. */
        if (pollfd.revents & POLLOUT) {
            struct v4l2_plane p[VIDEO_MAX_PLANES] = {{0}};
            struct v4l2_buffer b = {.type = output.type, .memory = output.memory,
                .length = output.planes, .m.planes = p};
            if (IO(fd, VIDIOC_DQBUF, &b) == 0) {
                consumed++;
                if (b.flags & V4L2_BUF_FLAG_ERROR) goto done;
                if (queued < (unsigned)total) {
                    if (enqueue(fd, &output, b.index, &units[queued], queued) < 0) goto done;
                    queued++;
                }
            } else if (errno != EAGAIN) goto done;
        }
        if (capture.streaming && (pollfd.revents & POLLIN)) {
            struct v4l2_plane p[VIDEO_MAX_PLANES] = {{0}};
            struct v4l2_buffer b = {.type = capture.type, .memory = capture.memory,
                .length = capture.planes, .m.planes = p};
            if (IO(fd, VIDIOC_DQBUF, &b) == 0) {
                if (p[0].bytesused) {
                    if (report_frame(fd, &capture, &b, p, frames, argc == 4 ? argv[3] : NULL) < 0)
                        goto done;
                    frames++;
                }
                last = !!(b.flags & V4L2_BUF_FLAG_LAST);
                if (!last) {
                    if (held_count >= MAX_BUFFERS || b.index >= MAX_BUFFERS) goto done;
                    held[held_count++]=b.index;
                    held_bytes[b.index]=p[0].bytesused;
                    held_hash[b.index]=reported_first_plane_hash;
                    if (held_count > capture_hold_count) {
                        unsigned release=held[0];
                        memmove(held,held+1,(--held_count)*sizeof(held[0]));
                        const unsigned char *data=capture.maps[release][0].ptr;
                        uint64_t hash=UINT64_C(14695981039346656037);
                        for (unsigned k=0;k<held_bytes[release];k++)
                            hash=(hash^data[k])*UINT64_C(1099511628211);
                        if (held_bytes[release] && hash != held_hash[release]) {
                            fprintf(stderr,"held view changed before client QBUF: index=%u\n",release);
                            goto done;
                        }
                        if (enqueue(fd,&capture,release,NULL,0) < 0) goto done;
                    }
                }
            } else if (errno != EAGAIN) goto done;
        }
        if (!stop_sent && consumed == (unsigned)total) {
            struct v4l2_decoder_cmd cmd = {.cmd = V4L2_DEC_CMD_STOP};
            if (IO(fd, VIDIOC_DECODER_CMD, &cmd) < 0) goto done;
            stop_sent = 1;
        }
    }
    printf("DECODE_RESULT units=%d consumed=%u frames=%u last=%d stop_sent=%d\n",
           total, consumed, frames, last, stop_sent);
    result = !(last && frames == (unsigned)total && consumed == (unsigned)total);
done:
    teardown(fd, &output);
    teardown(fd, &capture);
    printf("ENTER close\n");
    int closed = close(fd);
    printf("RETURN close ret=%d errno=%d\n", closed, closed < 0 ? errno : 0);
    if (closed < 0) result = 1;
    for (unsigned i = 0; i < MAX_UNITS; i++) free(units[i].data);
    return result;
}
