// SPDX-License-Identifier: GPL-2.0-only
/*
 * fgd-kernel-mode - minimal stand-in for the Android "fuelgauged" handshake on MediaTek GM3.0
 * (mt6358_battery.ko, MT6789/MT8781, PMIC MT6366).
 *
 * What it does (and ALL it does):
 *   1. read-only pre-flight of /sys/class/power_supply/battery
 *   2. open a NETLINK_FGD (26) socket, send two messages to the kernel gauge driver:
 *        FG_DAEMON_CMD_SET_DAEMON_PID (1)        - registers us as "the daemon"; needed or the kernel drops later commands
 *        FG_DAEMON_CMD_SEND_VERSION_CONTROL (5)  - deliberately advertises a protocol that does not match the kernel's
 *                                                  (118 daemon cmds / 17 kernel cmds / 776 / 39020 byte structs).  The driver's
 *                                                  own response to such a mismatch is "enable Kernel mode Gauge":
 *                                                  gm->algo.active = true; battery_algo_init(gm), i.e. the GM3.0 algorithm that is
 *                                                  compiled into the driver (mtk_battery_algo.c) takes over and publishes UISOC.
 *   3. poll sysfs until capacity is a real percentage (or time out).
 *
 * It never writes to sysfs, never opens a device node, never touches charger/PMIC registers, and never issues any
 * FG_DAEMON_CMD_SET_* / GET_* command.  Whatever the kernel then does in its own gauge algorithm is the same thing it
 * does on Android after the stock daemon connects.
 *
 * Evidence summary: docs/11-power-battery.md (the original ANALYSIS.md was not preserved). Build: power/src/build-fgd.sh.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef FGD_TEST_UNIX /* host-side test build only: talk AF_UNIX datagrams to tests/fake-kernel.py instead of NETLINK_FGD */
#include <sys/un.h>
#endif

/* Local copies of the few <linux/netlink.h> bits we need (the Alpine musl-dev sysroot ships no linux/ headers). ABI-stable. */
#define NLMSG_HDRLEN         16
struct nlmsghdr {
	uint32_t nlmsg_len;
	uint16_t nlmsg_type;
	uint16_t nlmsg_flags;
	uint32_t nlmsg_seq;
	uint32_t nlmsg_pid;
};
struct sockaddr_nl {
	sa_family_t nl_family;
	uint16_t    nl_pad;
	uint32_t    nl_pid;
	uint32_t    nl_groups;
};
#ifndef AF_NETLINK
#define AF_NETLINK 16
#endif

#define NETLINK_FGD          26
#define FGD_NL_MAGIC         2015060303u /* 0x781b614f */
#define FGD_NL_MSG_MAX_LEN   9200
#define FGD_NL_HDR_LEN       32          /* sizeof(struct fgd_nl_msg) - FGD_NL_MSG_MAX_LEN */

/* enum fg_daemon_cmds (index in the kernel enum) */
#define FG_DAEMON_CMD_SET_DAEMON_PID        1
#define FG_DAEMON_CMD_SEND_VERSION_CONTROL  5

/* values the genuine daemon sends, which this driver build compares against (fg_daemon_send_data, 0x1d020..0x1d050) */
#define KERNEL_DAEMON_CMDS   118
#define KERNEL_KERNEL_CMDS   17
#define KERNEL_CUSTOM_DATA   776
#define KERNEL_CUSTOM_TABLE  39020

struct fgd_nl_msg {
	uint32_t nl_cmd;
	uint32_t fgd_cmd;
	uint32_t fgd_cmd_hash;
	uint32_t fgd_subcmd;
	uint32_t fgd_subcmd_para1;
	uint32_t fgd_data_len;
	uint32_t fgd_ret_data_len;
	uint32_t identity;
	char     fgd_data[FGD_NL_MSG_MAX_LEN];
};

struct fgd_cmd_param_t_4 { /* payload of the SEND_* data commands */
	uint32_t total_size;
	uint32_t size;
	uint32_t idx;
	char     input[2048];
};

struct version_control {
	int32_t android_version;
	int32_t daemon_cmds;
	int32_t kernel_cmds;
	int32_t custom_data_len;
	int32_t custom_table_len;
};

_Static_assert(offsetof(struct fgd_nl_msg, fgd_data) == FGD_NL_HDR_LEN, "fgd header size");
_Static_assert(sizeof(struct version_control) == 20, "VersionControl size");
_Static_assert(sizeof(struct fgd_cmd_param_t_4) == 2060, "param_t_4 size");

static int verbose;
static int dry_run;
static const char *psy_root = "/sys/class/power_supply";
static const char *psy_name = "battery";

static void logmsg(int lvl, const char *fmt, ...)
{
	va_list ap;
	char buf[512];

	if (lvl > verbose)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	fprintf(stderr, "fgd-kernel-mode: %s\n", buf);
}
#define INFO(...)  logmsg(0, __VA_ARGS__)
#define VLOG(...)  logmsg(1, __VA_ARGS__)
#define VVLOG(...) logmsg(2, __VA_ARGS__)

static void hexdump_always(const char *tag, const void *p, size_t n)
{
	const unsigned char *b = p;
	size_t i;

	for (i = 0; i < n; i += 16) {
		char line[16 * 3 + 1];
		size_t j, o = 0;

		for (j = i; j < i + 16 && j < n; j++)
			o += (size_t)snprintf(line + o, sizeof line - o, "%02x ", b[j]);
		fprintf(stderr, "fgd-kernel-mode: %s %04zx: %s\n", tag, i, line);
	}
}

static void hexdump(const char *tag, const void *p, size_t n)
{
	if (verbose >= 2)
		hexdump_always(tag, p, n);
}

/* ---- sysfs (read-only) ---- */
static int sysfs_read_str(const char *attr, char *out, size_t len)
{
	char path[256];
	ssize_t n;
	int fd;

	snprintf(path, sizeof path, "%s/%s/%s", psy_root, psy_name, attr);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	n = read(fd, out, len - 1);
	close(fd);
	if (n < 0)
		return -errno;
	out[n] = 0;
	while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' '))
		out[--n] = 0;
	return 0;
}

static int sysfs_read_int(const char *attr, long *val)
{
	char buf[64], *e;
	int r = sysfs_read_str(attr, buf, sizeof buf);

	if (r)
		return r;
	*val = strtol(buf, &e, 0);
	return (e == buf) ? -EINVAL : 0;
}

struct state {
	int  have_type, is_battery;
	int  have_capacity, have_init_done, have_disable;
	long capacity, init_done, disable;
};

static void read_state(struct state *s)
{
	char t[64];

	memset(s, 0, sizeof *s);
	if (sysfs_read_str("type", t, sizeof t) == 0) {
		s->have_type = 1;
		s->is_battery = !strcmp(t, "Battery");
	}
	s->have_capacity  = sysfs_read_int("capacity", &s->capacity) == 0;
	s->have_init_done = sysfs_read_int("init_done", &s->init_done) == 0;
	s->have_disable   = sysfs_read_int("disable", &s->disable) == 0;
}

/* Returns 0 if we should proceed, 1 if nothing to do (already running), -1 if we must refuse. */
static int preflight(const struct state *s, int force)
{
	INFO("pre-flight: %s/%s type=%s capacity=%s%ld init_done=%s%ld disable=%s%ld",
	     psy_root, psy_name, s->have_type ? (s->is_battery ? "Battery" : "other") : "?",
	     s->have_capacity ? "" : "?", s->capacity,
	     s->have_init_done ? "" : "?", s->init_done,
	     s->have_disable ? "" : "?", s->disable);

	if (!s->have_type || !s->is_battery) {
		INFO("refuse: %s/%s is not a Battery power_supply", psy_root, psy_name);
		return -1;
	}
	if (!s->have_init_done || !s->have_disable) {
		INFO("refuse: no init_done/disable attributes - not the MediaTek GM3.0 mtk_battery driver");
		return -1;
	}
	if (s->disable == 1) {
		INFO("refuse: battery/disable=1 (GM3.0 switched off, capacity is pinned to 50); not touching it");
		return -1;
	}
	if (s->have_capacity && s->capacity >= 0 && s->init_done == 1) {
		INFO("gauge already initialised (capacity=%ld, init_done=1): nothing to do", s->capacity);
		return force ? 0 : 1;
	}
	if (s->init_done == 1 && !force) {
		INFO("refuse: init_done=1 but capacity=%ld - a daemon may already own the gauge; use --force to override",
		     s->have_capacity ? s->capacity : -999);
		return -1;
	}
	return 0;
}

/* ---- message construction ---- */
static size_t build_msg(unsigned char *out, uint32_t seq, struct fgd_nl_msg *m)
{
	struct nlmsghdr nlh;
	size_t plen = FGD_NL_HDR_LEN + m->fgd_data_len;

	m->identity = FGD_NL_MAGIC;
	memset(&nlh, 0, sizeof nlh);
	nlh.nlmsg_len = (uint32_t)(NLMSG_HDRLEN + plen);
	nlh.nlmsg_type = 0;
	nlh.nlmsg_flags = 0;
	nlh.nlmsg_seq = seq;
	nlh.nlmsg_pid = (uint32_t)getpid();
	memcpy(out, &nlh, sizeof nlh);
	memcpy(out + NLMSG_HDRLEN, m, plen);
	return NLMSG_HDRLEN + plen;
}

static void make_set_pid(struct fgd_nl_msg *m)
{
	uint32_t pid = (uint32_t)getpid();

	memset(m, 0, sizeof *m);
	m->fgd_cmd = FG_DAEMON_CMD_SET_DAEMON_PID;
	m->fgd_subcmd = 0;
	m->fgd_subcmd_para1 = FGD_NL_HDR_LEN; /* fg_cmd_check(): subcmd==0 && para1!=32 makes the kernel msleep(10 s) */
	m->fgd_data_len = sizeof pid;
	m->fgd_ret_data_len = sizeof(struct fgd_cmd_param_t_4);
	memcpy(m->fgd_data, &pid, sizeof pid);
}

static void make_version_control(struct fgd_nl_msg *m)
{
	struct fgd_cmd_param_t_4 *p = (struct fgd_cmd_param_t_4 *)m->fgd_data;
	struct version_control vc = { 0, 0, 0, 0, 0 }; /* "I implement none of your commands" */

	memset(m, 0, sizeof *m);
	m->fgd_cmd = FG_DAEMON_CMD_SEND_VERSION_CONTROL;
	m->fgd_subcmd = 0;
	m->fgd_subcmd_para1 = FGD_NL_HDR_LEN;
	p->total_size = sizeof vc; /* kernel logs "vc size is different" if this is not 20 */
	p->size = sizeof vc;
	p->idx = 0;
	memcpy(p->input, &vc, sizeof vc);
	m->fgd_data_len = 12 + sizeof vc;
	m->fgd_ret_data_len = sizeof(struct fgd_cmd_param_t_4); /* kernel writes total_size/size/idx into the reply buffer */
}

/* ---- transport ---- */
static int nl_open(void)
{
#ifdef FGD_TEST_UNIX
	struct sockaddr_un me = { .sun_family = AF_UNIX };
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

	if (fd < 0)
		return -1;
	snprintf(me.sun_path + 1, sizeof me.sun_path - 1, "fgd-test-client-%d", getpid());
	if (bind(fd, (struct sockaddr *)&me, sizeof(sa_family_t) + 1 + strlen(me.sun_path + 1)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
#else
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	struct timeval tv = { 3, 0 };
	int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_FGD);

	if (fd < 0)
		return -1;
	/* The kernel unicasts replies to portid == gm->fgd_pid == our pid, so bind to exactly that. */
	sa.nl_pid = (uint32_t)getpid();
	if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		int e = errno;

		close(fd);
		errno = e;
		return -1;
	}
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	return fd;
#endif
}

static int nl_xfer(int fd, uint32_t seq, struct fgd_nl_msg *m, const char *what)
{
	unsigned char tx[NLMSG_HDRLEN + sizeof(struct fgd_nl_msg)];
	unsigned char rx[NLMSG_HDRLEN + sizeof(struct fgd_nl_msg) + 64];
	size_t txlen = build_msg(tx, seq, m);
	struct fgd_nl_msg *r;
	struct nlmsghdr *rh;
	ssize_t n;

#ifdef FGD_TEST_UNIX
	struct sockaddr_un dst = { .sun_family = AF_UNIX };
	const char *sp = getenv("FGD_TEST_SOCK");

	if (!sp)
		sp = "fgd-test-kernel";
	snprintf(dst.sun_path + 1, sizeof dst.sun_path - 1, "%s", sp);
	VLOG("TEST transport: AF_UNIX abstract '%s'", sp);
	hexdump("tx", tx, txlen);
	n = sendto(fd, tx, txlen, 0, (struct sockaddr *)&dst, sizeof(sa_family_t) + 1 + strlen(sp));
	{
		struct pollfd pf = { .fd = fd, .events = POLLIN };

		if (n >= 0 && poll(&pf, 1, 3000) <= 0) {
			errno = EAGAIN;
			n = -1;
		}
	}
#else
	struct sockaddr_nl kern = { .nl_family = AF_NETLINK }; /* pid 0 = kernel */

	hexdump("tx", tx, txlen);
	n = sendto(fd, tx, txlen, 0, (struct sockaddr *)&kern, sizeof kern);
#endif
	if (n < 0) {
		INFO("%s: send failed: %s", what, strerror(errno));
		return -1;
	}
	VLOG("%s: sent %zd bytes (cmd=%u seq=%u)", what, n, m->fgd_cmd, seq);

	/* the kernel handler runs synchronously inside sendto(), so the reply is already queued */
	n = recv(fd, rx, sizeof rx, 0);
	if (n < 0) {
		INFO("%s: no reply from kernel: %s", what, strerror(errno));
		return -1;
	}
	hexdump("rx", rx, (size_t)n);
	if ((size_t)n < NLMSG_HDRLEN + FGD_NL_HDR_LEN) {
		INFO("%s: short reply (%zd bytes)", what, n);
		return -1;
	}
	rh = (struct nlmsghdr *)rx;
	r = (struct fgd_nl_msg *)(rx + NLMSG_HDRLEN);
	if (r->identity != FGD_NL_MAGIC) {
		INFO("%s: reply has wrong magic 0x%08x", what, r->identity);
		return -1;
	}
	if (r->fgd_cmd != m->fgd_cmd || rh->nlmsg_seq != seq) {
		INFO("%s: reply does not match (cmd %u/%u seq %u/%u)", what, r->fgd_cmd, m->fgd_cmd, rh->nlmsg_seq, seq);
		return -1;
	}
	VLOG("%s: kernel acknowledged (reply %zd bytes, data_len=%u)", what, n, r->fgd_data_len);
	return 0;
}

static int sleep_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	return nanosleep(&ts, NULL);
}

static void usage(void)
{
	fputs("usage: fgd-kernel-mode [-n|--dry-run] [-v|--verbose]... [--force] [--wait SEC] [--verify SEC] [--no-verify]\n"
	      "                       [--psy NAME] [--psy-root DIR]\n"
	      "  Hands the MediaTek GM3.0 fuel gauge over to the in-kernel algorithm so battery/capacity becomes valid.\n"
	      "  -n  pre-flight (read-only) + print the netlink messages, send nothing\n"
	      "  --force      run even if the gauge looks initialised (re-initialises the SOC estimate!)\n"
	      "  --wait SEC   wait up to SEC for the power_supply to appear (default 30)\n"
	      "  --verify SEC wait up to SEC for capacity>=0 after the handshake (default 15)\n"
	      "exit: 0 ok/nothing to do, 1 usage, 2 refused by pre-flight, 3 netlink/protocol failure, 4 handshake sent but capacity still -1\n",
	      stderr);
}

int main(int argc, char **argv)
{
	int force = 0, wait_s = 30, verify_s = 15, i, fd, rc;
	struct state st;
	struct fgd_nl_msg m1, m2;
	unsigned char tmp[NLMSG_HDRLEN + sizeof(struct fgd_nl_msg)];
	uint32_t seq = (uint32_t)time(NULL);

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "-n") || !strcmp(a, "--dry-run"))
			dry_run = 1;
		else if (!strcmp(a, "-v") || !strcmp(a, "--verbose"))
			verbose++;
		else if (!strcmp(a, "-vv"))
			verbose += 2;
		else if (!strcmp(a, "--force"))
			force = 1;
		else if (!strcmp(a, "--no-verify"))
			verify_s = 0;
		else if (!strcmp(a, "--wait") && i + 1 < argc)
			wait_s = atoi(argv[++i]);
		else if (!strcmp(a, "--verify") && i + 1 < argc)
			verify_s = atoi(argv[++i]);
		else if (!strcmp(a, "--psy") && i + 1 < argc)
			psy_name = argv[++i];
		else if (!strcmp(a, "--psy-root") && i + 1 < argc)
			psy_root = argv[++i];
		else {
			usage();
			return (!strcmp(a, "-h") || !strcmp(a, "--help")) ? 0 : 1;
		}
	}
	if (strchr(psy_name, '/') || wait_s < 0 || verify_s < 0) {
		usage();
		return 1;
	}

	/* wait for the power_supply (module load ordering) */
	read_state(&st);
	for (i = 0; !st.have_init_done && i < wait_s * 4 && !dry_run; i++) {
		if (i == 0)
			INFO("waiting up to %d s for %s/%s ...", wait_s, psy_root, psy_name);
		sleep_ms(250);
		read_state(&st);
	}

	rc = preflight(&st, force);
	if (rc > 0)
		return 0;
	if (rc < 0 && !dry_run)
		return 2;
	if (rc < 0)
		INFO("dry-run: pre-flight would REFUSE here (continuing only to show the messages)");

	make_set_pid(&m1);
	make_version_control(&m2);
	if (dry_run) {
		INFO("dry-run: would open socket(AF_NETLINK, SOCK_RAW, %d), bind(nl_pid=%d)", NETLINK_FGD, (int)getpid());
		INFO("dry-run: msg1 SET_DAEMON_PID(cmd=%u) pid=%d, ret_len=%u, payload %u bytes",
		     m1.fgd_cmd, (int)getpid(), m1.fgd_ret_data_len, FGD_NL_HDR_LEN + m1.fgd_data_len);
		INFO("dry-run: msg2 SEND_VERSION_CONTROL(cmd=%u) daemon_cmds=0 kernel_cmds=0 custom_data_len=0 custom_table_len=0 "
		     "(kernel expects %d/%d/%d/%d => mismatch => in-kernel algorithm)",
		     m2.fgd_cmd, KERNEL_DAEMON_CMDS, KERNEL_KERNEL_CMDS, KERNEL_CUSTOM_DATA, KERNEL_CUSTOM_TABLE);
		{
			size_t l = build_msg(tmp, seq, &m1);

			hexdump_always("msg1", tmp, l);
			l = build_msg(tmp, seq + 1, &m2);
			hexdump_always("msg2", tmp, l);
		}
		INFO("dry-run: nothing sent, nothing written");
		return 0;
	}

	fd = nl_open();
	if (fd < 0) {
		INFO("cannot open NETLINK_FGD (%d) socket: %s%s", NETLINK_FGD, strerror(errno),
		     errno == EPROTONOSUPPORT ? " - mt6358_battery.ko is not loaded / has no netlink family" : "");
		return 3;
	}
	INFO("handshake: SET_DAEMON_PID pid=%d", (int)getpid());
	if (nl_xfer(fd, seq, &m1, "SET_DAEMON_PID"))
		return 3;
	INFO("handshake: SEND_VERSION_CONTROL (advertising non-matching protocol -> kernel falls back to in-kernel GM3.0)");
	if (nl_xfer(fd, seq + 1, &m2, "SEND_VERSION_CONTROL"))
		return 3;
	close(fd);

	if (verify_s == 0) {
		INFO("handshake sent; verification skipped");
		return 0;
	}
	for (i = 0; i < verify_s * 4; i++) {
		read_state(&st);
		if (st.have_capacity && st.capacity >= 0 && st.capacity <= 100 && st.init_done == 1) {
			INFO("OK: capacity=%ld init_done=1 (in-kernel gauge active)", st.capacity);
			return 0;
		}
		sleep_ms(250);
	}
	INFO("handshake accepted by the kernel but capacity=%ld init_done=%ld after %d s - check dmesg "
	     "(expect 'enable Kernel mode Gauge' and 'MTK Battery algo init')",
	     st.have_capacity ? st.capacity : -999, st.have_init_done ? st.init_done : -999, verify_s);
	return 4;
}
