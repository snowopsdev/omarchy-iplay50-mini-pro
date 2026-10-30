/* The tablet's downstream DRM driver resets on DRM_IOCTL_MODE_CURSOR, even
 * when clearing an unused cursor. Use only with software cursors enabled.
 * All non-cursor operations are forwarded unchanged. */
extern void *dlsym(void *, const char *);
extern int *__errno_location(void);
#define RTLD_NEXT ((void *)-1L)
struct cursor_request {
    unsigned flags, crtc;
    int x, y;
    unsigned width, height, handle;
};

int ioctl(int fd, unsigned long req, ...) {
    __builtin_va_list ap;
    unsigned long arg;
    int (*real)(int, unsigned long, ...);
    unsigned cmd = req & 255;
    __builtin_va_start(ap, req);
    arg = __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    if (((req >> 8) & 255) == 'd' && (cmd == 0xa3 || cmd == 0xbb)) {
        const struct cursor_request *cursor = (const struct cursor_request *)arg;
        if (cursor && (cursor->flags & 1) && !cursor->handle)
            return 0; /* already disabled: hardware cursor allocations are rejected */
        *__errno_location() = 95; /* EOPNOTSUPP: caller must render the cursor */
        return -1;
    }
    real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    if (!real) { *__errno_location() = 38; return -1; }
    return real(fd, req, arg);
}
