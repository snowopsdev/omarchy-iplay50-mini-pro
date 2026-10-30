/* Test shim: emulates the kernel side of the ioctls so the daemon logic (fork/pipe/poll/read/write loop) can run under qemu-user
 * against a pty.  Compiled together with mtk-wmt-init.c using -Dioctl=fake_ioctl.  NOT part of the shipped binary. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int fake_ioctl(int fd, int req, ...)
{
	va_list ap; va_start(ap, req); unsigned long arg = va_arg(ap, unsigned long); va_end(ap);
	unsigned r = (unsigned)req;
	fprintf(stderr, "FAKE-KERNEL ioctl fd=%d req=0x%08x arg=0x%lx\n", fd, r, arg);
	switch (r) {
	case 0x8004770a: return 0;       /* SOC_HW_INIT */
	case 0x80047706: return -1;      /* EXT_CHIP_PWR_ON */
	case 0x80047703: return 0x6789;
	case 0x80047709: return 0x6631;
	case 0x8004a016: return 0x6789;
	case 0x8004a00c: return arg == 0 ? 0x6789 : 0x10;   /* fw version 0x10 */
	case 0x4004a007: usleep(300000); return 0;
	case 0x4008a01f: { unsigned *p = (unsigned *)arg; fprintf(stderr, "FAKE-KERNEL rom patch type=%u name=%s\n", p[0], (char *)(p + 2)); return 0; }
	default: return 0;
	}
}
