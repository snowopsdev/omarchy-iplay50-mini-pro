/* Narrow synchronous trace of display-changing DRM calls, for watchdog diagnosis.
 * No ioctl is changed or suppressed. The small log survives an abrupt reset. */
extern void *dlsym(void *, const char *);
extern int open(const char *, int, ...);
extern long write(int, const void *, unsigned long);
extern int close(int);
extern int *__errno_location(void);
extern long syscall(long, ...);
#define RTLD_NEXT ((void *)-1L)

static char *hex(char *p, unsigned long x) {
    static const char digits[] = "0123456789abcdef";
    char tmp[16]; unsigned n = 0;
    do { tmp[n++] = digits[x & 15]; x >>= 4; } while (x);
    while (n) *p++ = tmp[--n];
    return p;
}

static void record(char phase, int fd, unsigned long req, long result) {
    char buf[100], *p = buf;
    int saved = *__errno_location();
    int logfd = open("/var/log/omarchy-gfx/drm-ioctl.log", 1 | 64 | 1024 | 4096, 0644);
    if (logfd >= 0) {
        *p++ = phase; *p++ = ' ';
        p = hex(p, (unsigned long)syscall(178)); *p++ = ' ';
        p = hex(p, (unsigned long)fd); *p++ = ' ';
        p = hex(p, req); *p++ = ' ';
        p = hex(p, (unsigned long)result); *p++ = '\n';
        write(logfd, buf, (unsigned long)(p - buf));
        close(logfd);
    }
    *__errno_location() = saved;
}

int ioctl(int fd, unsigned long req, ...) {
    __builtin_va_list ap;
    unsigned long arg;
    int result;
    int (*real)(int, unsigned long, ...);
    unsigned cmd = req & 255;
    int traced = ((req >> 8) & 255) == 'd' &&
        (cmd == 0xa2 || cmd == 0xa3 || cmd == 0xa5 || cmd == 0xab ||
         cmd == 0xaf || cmd == 0xb0 || cmd == 0xb4 || cmd == 0xb7 ||
         cmd == 0xb8 || cmd == 0xba || cmd == 0xbb || cmd == 0xbc);
    __builtin_va_start(ap, req);
    arg = __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    if (!real) { *__errno_location() = 38; return -1; }
    if (traced) record('>', fd, req, 0);
    result = real(fd, req, arg);
    if (traced) record('<', fd, req, result);
    return result;
}
