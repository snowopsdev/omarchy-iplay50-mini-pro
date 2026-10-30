/* Opt-in, exact-driver MMAP facade over real DMABUF queues.
 * Kernel CAPTURE remains native M21S; clients get a separately converted NV12
 * DMA buffer. No driver format is relabelled without converting its pixels.
 * Staged experimental code: validate with a standalone MMAP client first.
 */
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <poll.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>
#include "../../hw-format/m21s-convert.h"
#include "../../hw-format/m21s-argb.h"
#include "mtk-color-desc.h"

#define M21S v4l2_fourcc('M','2','1','S')
#define MAX_CONTEXTS 16
#define MAX_SLOTS 32
#define MAX_BYTES (64U * 1024U * 1024U)
struct allocation { int fd; void *ptr; size_t len; };
struct slot { struct allocation native, view; int queued, native_queued; };
struct queue {
    unsigned count, type;
    int streaming;
    unsigned next_view;
    struct v4l2_format native_fmt;
    struct slot slots[MAX_SLOTS];
};
struct context { int active, fd, revoked, capture_drained; struct queue out, cap; };
static struct context contexts[MAX_CONTEXTS];
static int context_wakes[MAX_CONTEXTS];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int (*next_ioctl)(int, unsigned long, ...);
static int (*next_close)(int);
static int (*next_poll)(struct pollfd *, nfds_t, int);
static int (*next_poll_chk)(struct pollfd *, nfds_t, int, size_t);
static _Atomic unsigned trace_chk_entries;
static unsigned trace_poll_gates;
extern void __chk_fail(void) __attribute__((noreturn));
static void *(*next_mmap)(void *, size_t, int, int, int, off_t);
static void *(*next_mmap64)(void *, size_t, int, int, int, off64_t);
static int enabled, heap_fd = -1, gpu_process_policy;
static unsigned capture_headroom;
static int decouple_capture;
static int poll_backpressure;
static int trace_frames;
static int request_argb; /* Any explicitly requested packed RGB view. */
static uint32_t rgb_view_fourcc=V4L2_PIX_FMT_ABGR32;
/* Separate policy storage keeps the runtime diagnostic context ABI stable. */
struct view_policy {
    int settled, rgb, input_queued, header_seen, resume_pending;
    struct tablet_mtk_color_desc color;
    void *scratch;
    size_t scratch_bytes;
};
static struct view_policy view_policies[MAX_CONTEXTS];
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"GL finish query must remain lock-free");
static _Atomic unsigned finish_context_mask;
/* GL callers never enter the bridge mutex or inspect mutable queue state. */
int tablet_v4l2_capture_requires_finish(void) {
    return atomic_load_explicit(&finish_context_mask,memory_order_acquire)!=0;
}
/* Called only while the existing bridge mutex is held, after mutations. */
static void refresh_finish_requirement(struct context *c) {
    unsigned index=(unsigned)(c-contexts);
    unsigned bit=1U<<index;
    int required=c->active && !c->revoked && c->cap.count &&
                 !view_policies[index].rgb;
    if (required)
        atomic_fetch_or_explicit(&finish_context_mask,bit,memory_order_release);
    else
        atomic_fetch_and_explicit(&finish_context_mask,~bit,memory_order_release);
}
static struct view_policy *view_policy(struct context *c) {
    return &view_policies[c-contexts];
}
static void reset_view_policy(struct context *c) {
    struct view_policy *v=view_policy(c);
    if (v->scratch) munmap(v->scratch,v->scratch_bytes);
    int input_queued=v->input_queued,header_seen=v->header_seen;
    memset(v,0,sizeof(*v));
    v->input_queued=input_queued; v->header_seen=header_seen;
}

static void log_line(const char *fmt, ...) {
    char line[512]; va_list ap;
    va_start(ap, fmt); int n = vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    if (n > 0) (void)write(STDERR_FILENO, line,
                           (size_t)n < sizeof(line) ? (size_t)n : sizeof(line)-1);
}
static int fail(int error) { errno = error; return -1; }
static void fork_prepare(void) { if (enabled) pthread_mutex_lock(&mutex); }
static void fork_parent(void) { if (enabled) pthread_mutex_unlock(&mutex); }
static void fork_child(void) {
    if (!enabled) return;
    /* Sharing an already-active codec open-file description across processes
     * is unsupported. Fail closed in the child until it closes/reopens it. */
    for (unsigned i=0;i<MAX_CONTEXTS;i++) {
        if (contexts[i].active) contexts[i].revoked=1;
        if (context_wakes[i] >= 0) { next_close(context_wakes[i]); context_wakes[i]=-1; }
    }
    atomic_store_explicit(&finish_context_mask,0,memory_order_release);
    if (gpu_process_policy && heap_fd >= 0) {
        next_close(heap_fd);
        heap_fd=-1;
    }
    pthread_mutex_unlock(&mutex);
}
/* Exact argv-token policy, not a substring search over process titles.
 * Duplicate/ambiguous type switches and malformed/truncated cmdlines fail closed.
 */
static int gpu_role_from_cmdline(const char *data, size_t size) {
    if (!data || !size || data[size-1] != '\0') return 0;
    unsigned type_count=0; int gpu=0;
    for (size_t pos=0, index=0; pos<size; index++) {
        size_t length=strnlen(data+pos,size-pos);
        if (length == size-pos) return 0;
        const char *arg=data+pos;
        if (index && strcmp(arg,"--") == 0) break;
        if (index && (strcmp(arg,"--type") == 0 || strncmp(arg,"--type=",7) == 0)) {
            type_count++;
            gpu=strcmp(arg,"--type=gpu-process") == 0;
        }
        pos += length+1;
    }
    return type_count == 1 && gpu;
}
static int process_policy_allows(const char *policy) {
    if (!policy) return 1;  /* Standalone diagnostic behavior stays unchanged. */
    if (strcmp(policy,"gpu") != 0) return 0;
    char cmdline[65536]; size_t size=0;
    int fd=open("/proc/self/cmdline",O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    while (size < sizeof(cmdline)) {
        ssize_t n=read(fd,cmdline+size,sizeof(cmdline)-size);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { next_close(fd); return 0; }
        if (!n) break;
        size += (size_t)n;
    }
    next_close(fd);
    if (size == sizeof(cmdline)) return 0;
    return gpu_role_from_cmdline(cmdline,size);
}
static int parse_capture_headroom(const char *value, unsigned *result) {
    if (!value) { *result=0; return 1; }
    if (!*value) return 0;
    unsigned n=0;
    for (const char *p=value;*p;p++) {
        if (*p < '0' || *p > '9') return 0;
        n=n*10+(unsigned)(*p-'0');
        if (n > 16) return 0;
    }
    *result=n;
    return 1;
}
__attribute__((constructor)) static void initialize(void) {
    next_ioctl = dlsym(RTLD_NEXT, "ioctl");
    next_close = dlsym(RTLD_NEXT, "close");
    next_poll = dlsym(RTLD_NEXT, "poll");
    next_poll_chk = dlsym(RTLD_NEXT, "__poll_chk");
    for (unsigned i=0;i<MAX_CONTEXTS;i++) context_wakes[i]=-1;
    next_mmap = dlsym(RTLD_NEXT, "mmap");
    next_mmap64 = dlsym(RTLD_NEXT, "mmap64");
    const char *value = getenv("TABLET_V4L2_BRIDGE");
    const char *trace=getenv("TABLET_V4L2_TRACE");
    trace_frames=trace && strcmp(trace,"1") == 0;
    const char *policy=getenv("TABLET_V4L2_BRIDGE_PROCESS");
    gpu_process_policy=policy && strcmp(policy,"gpu") == 0;
    int valid_headroom=parse_capture_headroom(getenv("TABLET_V4L2_CAPTURE_HEADROOM"),
                                               &capture_headroom);
    const char *decouple=getenv("TABLET_V4L2_DECOUPLE_CAPTURE");
    int valid_decouple=!decouple || strcmp(decouple,"0") == 0 || strcmp(decouple,"1") == 0;
    decouple_capture=decouple && strcmp(decouple,"1") == 0;
    const char *pressure=getenv("TABLET_V4L2_POLL_BACKPRESSURE");
    int valid_pressure=!pressure || strcmp(pressure,"0") == 0 || strcmp(pressure,"1") == 0;
    poll_backpressure=pressure && strcmp(pressure,"1") == 0;
    const char *view_format=getenv("TABLET_V4L2_VIEW_FORMAT");
    int valid_view_format=!view_format || strcmp(view_format,"NV12")==0 ||
                                      strcmp(view_format,"AR24")==0 ||
                                      strcmp(view_format,"BGR4")==0;
    request_argb=view_format && (strcmp(view_format,"AR24")==0 ||
                                 strcmp(view_format,"BGR4")==0);
    rgb_view_fourcc=view_format && strcmp(view_format,"BGR4")==0 ?
                    V4L2_PIX_FMT_BGR32 : V4L2_PIX_FMT_ABGR32;
    enabled = value && strcmp(value, "1") == 0 && valid_headroom && valid_decouple && valid_pressure && valid_view_format && process_policy_allows(policy);
    if (value && strcmp(value,"1")==0 && !valid_view_format)
        log_line("tablet-v4l2 invalid view format; bridge disabled (no heap opened)\n");
    if (value && strcmp(value,"1") == 0 && !valid_pressure)
        log_line("tablet-v4l2 invalid poll backpressure setting; bridge disabled (no heap opened)\n");
    if (value && strcmp(value,"1") == 0 && !valid_decouple)
        log_line("tablet-v4l2 invalid decouple setting; bridge disabled (no heap opened)\n");
    if (value && strcmp(value,"1") == 0 && !valid_headroom)
        log_line("tablet-v4l2 invalid capture headroom; bridge disabled (no heap opened)\n");
    if (value && strcmp(value,"1") == 0 && policy && !enabled)
        log_line("tablet-v4l2 process policy disabled bridge pid=%ld (no heap opened)\n",
                 (long)getpid());
    if (enabled) {
        (void)pthread_atfork(fork_prepare,fork_parent,fork_child);
        /* Pre-open before Chromium installs its sandbox. This does not change
         * sandbox policy: allocation ioctls must still be allowed by it. */
        heap_fd = open("/dev/dma_heap/mtk_mm", O_RDWR | O_CLOEXEC);
        log_line("tablet-v4l2 bridge constructor pid=%ld heap_fd=%d errno=%d\n",
                 (long)getpid(), heap_fd, heap_fd < 0 ? errno : 0);
    }
}
int tablet_v4l2_bridge_abi(void) { return enabled ? 1 : 0; }
static struct context *find_context(int fd) {
    for (unsigned i=0; i<MAX_CONTEXTS; i++)
        if (contexts[i].active && contexts[i].fd == fd) return &contexts[i];
    return NULL;
}
static struct queue *get_queue(struct context *c, unsigned type) {
    if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) return &c->out;
    if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) return &c->cap;
    return NULL;
}
static int is_capture(struct queue *q) {
    return q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
}
static void free_allocation(struct allocation *a) {
    if (a->ptr) munmap(a->ptr, a->len);
    if (a->ptr) next_close(a->fd);
    *a = (struct allocation){.fd=-1};
}
static void free_queue(struct queue *q) {
    for (unsigned i=0; i<MAX_SLOTS; i++) {
        free_allocation(&q->slots[i].native);
        free_allocation(&q->slots[i].view);
        q->slots[i].queued = 0;
        q->slots[i].native_queued = 0;
    }
    q->count = 0; q->streaming = 0; q->next_view=0;
}
static int allocate(struct allocation *a, size_t bytes, int dma) {
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || !bytes || bytes > MAX_BYTES) return fail(EINVAL);
    size_t len = (bytes + (size_t)page-1) / (size_t)page * (size_t)page;
    int fd;
    if (dma) {
        if (heap_fd < 0) return fail(ENODEV);
        struct dma_heap_allocation_data request = {
            .len=len, .fd_flags=O_RDWR | O_CLOEXEC};
        if (next_ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &request) < 0) return -1;
        fd = (int)request.fd;
    } else {
        fd = memfd_create("tablet-v4l2-bitstream-view", MFD_CLOEXEC);
        if (fd < 0) return -1;
        if (ftruncate(fd, (off_t)len) < 0) { next_close(fd); return -1; }
    }
    void *ptr = next_mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) { int e=errno; next_close(fd); return fail(e); }
    *a = (struct allocation){.fd=fd, .ptr=ptr, .len=len};
    return 0;
}
static int sync_dma(struct allocation *a, uint64_t flags) {
    struct dma_buf_sync sync = {.flags=flags};
    return next_ioctl(a->fd, DMA_BUF_IOCTL_SYNC, &sync);
}
static int capture_layout(const struct v4l2_format *fmt, unsigned *stride,
                          unsigned *linear_bytes) {
    const struct v4l2_pix_format_mplane *f = &fmt->fmt.pix_mp;
    if (f->pixelformat != M21S || f->num_planes != 1 ||
        !f->width || !f->height || f->width > 8192 || f->height > 8192 ||
        f->width % 2 || f->height % 32) return fail(ENOTSUP);
    unsigned s = f->plane_fmt[0].bytesperline;
    if (s < f->width || s % 16) return fail(ENOTSUP);
    uint64_t bytes = (uint64_t)s * f->height * 3 / 2;
    uint64_t view = (uint64_t)f->width * f->height * 3 / 2;
    if (bytes > MAX_BYTES || view > MAX_BYTES ||
        f->plane_fmt[0].sizeimage < bytes) return fail(ENOTSUP);
    *stride=s; *linear_bytes=(unsigned)view;
    return 0;
}
static void settle_view_policy(struct context *c) {
    struct view_policy *v=view_policy(c);
    /* A plausible pre-header G_FMT is not proof of decoder initialization.
     * Only our successful OUTPUT submission followed by a resolution event
     * allows the vendor private control. Pre-header calls do not settle mode. */
    if (!request_argb || v->settled || !v->input_queued || !v->header_seen) return;
    struct tablet_mtk_color_desc color={0};
    int rc=tablet_query_color_desc(c->fd,next_ioctl,&color,NULL);
    v->rgb=rc==0 && tablet_color_desc_rgb_supported(&color);
    v->color=color;
    v->settled=1;
    log_line("tablet-v4l2 view_format=%s metadata_ret=%d primaries=%u transfer=%u matrix=%u hdr=%u full_range=%u\n",
             v->rgb ? (rgb_view_fourcc==V4L2_PIX_FMT_BGR32 ? "BGR4" : "AR24") :
                      "NV12",rc,color.color_primaries,
             color.transfer_characteristics,color.matrix_coefficients,
             color.is_hdr,color.full_range);
}
static int native_format(struct context *c, struct queue *q,
                         struct v4l2_format *result) {
    *result = (struct v4l2_format){.type=q->type};
    if (next_ioctl(c->fd, VIDIOC_G_FMT, result) < 0) return -1;
    const struct v4l2_pix_format_mplane *f = &result->fmt.pix_mp;
    if (is_capture(q)) {
        unsigned stride, bytes;
        if (capture_layout(result, &stride, &bytes) < 0) return -1;
        settle_view_policy(c);
    } else if (f->pixelformat != V4L2_PIX_FMT_H264 || f->num_planes != 1 ||
               !f->plane_fmt[0].sizeimage || f->plane_fmt[0].sizeimage > MAX_BYTES)
        return fail(ENOTSUP);
    return 0;
}
static int virtual_format(struct context *c, struct v4l2_format *fmt) {
    unsigned stride, bytes;
    if (capture_layout(fmt, &stride, &bytes) < 0) return -1;
    struct v4l2_pix_format_mplane *f = &fmt->fmt.pix_mp;
    if (view_policy(c)->rgb) {
        uint64_t rgb_bytes=(uint64_t)f->width*f->height*4;
        if (rgb_bytes>MAX_BYTES) return fail(ENOTSUP);
        f->pixelformat=rgb_view_fourcc;
        f->plane_fmt[0].bytesperline=f->width*4;
        f->plane_fmt[0].sizeimage=(unsigned)rgb_bytes;
    } else {
        f->pixelformat=V4L2_PIX_FMT_NV12;
        f->plane_fmt[0].bytesperline=f->width;
        f->plane_fmt[0].sizeimage=bytes;
    }
    return 0;
}
static unsigned virtual_offset(struct queue *q, unsigned index) {
    return (is_capture(q) ? 0x200000U : 0x100000U) + index * 4096U;
}
static int query_buffer(struct queue *q, struct v4l2_buffer *b) {
    if (b->memory != V4L2_MEMORY_MMAP || b->index >= q->count ||
        b->length < 1 || !b->m.planes) return fail(EINVAL);
    struct v4l2_plane *p=b->m.planes;
    memset(p, 0, sizeof(*p));
    p->length=(unsigned)q->slots[b->index].view.len;
    p->m.mem_offset=virtual_offset(q,b->index);
    b->length=1;
    b->flags=V4L2_BUF_FLAG_MAPPED | (q->slots[b->index].queued ? V4L2_BUF_FLAG_QUEUED : 0);
    return 0;
}
static int reqbufs(struct context *c, struct queue *q,
                   struct v4l2_requestbuffers *client) {
    if (client->memory != V4L2_MEMORY_MMAP || client->count > MAX_SLOTS ||
        client->flags) return fail(ENOTSUP);
    if (q->streaming || (q->count && client->count)) return fail(EBUSY);
    struct v4l2_requestbuffers request=*client;
    request.memory=V4L2_MEMORY_DMABUF;
    if (!client->count) {
        if (next_ioctl(c->fd, VIDIOC_REQBUFS, &request) < 0) return -1;
        if (is_capture(q)) c->capture_drained=0;
        view_policy(c)->resume_pending=0;
        free_queue(q);
        if (is_capture(q)) reset_view_policy(c);
        *client=request; client->memory=V4L2_MEMORY_MMAP;
        client->capabilities=V4L2_BUF_CAP_SUPPORTS_MMAP;
        return 0;
    }
    unsigned original_count=client->count;
    if (is_capture(q)) {
        unsigned extra=capture_headroom;
        if (extra > MAX_SLOTS-original_count) extra=MAX_SLOTS-original_count;
        request.count += extra;
    }
    if (native_format(c,q,&q->native_fmt) < 0) return -1;
    unsigned bytes=q->native_fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    unsigned view_bytes=bytes;
    if (is_capture(q)) {
        unsigned stride;
        if (capture_layout(&q->native_fmt,&stride,&view_bytes) < 0) return -1;
        struct view_policy *v=view_policy(c);
        if (v->rgb) {
            uint64_t rgb_bytes=(uint64_t)q->native_fmt.fmt.pix_mp.width *
                               q->native_fmt.fmt.pix_mp.height*4;
            if (rgb_bytes>MAX_BYTES) return fail(ENOTSUP);
            if (!v->scratch) {
                void *scratch=next_mmap(NULL,view_bytes,PROT_READ|PROT_WRITE,
                                        MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
                if (scratch==MAP_FAILED) return -1;
                v->scratch=scratch; v->scratch_bytes=view_bytes;
            }
            view_bytes=(unsigned)rgb_bytes;
        }
    }
    if (next_ioctl(c->fd, VIDIOC_REQBUFS, &request) < 0) {
        if (is_capture(q)) reset_view_policy(c);
        return -1;
    }
    if (is_capture(q))
        log_line("tablet-v4l2 CAPTURE capacity client_requested=%u policy_headroom=%u kernel_requested=%u returned=%u\n",
                 original_count,capture_headroom,
                 original_count + (capture_headroom < MAX_SLOTS-original_count ?
                                   capture_headroom : MAX_SLOTS-original_count),request.count);
    if (request.count < original_count || request.count > MAX_SLOTS) goto allocation_failed;
    q->count=request.count;
    if (is_capture(q)) c->capture_drained=0;
    for (unsigned i=0; i<q->count; i++) {
        if (allocate(&q->slots[i].native,bytes,1) < 0 ||
            allocate(&q->slots[i].view,view_bytes,is_capture(q)) < 0)
            goto allocation_failed;
    }
    *client=request; client->memory=V4L2_MEMORY_MMAP;
    client->capabilities=V4L2_BUF_CAP_SUPPORTS_MMAP;
    log_line("tablet-v4l2 pid=%ld type=%u allocated=%u native_bytes=%u view_bytes=%u\n",
             (long)getpid(),q->type,q->count,bytes,view_bytes);
    return 0;
allocation_failed: {
    int e=errno ? errno : EINVAL;
    request.count=0;
    (void)next_ioctl(c->fd,VIDIOC_REQBUFS,&request);
    free_queue(q);
    view_policy(c)->resume_pending=0;
    if (is_capture(q)) reset_view_policy(c);
    return fail(e);
}}
static void wake_view_waiters(struct context *c) {
    int fd=context_wakes[c-contexts], saved=errno;
    if (fd >= 0) {
        uint64_t one=1;
        while (write(fd,&one,sizeof(one)) < 0 && errno == EINTR) {}
    }
    errno=saved;
}
static int queue_native_capture(struct context *c, struct queue *q, unsigned index) {
    struct slot *slot=&q->slots[index];
    if (slot->native_queued) return 0;
    struct v4l2_plane plane={.length=(unsigned)slot->native.len};
    plane.m.fd=slot->native.fd;
    struct v4l2_buffer native={.type=q->type,.memory=V4L2_MEMORY_DMABUF,
        .index=index,.length=1,.m.planes=&plane,
        .flags=V4L2_BUF_FLAG_TIMESTAMP_COPY,.request_fd=-1};
    if (next_ioctl(c->fd,VIDIOC_QBUF,&native) < 0) return -1;
    slot->native_queued=1;
    return 0;
}
static int queue_buffer(struct context *c, struct queue *q, struct v4l2_buffer *b) {
    if (b->memory != V4L2_MEMORY_MMAP || b->index >= q->count ||
        b->length != 1 || !b->m.planes || q->slots[b->index].queued ||
        (b->flags & V4L2_BUF_FLAG_REQUEST_FD)) return fail(EINVAL);
    struct slot *slot=&q->slots[b->index];
    struct v4l2_plane plane=b->m.planes[0];
    if (plane.data_offset || plane.bytesused > slot->view.len) return fail(EINVAL);
    if (is_capture(q) && decouple_capture) {
        /* Client queues a view, not a particular native M21S buffer. Native
         * slots are primed once and independently recycled after copying. */
        slot->queued=1;
        wake_view_waiters(c);
        if (!c->capture_drained)
            for (unsigned i=0;i<q->count;i++) {
                if (queue_native_capture(c,q,i) < 0) { slot->queued=0; return -1; }
            }
        return 0;
    }
    if (!is_capture(q) && plane.bytesused) {
        if (plane.bytesused > slot->native.len) return fail(EINVAL);
        if (sync_dma(&slot->native,DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE) < 0) return -1;
        memcpy(slot->native.ptr,slot->view.ptr,plane.bytesused);
        if (sync_dma(&slot->native,DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE) < 0) return -1;
    }
    struct v4l2_buffer native=*b;
    native.memory=V4L2_MEMORY_DMABUF;
    native.m.planes=&plane;
    plane.m.fd=slot->native.fd;
    plane.length=(unsigned)slot->native.len;
    if (is_capture(q)) { native.request_fd=-1; plane.bytesused=0; }
    int result=next_ioctl(c->fd,VIDIOC_QBUF,&native);
    if (result == 0) {
        slot->queued=1; slot->native_queued=1;
        if (!is_capture(q)) {
            struct view_policy *v=view_policy(c);
            v->input_queued=1;
            if (plane.bytesused && v->resume_pending && c->cap.streaming) {
                /* START after LAST often precedes an immediate browser Reset.
                 * Do not resubmit its native LAST slot until real new input
                 * has actually been accepted. Client view QBUFs remain held
                 * behind capture_drained until this native priming completes.
                 */
                unsigned i;
                for (i=0;i<c->cap.count;i++)
                    if (queue_native_capture(c,&c->cap,i)<0) break;
                if (i==c->cap.count) {
                    c->capture_drained=0; v->resume_pending=0;
                } else {
                    /* OUTPUT was accepted: never falsely report its QBUF as
                     * rejected. Keep the pending intent for a later retry;
                     * already primed native slots are tracked independently. */
                    log_line("tablet-v4l2 deferred capture resume failed errno=%d\n",errno);
                }
            }
        }
    }
    return result;
}
static int dequeue_buffer(struct context *c, struct queue *q, struct v4l2_buffer *b) {
    if (b->memory != V4L2_MEMORY_MMAP || b->length < 1 || !b->m.planes)
        return fail(EINVAL);
    int independent=is_capture(q) && decouple_capture;
    unsigned view_index=0;
    if (independent) {
        /* Backpressure BEFORE consuming kernel output. Never overwrite a view
         * still owned by Chrome. Kernel POLLIN readiness remains untouched. */
        int available=0;
        for (unsigned i=0;i<q->count;i++) {
            unsigned candidate=(q->next_view+i)%q->count;
            if (q->slots[candidate].queued) {
                view_index=candidate; available=1; break;
            }
        }
        if (!available) return fail(EAGAIN);
    }
    struct v4l2_plane plane={0};
    struct v4l2_buffer native=*b;
    native.memory=V4L2_MEMORY_DMABUF; native.length=1; native.m.planes=&plane;
    if (next_ioctl(c->fd,VIDIOC_DQBUF,&native) < 0) return -1;
    if (native.index >= q->count || native.length != 1 || plane.data_offset ||
        !q->slots[native.index].native_queued ||
        (!independent && !q->slots[native.index].queued)) return fail(EPROTO);
    struct slot *native_slot=&q->slots[native.index];
    native_slot->native_queued=0;
    if (!independent) { view_index=native.index; native_slot->queued=0; }
    struct slot *view_slot=&q->slots[view_index];
    unsigned bytes=plane.bytesused;
    if (plane.bytesused > native_slot->native.len || (native.flags & V4L2_BUF_FLAG_ERROR))
        return fail(EIO);
    if (is_capture(q) && plane.bytesused) {
        unsigned stride;
        if (capture_layout(&q->native_fmt,&stride,&bytes) < 0) return -1;
        if (plane.bytesused < q->native_fmt.fmt.pix_mp.plane_fmt[0].sizeimage)
            return fail(EPROTO);
        if (sync_dma(&native_slot->native,DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ) < 0) return -1;
        if (sync_dma(&view_slot->view,DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE) < 0) {
            int e=errno;
            (void)sync_dma(&native_slot->native,DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
            return fail(e);
        }
        const struct v4l2_pix_format_mplane *f=&q->native_fmt.fmt.pix_mp;
        struct view_policy *v=view_policy(c);
        uint8_t *linear=v->rgb ? v->scratch : view_slot->view.ptr;
        size_t linear_bytes=v->rgb ? v->scratch_bytes : view_slot->view.len;
        int result=tablet_m21s_to_nv12(native_slot->native.ptr,plane.bytesused,f->width,
                                     f->height,stride,linear,linear_bytes,f->width);
        if (!result && v->rgb) {
            result=tablet_nv12_to_argb(linear,linear_bytes,f->width,f->height,
                        f->width,view_slot->view.ptr,view_slot->view.len,f->width*4,
                        v->color.matrix_coefficients,v->color.full_range);
            bytes=f->width*f->height*4;
        }
        int e=result < 0 ? -result : 0;
        if (sync_dma(&view_slot->view,DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE) < 0 && !e) e=errno;
        if (sync_dma(&native_slot->native,DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ) < 0 && !e) e=errno;
        if (e) return fail(e);
    }
    if (independent) {
        /* Pixel copy and cache synchronization are complete. The native DMA
         * buffer can return to the codec while the distinct view remains held. */
        if (native.flags & V4L2_BUF_FLAG_LAST) {
            c->capture_drained=1; view_policy(c)->resume_pending=0;
        }
        else if (queue_native_capture(c,q,native.index) < 0) return -1;
        view_slot->queued=0;
        q->next_view=(view_index+1)%q->count;
    }
    struct v4l2_plane *client_planes=b->m.planes;
    *b=native; b->index=view_index;
    b->memory=V4L2_MEMORY_MMAP; b->m.planes=client_planes;
    b->request_fd=0;
    *client_planes=(struct v4l2_plane){.bytesused=bytes,.length=(unsigned)view_slot->view.len};
    client_planes->m.mem_offset=virtual_offset(q,view_index);
    if (trace_frames) log_line("tablet-v4l2 pid=%ld DQ type=%u index=%u native_index=%u independent=%d bytes=%u flags=%08x\n",
             (long)getpid(),q->type,view_index,native.index,independent,bytes,b->flags);
    return 0;
}
static int bridge_ioctl(struct context *c, unsigned long request, void *arg) {
    struct queue *q;
    switch (request) {
    case VIDIOC_ENUM_FMT: {
        struct v4l2_fmtdesc *d=arg;
        q=get_queue(c,d->type);
        if (!q || d->index) return fail(EINVAL);
        unsigned type=d->type;
        memset(d,0,sizeof(*d)); d->type=type;
        if (is_capture(q)) {
            /* Chromium obtains post-header G_FMT before enumerating capture
             * candidates. Pre-header discovery stays conservative NV12. */
            d->pixelformat=view_policy(c)->rgb ? rgb_view_fourcc : V4L2_PIX_FMT_NV12;
            memcpy(d->description,"Converted native MTK M21S",25);
        } else {
            d->pixelformat=V4L2_PIX_FMT_H264; d->flags=V4L2_FMT_FLAG_COMPRESSED;
            memcpy(d->description,"H264 native MTK",16);
        }
        return 0;
    }
    case VIDIOC_G_FMT: case VIDIOC_S_FMT: case VIDIOC_TRY_FMT: {
        struct v4l2_format *f=arg; q=get_queue(c,f->type);
        if (!q) return fail(ENOTSUP);
        if (!is_capture(q)) {
            if (request != VIDIOC_G_FMT && f->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_H264)
                return fail(ENOTSUP);
            int result=next_ioctl(c->fd,request,arg);
            if (!result && request==VIDIOC_S_FMT) {
                view_policy(c)->input_queued=0;
                view_policy(c)->header_seen=0;
            }
            return result;
        }
        unsigned requested=f->fmt.pix_mp.pixelformat;
        unsigned w=f->fmt.pix_mp.width,h=f->fmt.pix_mp.height;
        if (native_format(c,q,f) < 0) return -1;
        if (request != VIDIOC_G_FMT &&
            (w != f->fmt.pix_mp.width || h != f->fmt.pix_mp.height)) return fail(ENOTSUP);
        if (request != VIDIOC_G_FMT && requested !=
            (view_policy(c)->rgb ? rgb_view_fourcc : V4L2_PIX_FMT_NV12))
            return fail(ENOTSUP);
        return virtual_format(c,f);
    }
    case VIDIOC_REQBUFS: {
        struct v4l2_requestbuffers *r=arg; q=get_queue(c,r->type);
        return q ? reqbufs(c,q,r) : fail(ENOTSUP);
    }
    case VIDIOC_QUERYBUF: case VIDIOC_QBUF: case VIDIOC_DQBUF: {
        struct v4l2_buffer *b=arg; q=get_queue(c,b->type);
        if (!q) return fail(ENOTSUP);
        if (request == VIDIOC_QUERYBUF) return query_buffer(q,b);
        return request == VIDIOC_QBUF ? queue_buffer(c,q,b) : dequeue_buffer(c,q,b);
    }
    case VIDIOC_EXPBUF: {
        struct v4l2_exportbuffer *e=arg; q=get_queue(c,e->type);
        if (!q || !is_capture(q) || e->plane || e->index >= q->count ||
            (e->flags & ~O_CLOEXEC)) return fail(ENOTSUP);
        e->fd=fcntl(q->slots[e->index].view.fd,
                    e->flags & O_CLOEXEC ? F_DUPFD_CLOEXEC : F_DUPFD,0);
        return e->fd < 0 ? -1 : 0;
    }
    case VIDIOC_DECODER_CMD: {
        struct v4l2_decoder_cmd *command=arg;
        int result=next_ioctl(c->fd,request,arg);
        if (!result && decouple_capture && command->cmd == V4L2_DEC_CMD_START) {
            if (c->capture_drained) {
                view_policy(c)->resume_pending=1;
            } else if (c->cap.streaming) {
                for (unsigned i=0;i<c->cap.count;i++)
                    if (queue_native_capture(c,&c->cap,i) < 0) return -1;
            }
        }
        return result;
    }
    case VIDIOC_STREAMON: case VIDIOC_STREAMOFF: {
        q=get_queue(c,*(unsigned *)arg);
        if (!q) return fail(ENOTSUP);
        int result=next_ioctl(c->fd,request,arg);
        if (!result) {
            q->streaming=request == VIDIOC_STREAMON;
            if (!q->streaming) {
                view_policy(c)->resume_pending=0;
                for (unsigned i=0;i<q->count;i++) {
                    q->slots[i].queued=0; q->slots[i].native_queued=0;
                }
                q->next_view=0;
                if (is_capture(q)) c->capture_drained=0;
                else {
                    view_policy(c)->input_queued=0;
                    view_policy(c)->header_seen=0;
                }
            }
        }
        return result;
    }
    case VIDIOC_DQEVENT: {
        int result=next_ioctl(c->fd,request,arg);
        if (!result) {
            const struct v4l2_event *event=arg;
            if (event->type==V4L2_EVENT_SOURCE_CHANGE &&
                (event->u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION) &&
                view_policy(c)->input_queued)
                view_policy(c)->header_seen=1;
        }
        return result;
    }
    case VIDIOC_CREATE_BUFS: case VIDIOC_PREPARE_BUF:
        return fail(ENOTSUP);
    default: return next_ioctl(c->fd,request,arg);
    }
}
int ioctl(int fd, unsigned long request, ...) {
    va_list ap; va_start(ap,request); void *arg=va_arg(ap,void *); va_end(ap);
    if (!next_ioctl) {
        if (!enabled) return (int)syscall(SYS_ioctl,fd,request,arg);
        return fail(ENOSYS);
    }
    if (!enabled) return next_ioctl(fd,request,arg);
    /* Chromium's V4L2Device::Ioctl takes signed int and can sign-extend an
     * ioctl command to unsigned long. Linux consumes the lower32bits. Keep
     * original bits for unrelated forwarding; normalize only bridge dispatch. */
    unsigned long command=(uint32_t)request;
    pthread_mutex_lock(&mutex);
    struct context *c=find_context(fd);
    if (!c && command == VIDIOC_QUERYCAP) {
        int result=next_ioctl(fd,request,arg);
        if (!result) {
            struct v4l2_capability *caps=arg; struct stat st;
            if (strcmp((char *)caps->driver,"mtk-vcodec-dec") == 0 &&
                strcmp((char *)caps->card,"platform:mt6789") == 0 &&
                fstat(fd,&st) == 0 && S_ISCHR(st.st_mode)) {
                int flags=fcntl(fd,F_GETFL);
                if (flags < 0 || !(flags & O_NONBLOCK)) {
                    log_line("tablet-v4l2 refusing blocking device fd=%d\n",fd);
                    pthread_mutex_unlock(&mutex); return fail(ENOTSUP);
                }
                for (unsigned i=0;i<MAX_CONTEXTS;i++) if (!contexts[i].active) {
                    c=&contexts[i]; *c=(struct context){.active=1,.fd=fd};
                    memset(&view_policies[i],0,sizeof(view_policies[i]));
                    c->out.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                    c->cap.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                    if (poll_backpressure && decouple_capture) {
                        context_wakes[i]=eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC);
                        if (context_wakes[i] < 0) {
                            memset(c,0,sizeof(*c)); c=NULL; result=-1; break;
                        }
                    }
                    log_line("tablet-v4l2 matched pid=%ld fd=%d\n",(long)getpid(),fd);
                    break;
                }
                if (!c) result=fail(EMFILE);
            }
        }
        int e=errno;
        if (c) refresh_finish_requirement(c);
        pthread_mutex_unlock(&mutex); errno=e; return result;
    }
    if (!c) {
        pthread_mutex_unlock(&mutex);
        return next_ioctl(fd,request,arg);
    }
    int result=c->revoked ? fail(EOWNERDEAD) : bridge_ioctl(c,command,arg);
    int e=errno; refresh_finish_requirement(c);
    pthread_mutex_unlock(&mutex); errno=e; return result;
}
static void *bridge_mmap(void *addr,size_t len,int prot,int flags,int fd,
                         off64_t offset,int large) {
    /* Chromium allocator initialization may mmap before this DSO constructor.
     * Resolve no symbols here: dlsym could recurse into this very function. */
    if ((!large && !next_mmap) || (large && !next_mmap64)) {
        if (!enabled) return (void *)syscall(SYS_mmap,addr,len,prot,flags,fd,offset);
        errno=ENOSYS; return MAP_FAILED;
    }
    if (!enabled) return large ? next_mmap64(addr,len,prot,flags,fd,offset) :
                                next_mmap(addr,len,prot,flags,fd,(off_t)offset);
    pthread_mutex_lock(&mutex);
    struct context *c=find_context(fd);
    if (!c) {
        pthread_mutex_unlock(&mutex);
        return large ? next_mmap64(addr,len,prot,flags,fd,offset) :
                       next_mmap(addr,len,prot,flags,fd,(off_t)offset);
    }
    if (c->revoked) {
        pthread_mutex_unlock(&mutex); errno=EOWNERDEAD; return MAP_FAILED;
    }
    void *result=MAP_FAILED; errno=EINVAL;
    if (flags == MAP_SHARED && !addr && (prot & ~(PROT_READ | PROT_WRITE)) == 0) {
        struct queue *qs[]={&c->out,&c->cap};
        for (unsigned j=0;j<2;j++) for (unsigned i=0;i<qs[j]->count;i++) {
            struct allocation *view=&qs[j]->slots[i].view;
            if (offset == virtual_offset(qs[j],i) && len && len <= view->len) {
                result=next_mmap(NULL,len,prot,flags,view->fd,0);
                goto done;
            }
        }
    }
done:;
    int e=errno; pthread_mutex_unlock(&mutex); errno=e; return result;
}
void *mmap(void *addr,size_t len,int prot,int flags,int fd,off_t offset) {
    return bridge_mmap(addr,len,prot,flags,fd,offset,0);
}
void *mmap64(void *addr,size_t len,int prot,int flags,int fd,off64_t offset) {
    return bridge_mmap(addr,len,prot,flags,fd,offset,1);
}
static int raw_poll(struct pollfd *fds,nfds_t count,int timeout) {
#ifdef SYS_poll
    return (int)syscall(SYS_poll,fds,count,timeout);
#else
    struct timespec duration={.tv_sec=timeout/1000,.tv_nsec=(timeout%1000)*1000000L};
    return (int)syscall(SYS_ppoll,fds,count,timeout < 0 ? NULL : &duration,NULL,8);
#endif
}
static int64_t monotonic_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t) < 0) return -1;
    return (int64_t)t.tv_sec*INT64_C(1000000000)+t.tv_nsec;
}
/* Bound the temporary poll array; larger/unrelated calls preserve pass-through.
 * Chromium's WaitOnceForEvents uses two FDs (video + its own reset wakefd). */
#define MAX_POLL_FDS 128
int poll(struct pollfd *fds,nfds_t count,int timeout) {
    if (!next_poll) {
        if (!enabled) return raw_poll(fds,count,timeout);
        return fail(ENOSYS);
    }
    if (!enabled || !poll_backpressure || !decouple_capture || !count || !fds ||
        count > MAX_POLL_FDS || timeout == 0) return next_poll(fds,count,timeout);
    int64_t start=monotonic_ns();
    if (start < 0) return next_poll(fds,count,timeout);
    int64_t deadline=timeout < 0 ? INT64_MAX : start+(int64_t)timeout*1000000;
    for (;;) {
        struct pollfd wait_fds[MAX_POLL_FDS+MAX_CONTEXTS];
        int wake_dups[MAX_CONTEXTS];
        unsigned wake_count=0,gated=0;
        int seen[MAX_CONTEXTS]={0};
        memcpy(wait_fds,fds,count*sizeof(*fds));
        for (nfds_t i=0;i<count;i++) wait_fds[i].revents=0;
        pthread_mutex_lock(&mutex);
        for (nfds_t i=0;i<count;i++) {
            if (!(fds[i].events & POLLIN)) continue;
            struct context *c=find_context(fds[i].fd);
            if (!c || c->revoked || !c->cap.streaming || !c->cap.count) continue;
            int available=0;
            for (unsigned j=0;j<c->cap.count;j++) available |= c->cap.slots[j].queued;
            if (available) continue;
            unsigned index=(unsigned)(c-contexts);
            if (context_wakes[index] < 0) continue;
            if (!seen[index]) {
                /* Drain stale notifications while QBUF cannot race us. A QBUF
                 * after unlock both updates view state and signals this FD. */
                uint64_t value;
                while (read(context_wakes[index],&value,sizeof(value)) < 0 && errno == EINTR) {}
                int duplicate=fcntl(context_wakes[index],F_DUPFD_CLOEXEC,0);
                if (duplicate < 0) continue;
                wake_dups[wake_count]=duplicate;
                wait_fds[count+wake_count]=(struct pollfd){.fd=duplicate,.events=POLLIN};
                wake_count++; seen[index]=1;
            }
            wait_fds[i].events &= (short)~POLLIN;
            gated++;
        }
        if (gated && trace_frames && trace_poll_gates++ < 8)
            log_line("tablet-v4l2 poll gate pid=%ld exhausted_fds=%u private_wakes=%u timeout=%d retry_ms=20\n",
                     (long)getpid(),gated,wake_count,timeout);
        pthread_mutex_unlock(&mutex);
        int64_t now=monotonic_ns();
        int remaining=timeout < 0 ? -1 :
            now >= deadline ? 0 : (int)((deadline-now+999999)/1000000);
        if (!gated) {
            for (unsigned i=0;i<wake_count;i++) next_close(wake_dups[i]);
            return next_poll(fds,count,remaining);
        }
        int slice=remaining < 0 || remaining > 20 ? 20 : remaining;
        int result=next_poll(wait_fds,count+wake_count,slice), saved=errno;
        int internal=0,visible=0;
        if (result >= 0) {
            for (nfds_t i=0;i<count;i++) {
                fds[i].revents=wait_fds[i].revents;
                visible += fds[i].revents != 0;
            }
            for (unsigned i=0;i<wake_count;i++)
                internal |= wait_fds[count+i].revents != 0;
        }
        for (unsigned i=0;i<wake_count;i++) next_close(wake_dups[i]);
        if (result < 0) { errno=saved; return result; }
        if (visible) return visible;  /* Original wakefd/revents stay intact. */
        if (internal) continue;      /* Recheck view availability, no fake wake. */
        /* Every20ms allow one ACTUAL ready callback. This also lets Chromium
         * enqueue the free slot its release observer returns after TryEnqueue.
         * Never manufacture a POLLIN event or consume native capture here. */
        result=next_poll(fds,count,0);
        if (result) return result;
        now=monotonic_ns();
        if (timeout >= 0 && now >= deadline) return 0;
    }
}
int __poll_chk(struct pollfd *fds,nfds_t count,int timeout,size_t fds_bytes) {
    /* Match libc's overflow-safe fortify object-size check BEFORE any access.
     * Never route an undersized object through the ordinary poll path. */
    if (count > fds_bytes/sizeof(*fds)) {
        if (next_poll_chk) return next_poll_chk(fds,count,timeout,fds_bytes);
        /* Possible pre-constructor misuse: libc's standard fortify failure,
         * without lazy dlsym (which could recurse through allocator setup). */
        __chk_fail();
    }
    if (trace_frames &&
        atomic_fetch_add_explicit(&trace_chk_entries,1,memory_order_relaxed) < 4)
        log_line("tablet-v4l2 __poll_chk pid=%ld nfds=%lu timeout=%d bytes=%zu pressure=%d decouple=%d\n",
                 (long)getpid(),(unsigned long)count,timeout,fds_bytes,
                 poll_backpressure,decouple_capture);
    return poll(fds,count,timeout);
}
int close(int fd) {
    if (!next_close) {
        if (!enabled) return (int)syscall(SYS_close,fd);
        return fail(ENOSYS);
    }
    if (!enabled) return next_close(fd);
    pthread_mutex_lock(&mutex);
    struct context *c=find_context(fd);
    int result=next_close(fd), e=errno;
    if (c) {
        int wake=context_wakes[c-contexts];
        if (wake >= 0) next_close(wake);
        context_wakes[c-contexts]=-1;
        free_queue(&c->out); free_queue(&c->cap); reset_view_policy(c);
        memset(view_policy(c),0,sizeof(*view_policy(c)));
        memset(c,0,sizeof(*c));
        refresh_finish_requirement(c);
    }
    pthread_mutex_unlock(&mutex); errno=e; return result;
}
