/* reboot-bootloader: freestanding static aarch64 helper (no libc).
 *   reboot-bootloader [-n] [-r reason] [mountpoint...]
 * sync(); remount each given mountpoint read-only (best effort); sync();
 * reboot(LINUX_REBOOT_CMD_RESTART2, reason) with reason defaulting to "bootloader",
 * which MediaTek lk turns into fastboot mode (if the kernel's reboot-mode driver passes it on).
 * -n = dry run (print what would happen, never calls reboot(2)).
 * Returns (exit 1) only if reboot(2) failed. */
typedef unsigned long u64;
typedef long s64;

#define SYS_write   64
#define SYS_mount   40
#define SYS_sync    81
#define SYS_reboot  142
#define SYS_exit    93
#define MS_RDONLY   1UL
#define MS_REMOUNT  32UL

static s64 sc4(s64 n, s64 a, s64 b, s64 c, s64 d)
{
	register s64 x8 __asm__("x8") = n;
	register s64 x0 __asm__("x0") = a;
	register s64 x1 __asm__("x1") = b;
	register s64 x2 __asm__("x2") = c;
	register s64 x3 __asm__("x3") = d;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory", "cc");
	return x0;
}
static s64 sc5(s64 n, s64 a, s64 b, s64 c, s64 d, s64 e)
{
	register s64 x8 __asm__("x8") = n;
	register s64 x0 __asm__("x0") = a;
	register s64 x1 __asm__("x1") = b;
	register s64 x2 __asm__("x2") = c;
	register s64 x3 __asm__("x3") = d;
	register s64 x4 __asm__("x4") = e;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory", "cc");
	return x0;
}

static u64 slen(const char *s) { u64 n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc4(SYS_write, 1, (s64)s, slen(s), 0); }
static int eq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }

int c_main(u64 *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	const char *reason = "bootloader";
	int dry = 0, i;

	for (i = 1; i < argc; i++) {
		if (eq(argv[i], "-n")) dry = 1;
		else if (eq(argv[i], "-r") && i + 1 < argc) reason = argv[++i];
		else {
			if (dry) { out("would remount ro: "); out(argv[i]); out("\n"); }
			else sc5(SYS_mount, 0, (s64)argv[i], 0, MS_REMOUNT | MS_RDONLY, 0);
		}
	}
	if (dry) { out("would sync + reboot(RESTART2, \""); out(reason); out("\")\n"); return 0; }
	sc4(SYS_sync, 0, 0, 0, 0);
	/* reboot(0xfee1dead, 672274793 (0x28121969), LINUX_REBOOT_CMD_RESTART2 = 0xA1B2C3D4, reason) */
	s64 r = sc4(SYS_reboot, 0xfee1dead, 672274793, 0xA1B2C3D4, (s64)reason);
	out("reboot-bootloader: reboot(2) failed\n");
	return r ? 1 : 0;
}

__asm__(
	".text\n.global _start\n_start:\n"
	"	mov x0, sp\n"
	"	and sp, x0, #-16\n"
	"	bl c_main\n"
	"	mov x8, #93\n"
	"	svc 0\n");
