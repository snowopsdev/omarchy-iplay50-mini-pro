/*
 * mtk-bt-bridge - expose the MediaTek connsys Bluetooth character device (/dev/stpbt, driver bt_drv_connac1x) to BlueZ.
 *
 * /dev/stpbt speaks raw HCI H4 packets (type byte + payload; opening it runs mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT), i.e. it needs the
 * WMT launcher daemon from mtk-wmt-init to be alive and connsys powered).  The kernel has no CONFIG_BT_HCIVHCI, so we attach the BlueZ
 * H4 line discipline to a pty (what `btattach -B <pty> -P h4` does) and copy complete H4 packets between the pty master and /dev/stpbt.
 * Evidence: docs/05-wifi-bluetooth.md, appendix "Bluetooth" ([K:stp_chrdev_bt.c BT_open/BT_read/BT_write], N_HCI ioctls from linux/hci_uart.h).
 *
 * Controller quirks (found on the sibling MT6768 CONNAC1.x BT firmware by another port, UNVERIFIED on MT6631/MT6789 -> each can be switched off):
 *   --no-synctrain-fix : 0x0c77 (Read Synchronization Train Parameters) is answered locally; the firmware advertises but rejects it, which makes
 *                        the kernel's HCI init fail with -EBADRQC.
 *   --bdaddr FILE      : first 6 bytes = BD address in display order (nvdata APCFG/APRDEB/BT_Addr).  Written with the MediaTek vendor command
 *                        0xFC1A right after every HCI Reset completes (the Reset completion is held back until 0xFC1A is answered).
 *   --no-le-fix        : Read Local Supported Commands is patched to claim the legacy LE commands (octets 25..28), else Linux masks LE events.
 *
 * Safety: opens only /dev/stpbt, a pty, and (read-only) the optional BD address file.  Build: connectivity/build.sh (static musl aarch64).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define N_HCI 15                                   /* linux/tty.h */
#define HCIUARTSETPROTO _IOW('U', 200, int)        /* linux/hci_uart (bluetooth/hci_uart.h) */
#define HCIUARTGETDEVICE _IOR('U', 202, int)
#define HCI_UART_H4 0
#define OP_RESET 0x0c03
#define OP_SYNC_TRAIN 0x0c77
#define OP_SET_BDADDR 0xfc1a
#define OP_READ_LOCAL_COMMANDS 0x1002

static volatile sig_atomic_t g_stop;
static int g_kmsg = -1, g_verbose = 1;

static void logm(const char *fmt, ...)
{
	char m[400], l[450];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(m, sizeof m, fmt, ap);
	va_end(ap);
	int n = snprintf(l, sizeof l, "mtk-bt-bridge[%d]: %s\n", (int)getpid(), m);
	(void)!write(2, l, n);
	if (g_kmsg >= 0) {
		n = snprintf(l, sizeof l, "<6>mtk-bt-bridge: %s\n", m);
		(void)!write(g_kmsg, l, n);
	}
}

/* H4 framing: returns total length of the packet at buf[0..len) or 0 if incomplete, -1 if the type byte is unknown */
static int h4_total(const uint8_t *b, size_t len)
{
	switch (b[0]) {
	case 0x01: return len >= 4 ? 4 + b[3] : 0;                                   /* command */
	case 0x02: return len >= 5 ? 5 + (b[3] | b[4] << 8) : 0;                     /* ACL */
	case 0x03: return len >= 4 ? 4 + b[3] : 0;                                   /* SCO */
	case 0x04: return len >= 3 ? 3 + b[2] : 0;                                   /* event */
	case 0x05: return len >= 5 ? 5 + ((b[3] | b[4] << 8) & 0x3fff) : 0;          /* ISO */
	default: return -1;
	}
}
struct framer { uint8_t buf[8192]; size_t len; };
/* feed data, call cb for each complete packet */
static void framer_feed(struct framer *f, const uint8_t *d, size_t n, void (*cb)(const uint8_t *, size_t, void *), void *ctx)
{
	if (n > sizeof f->buf - f->len) n = sizeof f->buf - f->len;
	memcpy(f->buf + f->len, d, n);
	f->len += n;
	while (f->len) {
		int t = h4_total(f->buf, f->len);
		if (t < 0) { memmove(f->buf, f->buf + 1, --f->len); continue; }       /* lost sync: drop a byte */
		if (t == 0 || (size_t)t > f->len) break;
		cb(f->buf, t, ctx);
		memmove(f->buf, f->buf + t, f->len - t);
		f->len -= t;
	}
}

static int g_fix_sync = 1, g_fix_le = 1;
static uint8_t g_bdaddr[6];
static int g_have_bdaddr;
static int g_bt = -1, g_master = -1;
static uint8_t g_held[512];
static size_t g_held_len;
static time_t g_held_deadline;

static int ev_opcode(const uint8_t *p, size_t n)
{
	if (p[0] == 0x04 && p[1] == 0x0e && n >= 6) return p[4] | p[5] << 8;       /* Command Complete */
	if (p[0] == 0x04 && p[1] == 0x0f && n >= 7) return p[5] | p[6] << 8;       /* Command Status */
	return -1;
}
static void wr(int fd, const uint8_t *p, size_t n) { if (write(fd, p, n) != (ssize_t)n && g_verbose) logm("short write on fd %d: %s", fd, strerror(errno)); }

static void from_controller(const uint8_t *p, size_t n, void *ctx)
{
	(void)ctx;
	int op = ev_opcode(p, n);
	if (g_have_bdaddr && !g_held_len && op == OP_RESET && p[1] == 0x0e && n < sizeof g_held) {
		memcpy(g_held, p, n);                                                    /* hold the Reset completion back ... */
		g_held_len = n;
		g_held_deadline = time(NULL) + 2;
		uint8_t c[10] = { 0x01, OP_SET_BDADDR & 0xff, OP_SET_BDADDR >> 8, 6 };
		for (int i = 0; i < 6; i++) c[4 + i] = g_bdaddr[5 - i];                  /* HCI wants little endian */
		logm("HCI Reset completed: writing BD address %02x:%02x:%02x:%02x:%02x:%02x with vendor cmd 0xfc1a",
		     g_bdaddr[0], g_bdaddr[1], g_bdaddr[2], g_bdaddr[3], g_bdaddr[4], g_bdaddr[5]);
		wr(g_bt, c, sizeof c);
		return;
	}
	if (g_held_len && op == OP_SET_BDADDR) {                                     /* ... until the vendor command is answered */
		logm("BD address command answered (%s)", (p[1] == 0x0e && n > 6 && p[6] == 0) || (p[1] == 0x0f && n > 3 && p[3] == 0) ? "ok" : "failed");
		wr(g_master, g_held, g_held_len);
		g_held_len = 0;
		return;
	}
	if (g_fix_le && op == OP_READ_LOCAL_COMMANDS && p[1] == 0x0e && n >= 7 + 64 && p[6] == 0) {
		uint8_t b[512];
		memcpy(b, p, n);
		b[7 + 25] |= 0xf7; b[7 + 26] |= 0xff; b[7 + 27] |= 0xff; b[7 + 28] |= 0x0f;
		logm("Read Local Supported Commands: legacy LE command bits (octets 25..28) set");
		wr(g_master, b, n);
		return;
	}
	wr(g_master, p, n);
}
static void from_host(const uint8_t *p, size_t n, void *ctx)
{
	(void)ctx;
	if (g_fix_sync && p[0] == 0x01 && n >= 4 && (p[1] | p[2] << 8) == OP_SYNC_TRAIN) {
		/* Command Complete: ncmd 1, opcode, status 0, interval(2) timeout(4) service data(1) */
		uint8_t r[14] = { 0x04, 0x0e, 0x0b, 0x01, OP_SYNC_TRAIN & 0xff, OP_SYNC_TRAIN >> 8 };
		logm("answering HCI_Read_Synchronization_Train_Parameters locally");
		wr(g_master, r, sizeof r);
		return;
	}
	wr(g_bt, p, n);
}

static void on_sig(int s) { (void)s; g_stop = 1; }

static int st_cnt;
static void st_cb(const uint8_t *p, size_t n, void *c) { (void)p; (void)c; st_cnt += (int)n; }
static int selftest(void)
{
	/* framer: two packets split across feeds */
	struct framer f = { .len = 0 };
	int cnt;
	uint8_t a[] = { 0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00, 0x04, 0x05, 0x04, 1, 2, 3, 4 };
	framer_feed(&f, a, 5, st_cb, NULL);
	framer_feed(&f, a + 5, sizeof a - 5, st_cb, NULL);
	cnt = st_cnt;
	printf("selftest framer: consumed %d bytes of %zu, pending %zu: %s\n", cnt, sizeof a, f.len, cnt == (int)sizeof a && f.len == 0 ? "PASS" : "FAIL");
	return !(cnt == (int)sizeof a && f.len == 0);
}

int main(int argc, char **argv)
{
	const char *bdfile = NULL, *dev = "/dev/stpbt";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--bdaddr") && i + 1 < argc) bdfile = argv[++i];
		else if (!strcmp(argv[i], "--dev") && i + 1 < argc) dev = argv[++i];
		else if (!strcmp(argv[i], "--no-synctrain-fix")) g_fix_sync = 0;
		else if (!strcmp(argv[i], "--no-le-fix")) g_fix_le = 0;
		else if (!strcmp(argv[i], "--selftest")) return selftest();
		else { fprintf(stderr, "usage: mtk-bt-bridge [--dev /dev/stpbt] [--bdaddr FILE] [--no-synctrain-fix] [--no-le-fix] [--selftest]\n"); return 2; }
	}
	g_kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
	if (bdfile) {
		uint8_t raw[6];
		int fd = open(bdfile, O_RDONLY | O_CLOEXEC);
		if (fd >= 0 && read(fd, raw, 6) == 6) {
			static const uint8_t zero[6], ff[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
			if (memcmp(raw, zero, 6) && memcmp(raw, ff, 6)) { memcpy(g_bdaddr, raw, 6); g_have_bdaddr = 1; }
		}
		if (fd >= 0) close(fd);
		logm("BD address file %s: %s", bdfile, g_have_bdaddr ? "valid address loaded" : "unusable, firmware default stays");
	}
	signal(SIGTERM, on_sig); signal(SIGINT, on_sig); signal(SIGPIPE, SIG_IGN);

	g_bt = open(dev, O_RDWR | O_NOCTTY | O_CLOEXEC);                              /* BT_open: WMT func on (BT) */
	if (g_bt < 0) { logm("cannot open %s: %s (is mtk-wmt-init running and connsys powered?)", dev, strerror(errno)); return 1; }
	logm("opened %s (BT function powered)", dev);

	g_master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (g_master < 0 || grantpt(g_master) < 0 || unlockpt(g_master) < 0) { logm("pty setup failed: %s", strerror(errno)); return 1; }
	char sname[64];
	if (ptsname_r(g_master, sname, sizeof sname)) { logm("ptsname failed"); return 1; }
	int slave = open(sname, O_RDWR | O_NOCTTY);
	if (slave < 0) { logm("open %s failed: %s", sname, strerror(errno)); return 1; }
	struct termios t;
	tcgetattr(slave, &t); cfmakeraw(&t); tcsetattr(slave, TCSANOW, &t);
	int ldisc = N_HCI;
	if (ioctl(slave, TIOCSETD, &ldisc) < 0) { logm("TIOCSETD N_HCI failed: %s (CONFIG_BT_HCIUART missing?)", strerror(errno)); return 1; }
	/* hci_ldisc takes the protocol id as the ioctl argument value (as hciattach does), not a pointer. */
	if (ioctl(slave, HCIUARTSETPROTO, (unsigned long)HCI_UART_H4) < 0) { logm("HCIUARTSETPROTO H4 failed: %s", strerror(errno)); return 1; }
	int hci = ioctl(slave, HCIUARTGETDEVICE, 0);   /* returns the hci index */
	logm("H4 line discipline attached to %s -> hci%d; bridging", sname, hci);

	struct framer fc = { .len = 0 }, fh = { .len = 0 };
	struct pollfd p[2] = { { .fd = g_bt, .events = POLLIN }, { .fd = g_master, .events = POLLIN } };
	while (!g_stop) {
		int r = poll(p, 2, 500);
		if (r < 0 && errno != EINTR) break;
		if (g_held_len && time(NULL) >= g_held_deadline) {
			logm("no answer to the vendor BD address command, releasing the held Reset completion");
			wr(g_master, g_held, g_held_len);
			g_held_len = 0;
		}
		if (r <= 0) continue;
		uint8_t buf[4096];
		if (p[0].revents & POLLIN) {
			ssize_t n = read(g_bt, buf, sizeof buf);
			if (n <= 0) { logm("%s closed/EOF (%zd, %s)", dev, n, strerror(errno)); break; }
			framer_feed(&fc, buf, n, from_controller, NULL);
		}
		if (p[1].revents & POLLIN) {
			ssize_t n = read(g_master, buf, sizeof buf);
			if (n > 0) framer_feed(&fh, buf, n, from_host, NULL);
		}
		if (p[0].revents & (POLLERR | POLLHUP | POLLNVAL)) { logm("poll error on %s", dev); break; }
	}
	logm("exiting; closing %s (BT function off)", dev);
	return 0;
}
