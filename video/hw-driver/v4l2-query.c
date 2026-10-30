/* Query one already-initialized V4L2 decoder. No streaming/private ioctls.
 * Opening this vendor decoder can perform VCU IPC: daemon READY is required.
 * Build natively: cc -O2 -Wall -Wextra -Werror v4l2-query.c -o v4l2-query
 * Run with externally retained output and a bounded systemd service/timeout.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int call(int fd, unsigned long request, void *arg, const char *name) {
    printf("ENTER %s 0x%lx\n", name, request);
    errno = 0;
    int ret;
    do { ret = ioctl(fd, request, arg); } while (ret < 0 && errno == EINTR);
    int saved = errno;
    printf("RETURN %s ret=%d errno=%d (%s)\n", name, ret, saved,
           saved ? strerror(saved) : "ok");
    errno = saved;
    return ret;
}
#define IO(fd, request, arg) call(fd, request, arg, #request)

static const char *fourcc(uint32_t f, char out[5]) {
    for (unsigned i = 0; i < 4; i++) {
        unsigned char c = (f >> (8 * i)) & 255;
        out[i] = c >= 32 && c < 127 ? (char)c : '.';
    }
    out[4] = 0;
    return out;
}

static void formats(int fd, enum v4l2_buf_type type) {
    for (unsigned i = 0; i < 128; i++) {
        struct v4l2_fmtdesc fmt = {.index = i, .type = type};
        if (IO(fd, VIDIOC_ENUM_FMT, &fmt) < 0) break;
        char code[5];
        printf("FORMAT queue=%u index=%u fourcc=%s hex=%08x flags=%08x name=%.*s\n",
               type, i, fourcc(fmt.pixelformat, code), fmt.pixelformat,
               fmt.flags, (int)sizeof(fmt.description), fmt.description);
        if (type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) continue;
        struct v4l2_frmsizeenum size = {.pixel_format = fmt.pixelformat};
        if (IO(fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0) {
            if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                printf("SIZE %s discrete=%ux%u\n", code,
                       size.discrete.width, size.discrete.height);
            else
                printf("SIZE %s type=%u width=%u..%u/%u height=%u..%u/%u vendor_profile=%x vendor_level=%x\n",
                       code, size.type, size.stepwise.min_width,
                       size.stepwise.max_width, size.stepwise.step_width,
                       size.stepwise.min_height, size.stepwise.max_height,
                       size.stepwise.step_height, size.reserved[0], size.reserved[1]);
        }
    }
}

static void control(int fd, uint32_t id) {
    struct v4l2_queryctrl query = {.id = id};
    if (IO(fd, VIDIOC_QUERYCTRL, &query) < 0) return;
    printf("CTRL id=%08x type=%u name=%.*s min=%d max=%d step=%d default=%d flags=%08x\n",
           id, query.type, (int)sizeof(query.name), query.name,
           query.minimum, query.maximum, query.step, query.default_value,
           query.flags);
    if (query.type == V4L2_CTRL_TYPE_MENU ||
        query.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
        for (int i = query.minimum; i <= query.maximum && i < 128; i++) {
            struct v4l2_querymenu menu = {.id = id, .index = (uint32_t)i};
            if (IO(fd, VIDIOC_QUERYMENU, &menu) == 0)
                printf("MENU id=%08x index=%d name=%.*s\n", id, i,
                       (int)sizeof(menu.name), menu.name);
        }
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/videoN (requires ready vendor daemon)\n", argv[0]);
        return 2;
    }
    printf("ENTER open %s\n", argv[1]);
    int fd = open(argv[1], O_RDWR | O_NONBLOCK | O_CLOEXEC);
    printf("RETURN open fd=%d errno=%d\n", fd, fd < 0 ? errno : 0);
    if (fd < 0) return 1;
    struct v4l2_capability caps = {0};
    if (IO(fd, VIDIOC_QUERYCAP, &caps) < 0) { close(fd); return 1; }
    printf("CAPS driver=%.*s card=%.*s bus=%.*s capabilities=%08x device_caps=%08x\n",
           (int)sizeof(caps.driver), caps.driver, (int)sizeof(caps.card), caps.card,
           (int)sizeof(caps.bus_info), caps.bus_info, caps.capabilities, caps.device_caps);
    formats(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    formats(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    control(fd, V4L2_CID_MPEG_VIDEO_H264_PROFILE);
    control(fd, V4L2_CID_MPEG_VIDEO_VP9_PROFILE);
    control(fd, V4L2_CID_MIN_BUFFERS_FOR_CAPTURE);
    struct v4l2_event_subscription subscription = {.type = V4L2_EVENT_SOURCE_CHANGE};
    IO(fd, VIDIOC_SUBSCRIBE_EVENT, &subscription);
    printf("ENTER close\n");
    int ret = close(fd);
    printf("RETURN close ret=%d errno=%d\n", ret, ret < 0 ? errno : 0);
    return ret < 0;
}
