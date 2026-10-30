/* Explicitly opt-in adaptation for MT6789's vendor V4L2 supplemental-buffer ABI.
 * Built against standard Linux headers; no magic structure offsets or private
 * kernel patch. Preserve the existing DRM cursor shim by placing this FIRST
 * in LD_PRELOAD and chaining ioctl through RTLD_NEXT.
 *
 * cc -O2 -fPIC -shared -Wall -Wextra -Werror mt6789-v4l2-compat.c \
 *    -o mt6789-v4l2-compat.so -ldl
 * TABLET_V4L2_MTK_COMPAT=1 LD_PRELOAD=.../mt6789-v4l2-compat.so:EXISTING... app
 *
 * Unverified on tablet: stage only until real decoded frames pass native probe.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <linux/videodev2.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

static int (*next_ioctl)(int, unsigned long, ...);
static int enabled;

__attribute__((constructor)) static void initialize(void) {
    next_ioctl = dlsym(RTLD_NEXT, "ioctl");
    const char *value = getenv("TABLET_V4L2_MTK_COMPAT");
    enabled = value && strcmp(value, "1") == 0;
}

static int target_decoder(int fd) {
    struct v4l2_capability caps = {0};
    if (next_ioctl(fd, VIDIOC_QUERYCAP, &caps) < 0) return 0;
    return strncmp((const char *)caps.driver, "mtk-vcodec-dec", sizeof(caps.driver)) == 0 &&
           strncmp((const char *)caps.card, "platform:mt6789", sizeof(caps.card)) == 0 &&
           (caps.device_caps & V4L2_CAP_VIDEO_M2M_MPLANE);
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    uintptr_t arg = va_arg(args, uintptr_t);
    va_end(args);
    if (!next_ioctl) { errno = ENOSYS; return -1; }
    if (enabled && request == VIDIOC_QBUF && arg) {
        struct v4l2_buffer *buffer = (struct v4l2_buffer *)arg;
        if (buffer->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
            buffer->request_fd == 0 && !(buffer->flags & V4L2_BUF_FLAG_REQUEST_FD)) {
            int saved_errno = errno;
            int matches = target_decoder(fd);
            errno = saved_errno;
            if (matches) {
                struct v4l2_buffer translated = *buffer;
                translated.request_fd = -1;
                int ret = next_ioctl(fd, request, &translated);
                int returned_errno = errno;
                translated.request_fd = buffer->request_fd;
                *buffer = translated;
                errno = returned_errno;
                return ret;
            }
        }
    }
    return next_ioctl(fd, request, arg);
}
