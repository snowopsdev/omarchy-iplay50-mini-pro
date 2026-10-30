/*
 * mtk-wmt-init - plain-Linux replacement for MediaTek's Android "wmt_loader" plus the mandatory part of
 * "wmt_launcher -p /vendor/firmware/ -o 1", for the MT6789 SoC connsys (chip id 0x6789, "soc2_2", MT6631 A-die).
 *
 *   stage 1 (== wmt_loader):  /dev/wmtdetect ioctls  -> creates /dev/stpwmt, starts the in-kernel WMT threads
 *   stage 2 (== wmt_launcher): /dev/stpwmt: set STP mode, power on connsys (LPBK_POWER_CTRL), then serve the
 *                              kernel's "srh_rom_patch" requests for as long as the process lives (daemon).
 *   subcommand `wifi-nvram`  (== the NVRAM-push part of wlan_assistant): hand the factory Wi-Fi NVRAM to the
 *                              gen4m driver through "WR-BUF:NVRAM" on /dev/wmtWifi.   Optional.
 *
 * Every step is annotated with the evidence it comes from, see docs/05-wifi-bluetooth.md:
 *   [L:addr] = wmt_loader disassembly address,  [W:addr] = wmt_launcher disassembly address,
 *   [A:addr] = wlan_assistant disassembly address, [K:file] = MediaTek kernel-module source (MT6789 tree).
 *
 * Safety: this program never opens a block device, never writes a regular file other than its own state/log
 * files under /run/mtk-wmt, and only reads firmware / NVRAM files.  It talks to /dev/wmtdetect, /dev/stpwmt,
 * /dev/wmtWifi, /dev/kmsg and (fallback) creates missing char device nodes for those names with mknod().
 *
 * Build: see connectivity/build.sh (static, musl, aarch64).   License: GPL-2.0 (ioctl numbers derived from GPL kernel headers).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VERSION "0.4"

/* ---------------------------------------------------------------- ioctl numbers (arm64) */
/* _IOC(dir,type,nr,size) with dir: 1=write 2=read; argument types are int (4) or char* (8). [K:wmt_detect.h, wmt_dev.c] */
#define IOC_(dir, type, nr, size) (((unsigned)(dir) << 30) | ((unsigned)(size) << 16) | ((unsigned)(type) << 8) | (unsigned)(nr))
/* /dev/wmtdetect, magic 'w'.  All are _IOR(...,int) except SET_CHIP_ID (_IOW).   [L:0x1148 = 0x80047700|10 etc.] */
#define COMBO_IOCTL_GET_CHIP_ID        IOC_(2, 'w', 0, 4)
#define COMBO_IOCTL_SET_CHIP_ID        IOC_(1, 'w', 1, 4)   /* [L:0x1630] 0x40047701 */
#define COMBO_IOCTL_EXT_CHIP_DETECT    IOC_(2, 'w', 2, 4)
#define COMBO_IOCTL_GET_SOC_CHIP_ID    IOC_(2, 'w', 3, 4)
#define COMBO_IOCTL_DO_MODULE_INIT     IOC_(2, 'w', 4, 4)
#define COMBO_IOCTL_MODULE_CLEANUP     IOC_(2, 'w', 5, 4)
#define COMBO_IOCTL_EXT_CHIP_PWR_ON    IOC_(2, 'w', 6, 4)
#define COMBO_IOCTL_EXT_CHIP_PWR_OFF   IOC_(2, 'w', 7, 4)
#define COMBO_IOCTL_DO_SDIO_AUDOK      IOC_(2, 'w', 8, 4)
#define COMBO_IOCTL_GET_ADIE_CHIP_ID   IOC_(2, 'w', 9, 4)
#define COMBO_IOCTL_CONNSYS_SOC_HW_INIT IOC_(2, 'w', 10, 4) /* [L:0x114c] 0x8004770a */
/* /dev/stpwmt, magic 0xa0 */
#define WMT_IOCTL_SET_STP_MODE         IOC_(1, 0xa0, 5, 4)   /* [W:0x4df8] 0x4004a005 */
#define WMT_IOCTL_LPBK_POWER_CTRL      IOC_(1, 0xa0, 7, 4)   /* [W:0x652c] 0x4004a007 */
#define WMT_IOCTL_GET_CHIP_INFO        IOC_(2, 0xa0, 12, 4)  /* [W:0x38ec] 0x8004a00c */
#define WMT_IOCTL_SET_LAUNCHER_KILL    IOC_(1, 0xa0, 13, 4)  /* [W:0x4e10] 0x4004a00d */
#define WMT_IOCTL_SET_PATCH_NUM        IOC_(1, 0xa0, 14, 4)  /* [W:0x5f18] 0x4004a00e */
#define WMT_IOCTL_SET_PATCH_INFO       IOC_(1, 0xa0, 15, 8)  /* [W:0x5f78] 0x4008a00f */
#define WMT_IOCTL_WMT_QUERY_CHIPID     IOC_(2, 0xa0, 22, 4)  /* [W:0x45d8] 0x8004a016 */
#define WMT_IOCTL_SET_ROM_PATCH_INFO   IOC_(1, 0xa0, 31, 8)  /* [W:0x6370] 0x4008a01f */

#define CHIPINFO_CHIPID 0
#define CHIPINFO_FWVER  2

/* ---------------------------------------------------------------- globals */
static int g_dry, g_fg, g_verbose = 2, g_kmsg = -1;
static int g_no_vercheck, g_force_loader;
static int g_power_arg = 2;               /* [W:0x64e8] vendor "-o 1" => LPBK_POWER_CTRL(2); no -o => (1) */
static int g_wait_sec = 240;
static unsigned g_chip_override, g_adie_override, g_dry_fwver = 0x10000; /* 0x10000 => "unknown" in dry-run */
static const char *g_fwdir = "/vendor/firmware";   /* [W:0x3a70] hard-coded "/vendor/firmware" for rom patches; -p for srh_patch */
static const char *g_devdir = "/dev";
static const char *g_state = "/run/mtk-wmt";
static const char *g_logfile = NULL;
static const char *g_pattern_override = NULL;
static const char *g_dry_cmd = NULL;
static volatile sig_atomic_t g_stop;
static int g_stpwmt = -1;
static int g_status_pipe = -1;
static unsigned g_chip = 0, g_adie = 0;
static struct timespec g_t0;

/* ---------------------------------------------------------------- logging (stderr + /dev/kmsg) */
static void logv(int lvl, const char *fmt, va_list ap)
{
	char msg[900], line[1000];
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	vsnprintf(msg, sizeof msg, fmt, ap);
	long ms = (t.tv_sec - g_t0.tv_sec) * 1000L + (t.tv_nsec - g_t0.tv_nsec) / 1000000L;   /* integer math: no soft-float in the static link */
	int n = snprintf(line, sizeof line, "mtk-wmt-init[%d] %4ld.%03ld: %s%s\n", (int)getpid(), ms / 1000, ms % 1000, lvl == 3 ? "ERROR: " : "", msg);
	if (n > 0)
		(void)!write(2, line, n < (int)sizeof line ? n : (int)sizeof line - 1);
	if (g_kmsg >= 0) {
		n = snprintf(line, sizeof line, "<%d>mtk-wmt-init: %s%s\n", lvl == 3 ? 3 : 6, lvl == 3 ? "ERROR: " : "", msg);
		if (n > 0)
			(void)!write(g_kmsg, line, n < (int)sizeof line ? n : (int)sizeof line - 1);
	}
}
static void logm(const char *fmt, ...) { va_list ap; va_start(ap, fmt); logv(6, fmt, ap); va_end(ap); }
static void loge(const char *fmt, ...) { va_list ap; va_start(ap, fmt); logv(3, fmt, ap); va_end(ap); }
static void logd(const char *fmt, ...) { if (g_verbose < 2) return; va_list ap; va_start(ap, fmt); logv(6, fmt, ap); va_end(ap); }

/* ---------------------------------------------------------------- helpers */
static struct { unsigned code; const char *name; } ioctl_names[] = {
	{ COMBO_IOCTL_GET_CHIP_ID, "COMBO_IOCTL_GET_CHIP_ID" }, { COMBO_IOCTL_SET_CHIP_ID, "COMBO_IOCTL_SET_CHIP_ID" },
	{ COMBO_IOCTL_EXT_CHIP_DETECT, "COMBO_IOCTL_EXT_CHIP_DETECT" }, { COMBO_IOCTL_GET_SOC_CHIP_ID, "COMBO_IOCTL_GET_SOC_CHIP_ID" },
	{ COMBO_IOCTL_DO_MODULE_INIT, "COMBO_IOCTL_DO_MODULE_INIT" }, { COMBO_IOCTL_MODULE_CLEANUP, "COMBO_IOCTL_MODULE_CLEANUP" },
	{ COMBO_IOCTL_EXT_CHIP_PWR_ON, "COMBO_IOCTL_EXT_CHIP_PWR_ON" }, { COMBO_IOCTL_EXT_CHIP_PWR_OFF, "COMBO_IOCTL_EXT_CHIP_PWR_OFF" },
	{ COMBO_IOCTL_GET_ADIE_CHIP_ID, "COMBO_IOCTL_GET_ADIE_CHIP_ID" }, { COMBO_IOCTL_CONNSYS_SOC_HW_INIT, "COMBO_IOCTL_CONNSYS_SOC_HW_INIT" },
	{ WMT_IOCTL_SET_STP_MODE, "WMT_IOCTL_SET_STP_MODE" }, { WMT_IOCTL_LPBK_POWER_CTRL, "WMT_IOCTL_LPBK_POWER_CTRL" },
	{ WMT_IOCTL_GET_CHIP_INFO, "WMT_IOCTL_GET_CHIP_INFO" }, { WMT_IOCTL_SET_LAUNCHER_KILL, "WMT_IOCTL_SET_LAUNCHER_KILL" },
	{ WMT_IOCTL_SET_PATCH_NUM, "WMT_IOCTL_SET_PATCH_NUM" }, { WMT_IOCTL_SET_PATCH_INFO, "WMT_IOCTL_SET_PATCH_INFO" },
	{ WMT_IOCTL_WMT_QUERY_CHIPID, "WMT_IOCTL_WMT_QUERY_CHIPID" }, { WMT_IOCTL_SET_ROM_PATCH_INFO, "WMT_IOCTL_SET_ROM_PATCH_INFO" },
};
static const char *ioctl_name(unsigned c)
{
	for (unsigned i = 0; i < sizeof ioctl_names / sizeof *ioctl_names; i++)
		if (ioctl_names[i].code == c)
			return ioctl_names[i].name;
	return "?";
}

/* Canned replies for --dry-run so the whole sequence can be exercised without hardware. */
static int dry_reply(unsigned code, unsigned long arg)
{
	switch (code) {
	case COMBO_IOCTL_EXT_CHIP_PWR_ON: return -1;                /* "no external combo chip": SoC path */
	case COMBO_IOCTL_GET_SOC_CHIP_ID: case WMT_IOCTL_WMT_QUERY_CHIPID: return g_chip_override ? (int)g_chip_override : 0x6789;
	case COMBO_IOCTL_GET_ADIE_CHIP_ID: return g_adie_override ? (int)g_adie_override : 0x6631;
	case WMT_IOCTL_GET_CHIP_INFO:
		if (arg == CHIPINFO_CHIPID) return g_chip_override ? (int)g_chip_override : 0x6789;
		if (arg == CHIPINFO_FWVER) return (int)g_dry_fwver;
		return 0;
	default: return 0;
	}
}

/* ioctl with name/result logging.  Arguments are passed by value (all ioctls here take ints) except the two patch-info structs. */
static int xioctl(int fd, unsigned code, unsigned long arg)
{
	int r, e = 0;
	if (g_dry) {
		r = dry_reply(code, arg);
		logd("DRY-RUN ioctl(fd=%d, %s 0x%08x, 0x%lx) -> %d (not executed)", fd, ioctl_name(code), code, arg, r);
		return r;
	}
	errno = 0;
	r = ioctl(fd, code, arg);
	e = errno;
	if (r < 0)
		logd("ioctl(fd=%d, %s 0x%08x, 0x%lx) -> %d errno=%d (%s)", fd, ioctl_name(code), code, arg, r, e, strerror(e));
	else
		logd("ioctl(fd=%d, %s 0x%08x, 0x%lx) -> %d (0x%x)", fd, ioctl_name(code), code, arg, r, (unsigned)r);
	errno = e;
	return r;
}

static void msleep(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, NULL); }

static int is_known_node(const char *name)
{
	return !strcmp(name, "wmtdetect") || !strcmp(name, "stpwmt") || !strcmp(name, "wmtWifi") || !strcmp(name, "stpbt");
}

/* Fallback when /dev is not a devtmpfs that auto-creates nodes: find "<major>:<minor>" in /sys/class/<cls>/<name>/dev. */
static int ensure_node(const char *name)
{
	char path[300], sp[300], buf[64];
	struct stat st;
	unsigned maj, min;
	snprintf(path, sizeof path, "%s/%s", g_devdir, name);
	if (stat(path, &st) == 0) {
		if (S_ISCHR(st.st_mode))
			return 0;
		loge("%s exists but is not a character device (mode 0%o)", path, st.st_mode);
		return -1;
	}
	if (!is_known_node(name))
		return -1;
	DIR *d = opendir("/sys/class");
	if (!d)
		return -1;
	struct dirent *de;
	int found = 0;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.')
			continue;
		snprintf(sp, sizeof sp, "/sys/class/%s/%s/dev", de->d_name, name);
		int fd = open(sp, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		ssize_t n = read(fd, buf, sizeof buf - 1);
		close(fd);
		if (n > 0) {
			buf[n] = 0;
			char *colon = strchr(buf, ':');
			if (colon) {
				maj = strtoul(buf, NULL, 10);
				min = strtoul(colon + 1, NULL, 10);
				found = 1;
				logm("node %s missing: sysfs %s says %u:%u -> mknod (devtmpfs not auto-populating?)", path, sp, maj, min);
				break;
			}
		}
	}
	closedir(d);
	if (!found)
		return -1;
	if (g_dry) {
		logm("DRY-RUN would mknod %s c %u %u", path, maj, min);
		return 0;
	}
	if (mknod(path, S_IFCHR | 0660, makedev(maj, min)) < 0 && errno != EEXIST) {
		loge("mknod %s failed: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

/* open a device node, retrying (the vendor tools retry every 300 ms forever; we give up after tries*300 ms).
 * NOTE: the vendor binaries open with O_CREAT (0x102); we deliberately do NOT, so a missing node can never turn into a regular file. */
static int open_node(const char *name, int flags, int tries)
{
	char path[300];
	snprintf(path, sizeof path, "%s/%s", g_devdir, name);
	if (g_dry) {
		logm("DRY-RUN would open %s (flags 0x%x)", path, flags);
		return 1000;
	}
	for (int i = 0; i < tries; i++) {
		ensure_node(name);
		int fd = open(path, flags | O_CLOEXEC | O_NOCTTY);
		if (fd >= 0) {
			logm("open(%s) ok fd=%d (attempt %d)", path, fd, i + 1);
			return fd;
		}
		if (i == 0 || i % 10 == 9)
			logm("can't open %s: %s (attempt %d/%d)", path, strerror(errno), i + 1, tries);
		msleep(300);
	}
	return -1;
}

static void state_put(const char *name, const char *val)
{
	char p[300];
	if (g_dry) {
		logm("DRY-RUN would set state %s=%s", name, val);
		return;
	}
	mkdir(g_state, 0755);
	snprintf(p, sizeof p, "%s/%s", g_state, name);
	FILE *f = fopen(p, "w");
	if (f) {
		fprintf(f, "%s\n", val);
		fclose(f);
	} else {
		loge("cannot write %s: %s", p, strerror(errno));
	}
}
static int state_get(const char *name, char *out, size_t n)
{
	char p[300];
	snprintf(p, sizeof p, "%s/%s", g_state, name);
	FILE *f = fopen(p, "r");
	if (!f)
		return -1;
	if (!fgets(out, n, f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	out[strcspn(out, "\n")] = 0;
	return 0;
}

static void report(char c)
{
	if (g_status_pipe >= 0) {
		(void)!write(g_status_pipe, &c, 1);
		close(g_status_pipe);
		g_status_pipe = -1;
	}
}

/* ---------------------------------------------------------------- stage 1: wmt_loader */
/*
 * Sequence, from wmt_loader main() [L:0x1054-0x19e4]:
 *   (property vendor.connsys.driver.ready == "yes" -> "WMT driver has been ready." exit)         [L:0x10c0-0x10cc]
 *   open /dev/wmtdetect, retry every 300 ms                                                      [L:0x10d8-0x1140]
 *   ioctl 0x8004770a CONNSYS_SOC_HW_INIT -> must be 0                                            [L:0x1158]
 *   (property persist.vendor.connsys.chipid: if a known id, skip detection; empty on first boot)  [L:0x11a4]
 *   loop (max 20 x, 500 ms apart): EXT_CHIP_PWR_ON(0x80047706) == -1 => "SOC chip no need do combo chip power on"
 *        => GET_SOC_CHIP_ID (0x80047703) -> chip id (0x6789); not -1 => leave loop                 [L:0x1254-0x1330]
 *   SET_CHIP_ID(0x40047701, id); legacy-id remap (identity for 0x6789)                           [L:0x1630-0x16fc]
 *   MODULE_CLEANUP(0x80047705, id)==0 ; DO_MODULE_INIT(0x80047704, id)==0 -> kernel: WMT_init()  [L:0x1704-0x1788]
 *   GET_ADIE_CHIP_ID(0x80047709) != -1 -> property vendor.connsys.adie.chipid="0x%04x"           [L:0x17a0-0x17f8]
 *   property vendor.connsys.driver.ready=yes                                                     [L:0x19a4-0x19b4]
 */
static int stage_loader(void)
{
	char v[64];
	if (!g_dry && !g_force_loader && state_get("driver.ready", v, sizeof v) == 0 && !strcmp(v, "yes")) {
		logm("loader: WMT driver has been ready (state file %s/driver.ready); skipping loader stage (use --force-loader to redo)", g_state);
		if (state_get("chipid", v, sizeof v) == 0) g_chip = strtoul(v, NULL, 16);
		if (state_get("adie.chipid", v, sizeof v) == 0) g_adie = strtoul(v, NULL, 16);
		return 0;
	}
	int fd = open_node("wmtdetect", O_RDWR, 100);
	if (fd < 0) {
		loge("loader: cannot open %s/wmtdetect (is wmt_drv.ko loaded?)", g_devdir);
		return -1;
	}
	int r = xioctl(fd, COMBO_IOCTL_CONNSYS_SOC_HW_INIT, 0);
	if (r != 0) {
		loge("loader: COMBO_IOCTL_CONNSYS_SOC_HW_INIT fail !!(0x%x) errno=%d (%s)", r, errno, strerror(errno));
		return -1;
	}
	int chip = -1;
	for (int i = 0; i < 20; i++) {
		r = xioctl(fd, COMBO_IOCTL_EXT_CHIP_PWR_ON, 0);
		if (r == -1) {
			logm("loader: SOC chip no need do combo chip power on.");
			chip = xioctl(fd, COMBO_IOCTL_GET_SOC_CHIP_ID, 0);
		} else if (r == 0) {
			loge("loader: an external combo chip answered (EXT_CHIP_PWR_ON==0): not an SoC-connsys device, not supported here");
			return -1;
		} else {
			loge("loader: external combo power on failed (%d)", r);
		}
		logm("loader: chipid (0x%x) detected", chip);
		if (chip != -1)
			break;
		logm("loader: chipId detect failed, retrying, left retryCounter:%d", 18 - i);
		msleep(500);
	}
	if (chip == -1) {
		loge("loader: chip id error !!(0x%x)", chip);
		return -1;
	}
	if (g_chip_override && g_chip_override != (unsigned)chip)
		logm("loader: NOTE chip id override 0x%x ignored for ioctls (kernel says 0x%x)", g_chip_override, chip);
	xioctl(fd, COMBO_IOCTL_SET_CHIP_ID, (unsigned long)chip);   /* vendor ignores the result and remaps legacy ids; 0x6789 is passed through */
	r = xioctl(fd, COMBO_IOCTL_MODULE_CLEANUP, (unsigned long)chip);
	if (r != 0) {
		loge("loader: do WMT-DETECT module cleanup failed: %d", r);
		/* vendor continues anyway (it only logs) [L:0x1714-0x178c] */
	}
	r = xioctl(fd, COMBO_IOCTL_DO_MODULE_INIT, (unsigned long)chip);
	if (r != 0) {
		loge("loader: do kernel module init failed: %d errno=%d (%s) -- is /vendor/firmware mounted? WMT_init reads WMT_SOC.cfg", r, errno, strerror(errno));
		/* vendor continues and still sets driver.ready; we stop here because nothing below can work */
		return -1;
	}
	logm("loader: do kernel module init succeed: %d", r);
	int adie = xioctl(fd, COMBO_IOCTL_GET_ADIE_CHIP_ID, 0);
	logm("loader: adie chip id = 0x%x", adie);
	if (adie == -1) {
		loge("loader: A-die chip id error !!");
		return -1;
	}
	g_chip = chip;
	g_adie = adie;
	if (!g_dry)
		close(fd);
	snprintf(v, sizeof v, "0x%04x", g_chip);
	state_put("chipid", v);                 /* property persist.vendor.connsys.chipid */
	snprintf(v, sizeof v, "0x%04x", g_adie);
	state_put("adie.chipid", v);            /* property vendor.connsys.adie.chipid */
	state_put("driver.ready", "yes");       /* property vendor.connsys.driver.ready */
	logm("loader: done: vendor.connsys.driver.ready=yes (chip 0x%04x, a-die 0x%04x)", g_chip, g_adie);
	/* gen4m's init_module() copies wmt_drv's gConEmiPhyBase/Size into gConEmiPhyBaseFinal/SizeFinal exactly once; wmt_drv fills them in
	 * mtk_wmt_probe(), which only runs from the SOC_HW_INIT ioctl above (mtk_wcn_consys_hw_init -> platform_driver_register).  A gen4m that was
	 * insmod'ed before this point has base 0 and later fails with "wlanDownloadEMISection: gConEmiPhyBaseFinal invalid".  [NOTES.md section 3b] */
	if (!g_dry) {
		FILE *pm = fopen("/proc/modules", "r");
		char ln[256];
		while (pm && fgets(ln, sizeof ln, pm)) {
			if (!strncmp(ln, "wlan_drv_gen4m_6789 ", 20)) {
				logm("loader: WARNING wlan_drv_gen4m_6789 was loaded BEFORE the loader stage: its gConEmiPhyBaseFinal is 0. "
				     "rmmod wlan_drv_gen4m_6789 wmt_chrdev_wifi and insmod them again now (wifi-up.sh does this automatically)");
				state_put("wlan.stale", "yes");
				break;
			}
		}
		if (pm) fclose(pm);
	}
	return 0;
}

/* ---------------------------------------------------------------- stage 2: wmt_launcher */

/* chip id -> firmware prefix.  [W:0x38e8-0x39b4] (soc2_0: 6779,6853,6873; soc2_2: 6781,6789,6833,6855; else soc1_0) */
static const char *soc_prefix(unsigned chip)
{
	switch (chip) {
	case 0x6779: case 0x6853: case 0x6873: return "soc2_0";
	case 0x6781: case 0x6789: case 0x6833: case 0x6855: return "soc2_2";
	default: return "soc1_0";
	}
}

/* A-die id -> firmware "pattern" substring.  launcher_get_patch_pattern [W:0x596c-0x5da8], chip 0x6789:
 *   vendor.connsys.adie.chipid == "0x6631" -> "1d_1" [W:0x5a58] ; == "0x6635" -> "1e_1" [W:0x5cfc] ; else "Unknown A-DIE chipid" */
static const char *soc_pattern(unsigned chip, unsigned adie)
{
	if (g_pattern_override)
		return g_pattern_override;
	if (chip == 0x6789) {
		if (adie == 0x6631) return "1d_1";
		if (adie == 0x6635) return "1e_1";
		return NULL;
	}
	/* other chips are not needed on this tablet (the vendor binary has more tables); require --pattern */
	return NULL;
}

struct rom_patch_info {         /* struct wmt_rom_patch_info, [K:wmt_lib.h:175]; 264 bytes */
	uint32_t type;
	uint8_t addr[4];
	char name[256];
};
struct patch_info {             /* WMT_PATCH_INFO [K:wmt_lib.h:170] */
	uint32_t seq;
	uint8_t addr[4];
	char name[256];
};

static int cmpstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int list_matching(const char *prefix, const char *pattern, const char *dir, char ***out)
{
	DIR *d = opendir(dir);
	if (!d) {
		loge("patch path %s cannot be opened: %s", dir, strerror(errno));
		return -1;
	}
	char **v = NULL;
	int n = 0;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (strncmp(de->d_name, prefix, strlen(prefix)))
			continue;
		if (!strstr(de->d_name, pattern))
			continue;
		v = realloc(v, (n + 1) * sizeof *v);
		v[n++] = strdup(de->d_name);
	}
	closedir(d);
	if (n > 1)
		qsort(v, n, sizeof *v, cmpstr);
	*out = v;
	return n;
}
static void free_list(char **v, int n) { for (int i = 0; i < n; i++) free(v[i]); free(v); }

/* Read patch header fields the vendor launcher uses.
 *   byte @0x16/0x17 : version bytes; launcher_get_patch_version reads byte 0x16 into buf[1] and byte 0x17 into buf[0];
 *                     the file is used only if buf[0] (== byte @0x17) == (fw_version & 0xff)     [W:0x3ea0-0x3f20]
 *   bytes @0x18..0x1f: "patch info": info[1..3] = EMI address bytes (info[0] is a flag byte and is zeroed when passed on),
 *                     info[3] & 0xf0 must be non-zero and info[7] <= 5 is the ROM-patch type (BT=0,WIFI=3,MCU=4)   [W:0x3f24-0x3f50,0x6324]
 */
static int read_hdr(const char *path, uint8_t *v16, uint8_t *v17, uint8_t info[8])
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		loge("open patch file(%s) failed: %s", path, strerror(errno));
		return -1;
	}
	uint8_t h[0x20];
	ssize_t n = pread(fd, h, sizeof h, 0);
	close(fd);
	if (n != (ssize_t)sizeof h) {
		loge("patch file(%s) too short (%zd bytes)", path, n);
		return -1;
	}
	*v16 = h[0x16];
	*v17 = h[0x17];
	memcpy(info, h + 0x18, 8);
	return 0;
}

static int cmd_srh_rom_patch(void)
{
	/* [W:0x3854 cmd_hdr_sch_rom_patch] */
	int chip = xioctl(g_stpwmt, WMT_IOCTL_GET_CHIP_INFO, CHIPINFO_CHIPID);
	if (chip <= 0) { loge("srh_rom_patch: invalid chip id %d", chip); return -1; }
	const char *sp = soc_prefix(chip);
	char prefix[32];
	snprintf(prefix, sizeof prefix, "%s_ram", sp);                 /* strncpy(prefix,"soc2_2",6) + strncat("_ram") [W:0x39c0-0x39e4] */
	const char *pattern = soc_pattern(chip, g_adie);
	logm("srh_rom_patch: chip 0x%x prefix \"%s\" a-die 0x%x pattern \"%s\"", chip, prefix, g_adie, pattern ? pattern : "(none)");
	if (!pattern) { loge("srh_rom_patch: Unknown A-DIE chipid 0x%x (use --pattern 1d_1|1e_1)", g_adie); return -10; }
	int fwv = xioctl(g_stpwmt, WMT_IOCTL_GET_CHIP_INFO, CHIPINFO_FWVER);   /* [W:0x3a40] arg 2 */
	logm("srh_rom_patch: fw_version:0x%04x (compared with patch header byte @0x17)", fwv & 0xffff);
	char **names;
	int n = list_matching(prefix, pattern, g_fwdir, &names);
	if (n < 0) return -3;                                              /* "patch path cannot be opened" */
	int ok = 0, rc = 0;
	for (int i = 0; i < n; i++) {
		char path[512];
		uint8_t v16, v17, info[8];
		snprintf(path, sizeof path, "%s/%s", g_fwdir, names[i]);
		if (read_hdr(path, &v16, &v17, info) < 0) continue;
		logm("srh_rom_patch: %s: fw Ver in patch: 0x%02x%02x (byte@0x17=0x%02x), driver fw_version 0x%04x", names[i], v17, v16, v17, fwv & 0xffff);
		if (!g_no_vercheck && !(g_dry && g_dry_fwver == 0x10000) && v17 != (fwv & 0xff)) {
			logm("srh_rom_patch: %s skipped: patch version byte 0x%02x != fw_version low byte 0x%02x", names[i], v17, fwv & 0xff);
			continue;
		}
		if ((info[3] & 0xf0) == 0) { logm("srh_rom_patch: %s skipped: patch info[3]=0x%02x has no high nibble", names[i], info[3]); continue; }
		if (info[7] > 5) { logm("srh_rom_patch: %s skipped: patch info[7]=%u > 5", names[i], info[7]); continue; }
		logm("srh_rom_patch: patch info:0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x  rom patch type = [%u]",
		     info[0], info[1], info[2], info[3], info[4], info[5], info[6], info[7], info[7]);
		struct rom_patch_info ri;                                      /* launcher_set_rom_patch_info [W:0x62a4] */
		memset(&ri, 0, sizeof ri);
		ri.type = info[7];
		ri.addr[0] = 0;                                                /* low byte is a sequence/flag byte: zeroed [W:0x6354 strb wzr] */
		ri.addr[1] = info[1]; ri.addr[2] = info[2]; ri.addr[3] = info[3];
		strncpy(ri.name, names[i], sizeof ri.name - 1);                /* bare file name; the kernel does request_firmware(name) */
		/* kernel computes patchEmiOffset = addr[2]<<16 | addr[1]<<8 | addr[0]; addr[3] (0xf0) is ignored [K:wmt_ic_soc.c:3600] */
		logm("srh_rom_patch: SET_ROM_PATCH_INFO type=%u name=%s addr bytes 00,%02x,%02x,%02x (EMI offset 0x%x)",
		     ri.type, ri.name, ri.addr[1], ri.addr[2], ri.addr[3], (ri.addr[2] << 16) | (ri.addr[1] << 8));
		if (g_dry) {
			logm("DRY-RUN ioctl(fd=%d, WMT_IOCTL_SET_ROM_PATCH_INFO 0x%08x, <%zu-byte struct>) not executed", g_stpwmt, WMT_IOCTL_SET_ROM_PATCH_INFO, sizeof ri);
			ok++;
			continue;
		}
		if (xioctl(g_stpwmt, WMT_IOCTL_SET_ROM_PATCH_INFO, (unsigned long)&ri) != 0) {
			loge("ioctl WMT_IOCTL_SET_ROM_PATCH_INFO error (errno %d)", errno);
			rc = -3;
			break;
		}
		ok++;
	}
	free_list(names, n);
	if (!rc && ok == 0) {
		/* The vendor launcher would answer "ok" here; the kernel then logs "NULL patchinfo pointer".  We fail loudly instead. */
		loge("srh_rom_patch: no usable %s*%s* file in %s (checked %d candidates)", prefix, pattern, g_fwdir, n);
		rc = -6;
	}
	return rc;
}

static int cmd_srh_patch(void)
{
	/* Legacy multi-patch request [W:0x3054 cmd_hdr_sch_patch / launcher_set_patch_info 0x5e6c].  The kernel only sends this when the chip
	 * table does not set OPT_DISABLE_ROM_PATCH_DWN; unverified for 0x6789.  Same selection rules, prefix "<soc>_patch". */
	int chip = xioctl(g_stpwmt, WMT_IOCTL_GET_CHIP_INFO, CHIPINFO_CHIPID);
	if (chip <= 0) return -1;
	char prefix[32];
	snprintf(prefix, sizeof prefix, "%s_patch", soc_prefix(chip));
	const char *pattern = soc_pattern(chip, g_adie);
	if (!pattern) return -10;
	int fwv = xioctl(g_stpwmt, WMT_IOCTL_GET_CHIP_INFO, CHIPINFO_FWVER);
	char **names;
	int n = list_matching(prefix, pattern, g_fwdir, &names);
	if (n < 0) return -3;
	int ok = 0, rc = 0;
	for (int i = 0; i < n; i++) {
		char path[512];
		uint8_t v16, v17, info[8];
		snprintf(path, sizeof path, "%s/%s", g_fwdir, names[i]);
		if (read_hdr(path, &v16, &v17, info) < 0) continue;
		if (!g_no_vercheck && !(g_dry && g_dry_fwver == 0x10000) && v17 != (fwv & 0xff)) continue;
		struct patch_info pi;
		memset(&pi, 0, sizeof pi);
		if (ok == 0) {
			logm("srh_patch: patch num = [%d]", info[0] >> 4);
			if (xioctl(g_stpwmt, WMT_IOCTL_SET_PATCH_NUM, info[0] >> 4) != 0 && !g_dry) { rc = -6; break; }
		}
		pi.seq = info[0] & 0xf;
		pi.addr[1] = info[1]; pi.addr[2] = info[2]; pi.addr[3] = info[3];
		strncpy(pi.name, names[i], sizeof pi.name - 1);
		logm("srh_patch: patch seq = [%u] name=%s", pi.seq, pi.name);
		if (!g_dry && xioctl(g_stpwmt, WMT_IOCTL_SET_PATCH_INFO, (unsigned long)&pi) != 0) { rc = -6; break; }
		ok++;
	}
	free_list(names, n);
	if (!rc && !ok) rc = -6;
	return rc;
}

/* dispatch: vendor table = {"srh_patch", "srh_rom_patch"} [W:0x86e0-0x86f8]; reply "ok" / "cmd not found" / "resp_%d" [W:0x5330-0x53e8] */
static void handle_cmd(const char *cmd)
{
	char resp[64];
	int r;
	if (!strcmp(cmd, "srh_patch")) r = cmd_srh_patch();
	else if (!strcmp(cmd, "srh_rom_patch")) r = cmd_srh_rom_patch();
	else if (!strcmp(cmd, "update_patch_version")) {
		/* Kernel sends this after a patch-version change [K:wmt_ctrl.c:723]; the vendor launcher does not know it ("cmd not found") and
		 * the kernel ignores the result [K:wmt_ic_soc.c:1243-1246].  We answer ok to keep dmesg quieter. */
		r = 0;
	} else r = 1;
	if (r == 0) snprintf(resp, sizeof resp, "ok");
	else if (r == 1) snprintf(resp, sizeof resp, "cmd not found");
	else snprintf(resp, sizeof resp, "resp_%d", r);
	logm("cmd(%s) resp(%s)", cmd, resp);
	if (g_dry) { logm("DRY-RUN would write(fd=%d, \"%s\")", g_stpwmt, resp); return; }
	ssize_t w = write(g_stpwmt, resp, strlen(resp));
	if (w != (ssize_t)strlen(resp))
		loge("write resp(%d) fail: len(%zd), errno(%d, %s)", g_stpwmt, w, errno, strerror(errno));
}

static void *pwr_on_thread(void *arg)
{
	(void)arg;
	pthread_setname_np(pthread_self(), "pwr_on_conn");                        /* [W:0x64f8] */
	logm("enter power on connsys flow (LPBK_POWER_CTRL arg %d)", g_power_arg);
	int ok = 0;
	for (int i = 0; i < 20; i++) {                                           /* [W:0x6528-0x657c] 20 tries, 1 s apart */
		if (xioctl(g_stpwmt, WMT_IOCTL_LPBK_POWER_CTRL, g_power_arg) == 0) { ok = 1; break; }
		xioctl(g_stpwmt, WMT_IOCTL_LPBK_POWER_CTRL, 0);
		logm("power on %x failed, retrying, retry counter:%d (errno %d %s)", g_chip, i, errno, strerror(errno));
		sleep(1);
	}
	state_put("launcher.ready", ok ? "yes" : "no");
	if (ok) logm("power on connsys: SUCCESS (connsys is up, firmware downloaded)");
	else loge("power on connsys: FAILED after 20 tries -- see kernel log (dmesg | grep -E 'WMT|STP|BTIF|CONSYS')");
	report(ok ? 'K' : 'F');
	return NULL;
}

static void on_term(int sig) { (void)sig; g_stop = 1; }

static int stage_launcher(void)
{
	int fd = open_node("stpwmt", O_RDWR, 100);                                 /* [W:0x44a4-0x4514] */
	if (fd < 0) { loge("launcher: cannot open %s/stpwmt (loader stage not done / DO_MODULE_INIT failed?)", g_devdir); return -1; }
	g_stpwmt = fd;
	int chip = xioctl(fd, WMT_IOCTL_WMT_QUERY_CHIPID, 0);                      /* [W:0x45d4-0x4604] retried every 300 ms until != -1 */
	for (int i = 0; chip == -1 && i < 50; i++) { msleep(300); chip = xioctl(fd, WMT_IOCTL_WMT_QUERY_CHIPID, 0); }
	logm("launcher: chip_id from kernel by ioctl:0x%04x", chip);
	if (chip <= 0) { loge("launcher: invalid chip id(%d)", chip); return -1; }
	if (g_chip && (unsigned)chip != g_chip) logm("launcher: WARNING chip id mismatch loader 0x%x vs kernel 0x%x", g_chip, chip);
	g_chip = chip;
	/* combo chips (0x6620/0x6628/0x6630/0x6632) would need TELL_CHIPID + CFG_NAME + UART setup: not this device [W:0x4658-0x4670, 0x4880] */
	/* SoC branch [W:0x4de0-0x4e14]: SET_STP_MODE( (baud<<8) | (fm_mode<<4) | stp_mode ), defaults stp=3 (BTIF), fm=2, baud=0 [W:0x14a0] */
	unsigned stp = 3, fm = 2, baud = 0;
	xioctl(fd, WMT_IOCTL_SET_STP_MODE, (baud << 8) | (fm << 4) | stp);        /* 0x23 */
	xioctl(fd, WMT_IOCTL_SET_LAUNCHER_KILL, 0);
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_IGN;                                                   /* SIGCHLD, SIGPIPE ignored [W:0x4e34-0x4e50] */
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);                                              /* vendor only logs sig_hup */
	sa.sa_handler = on_term;                                                   /* SIGTERM/SIGINT -> flag + SET_LAUNCHER_KILL(1) [W:0x640c] */
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	state_put("formeta.ready", "yes");                                         /* property vendor.connsys.formeta.ready */

	pthread_t th;
	if (g_power_arg < 0) {
		logm("launcher: --no-poweron: not powering connsys now; the first `echo 1 > /dev/wmtWifi` (or open of /dev/stpbt) powers it while we serve patch requests");
		report('K');
	} else if (g_dry) {
		pwr_on_thread(NULL);
	} else if (pthread_create(&th, NULL, pwr_on_thread, NULL) != 0) {
		loge("create pwr on thread fail");
		return -1;
	} else {
		pthread_detach(th);
		logm("create pwr on thread ok");
	}

	if (g_dry) {
		const char *c = g_dry_cmd ? g_dry_cmd : "srh_rom_patch";
		logm("DRY-RUN simulating a kernel request: \"%s\"", c);
		handle_cmd(c);
		return 0;
	}

	/* main loop: poll(POLLIN|POLLRDNORM) / read(<=255) / compare / write reply [W:0x51f0-0x53e8] */
	struct pollfd p = { .fd = fd, .events = POLLIN | POLLRDNORM };
	while (!g_stop) {
		p.revents = 0;
		int r = poll(&p, 1, 1000);
		if (r < 0) { if (errno == EINTR) continue; loge("poll error:%d errno:%d, %s", r, errno, strerror(errno)); break; }
		if (r == 0) continue;
		if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { loge("poll revents 0x%x on /dev/stpwmt, giving up", p.revents); break; }
		if (!(p.revents & POLLIN)) continue;
		char buf[256];
		ssize_t n = read(fd, buf, 255);
		if (n < 1) { logm("POLLIN(%d) but read fail:%zd", fd, n); msleep(100); continue; }
		buf[n] = 0;
		handle_cmd(buf);
	}
	logm("launcher: stopping (signal=%d): telling the driver the launcher is gone", (int)g_stop);
	xioctl(fd, WMT_IOCTL_SET_LAUNCHER_KILL, 1);
	state_put("launcher.ready", "no");
	return 0;
}

/* ---------------------------------------------------------------- subcommand: wifi-nvram */
/* [A:0x165c write_nvram, 0x1240 write_data_to_driver, 0x1c4c get_custom_nvram_file_name]:
 *   payload = "WR-BUF:NVRAM" + first (st_size-2) bytes of /mnt/vendor/nvdata/APCFG/APRDEB/WIFI, written with ONE write() to /dev/wmtWifi.
 *   The vendor accepts the file only if (st_size-2) is a positive multiple of 256.  (MAC override from /data/vendor/macwifi not implemented.) */
static int cmd_wifi_nvram(const char *file, int force)
{
	struct stat st;
	if (stat(file, &st) < 0) { loge("wifi-nvram: stat %s error: %s", file, strerror(errno)); return 1; }
	long len = (long)st.st_size - 2;
	logm("wifi-nvram: %s size %lld -> nvram size = %ld", file, (long long)st.st_size, len);
	if (len < 1 || (len & 0xff)) {
		if (!force) { loge("wifi-nvram: invalid nvram size %ld (vendor requires a positive multiple of 256); use --force to push anyway", len); return 1; }
		logm("wifi-nvram: --force: pushing despite odd size");
		if (len < 1) return 1;
	}
	if (len > 8192) { loge("wifi-nvram: refusing %ld bytes (> 8192)", len); return 1; }
	char *buf = calloc(1, 12 + len);
	if (!buf) return 1;
	memcpy(buf, "WR-BUF:NVRAM", 12);
	int fd = open(file, O_RDONLY | O_CLOEXEC);
	if (fd < 0) { loge("wifi-nvram: Error opening nvram file: %s", strerror(errno)); return 1; }
	ssize_t n = read(fd, buf + 12, len);
	close(fd);
	if (n != len) { loge("wifi-nvram: short read %zd/%ld", n, len); return 1; }
	if (g_dry) { logm("DRY-RUN would write %ld bytes (\"WR-BUF:NVRAM\" + %ld) to %s/wmtWifi", 12 + len, len, g_devdir); return 0; }
	int w = open_node("wmtWifi", O_WRONLY, 20);
	if (w < 0) { loge("wifi-nvram: can't open device node wmtWifi"); return 1; }
	ssize_t r = write(w, buf, 12 + len);
	int e = errno;
	close(w);
	if (r != 12 + len) { loge("wifi-nvram: write to driver failed: ret %zd errno %d (%s)", r, e, strerror(e)); return 1; }
	logm("wifi-nvram: wrote %zd bytes to /dev/wmtWifi (driver accepted NVRAM; vendor.mtk.nvram.ready=1 equivalent)", r);
	state_put("nvram.ready", "yes");
	return 0;
}

/* ---------------------------------------------------------------- main */
static void usage(void)
{
	fprintf(stderr,
"mtk-wmt-init %s - MT6789 connsys bring-up (wmt_loader + wmt_launcher replacement)\n"
"usage: mtk-wmt-init [options]                 run loader + launcher; powers connsys and stays resident as daemon\n"
"       mtk-wmt-init wifi-nvram [--file F] [--force] [--dry-run]   push Wi-Fi NVRAM to the gen4m driver\n"
"options:\n"
"  --dry-run          do not touch any device: print every ioctl that would be made (canned replies), scan the firmware dir\n"
"  --dry-cmd NAME     (dry-run) kernel request to simulate, default srh_rom_patch\n"
"  -f, --foreground   do not fork into the background\n"
"  -p, --fwdir DIR    firmware directory (default /vendor/firmware)\n"
"  --devdir DIR       device node directory (default /dev)\n"
"  --state DIR        state/log dir (default /run/mtk-wmt)\n"
"  --log FILE         daemon log file (default <state>/mtk-wmt-init.log)\n"
"  --power-arg N      WMT_IOCTL_LPBK_POWER_CTRL argument (default 2 = vendor `-o 1`; 1 = vendor without -o)\n"
"  --no-poweron       serve patch requests only; connsys is powered by the first Wi-Fi/BT function-on\n"
"  --wait SEC         parent waits this long for power-on result (default 240)\n"
"  --pattern P        override firmware pattern (1d_1 for MT6631, 1e_1 for MT6635)\n"
"  --chip 0xNNNN, --adie 0xNNNN, --dry-fwver N   dry-run only: pretend ids\n"
"  --force-loader     redo loader stage even if %s/driver.ready exists\n"
"  --no-vercheck      accept firmware files whose header version byte differs from the chip's fw_version (debug)\n"
"  -q                 less verbose\n", VERSION, "/run/mtk-wmt");
}

static int run_all(void)
{
	logm("mtk-wmt-init %s starting: dry=%d fwdir=%s devdir=%s power_arg=%d state=%s", VERSION, g_dry, g_fwdir, g_devdir, g_power_arg, g_state);
	if (!g_dry) {
		struct stat st;
		char p[300];
		snprintf(p, sizeof p, "%s/WMT_SOC.cfg", g_fwdir);
		if (stat(p, &st) < 0)
			logm("WARNING: %s not found: the kernel reads WMT_SOC.cfg via request_firmware during DO_MODULE_INIT (firmware_class.path must point at the vendor firmware)", p);
		else
			logd("found %s (%lld bytes)", p, (long long)st.st_size);
	}
	if (stage_loader() < 0) {
		report('F');
		return 1;
	}
	int r = stage_launcher();
	if (r < 0)
		report('F');
	return r < 0;
}

int main(int argc, char **argv)
{
	clock_gettime(CLOCK_MONOTONIC, &g_t0);
	const char *sub = NULL, *nvfile = "/mnt/vendor/nvdata/APCFG/APRDEB/WIFI";
	int force = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(), exit(2), (char *)0))
		if (!strcmp(a, "wifi-nvram")) sub = "wifi-nvram";
		else if (!strcmp(a, "--dry-run") || !strcmp(a, "-n")) g_dry = 1;
		else if (!strcmp(a, "--dry-cmd")) g_dry_cmd = NEXT();
		else if (!strcmp(a, "-f") || !strcmp(a, "--foreground")) g_fg = 1;
		else if (!strcmp(a, "-p") || !strcmp(a, "--fwdir")) g_fwdir = NEXT();
		else if (!strcmp(a, "--devdir")) g_devdir = NEXT();
		else if (!strcmp(a, "--state")) g_state = NEXT();
		else if (!strcmp(a, "--log")) g_logfile = NEXT();
		else if (!strcmp(a, "--file")) nvfile = NEXT();
		else if (!strcmp(a, "--force")) force = 1;
		else if (!strcmp(a, "--power-arg")) g_power_arg = atoi(NEXT());
		else if (!strcmp(a, "--no-poweron")) g_power_arg = -1;
		else if (!strcmp(a, "--wait")) g_wait_sec = atoi(NEXT());
		else if (!strcmp(a, "--pattern")) g_pattern_override = NEXT();
		else if (!strcmp(a, "--chip")) g_chip_override = strtoul(NEXT(), NULL, 0);
		else if (!strcmp(a, "--adie")) g_adie_override = strtoul(NEXT(), NULL, 0);
		else if (!strcmp(a, "--dry-fwver")) g_dry_fwver = strtoul(NEXT(), NULL, 0);
		else if (!strcmp(a, "--force-loader")) g_force_loader = 1;
		else if (!strcmp(a, "--no-vercheck")) g_no_vercheck = 1;
		else if (!strcmp(a, "-q")) g_verbose = 1;
		else if (!strcmp(a, "--version")) { puts("mtk-wmt-init " VERSION); return 0; }
		else { usage(); return !(!strcmp(a, "-h") || !strcmp(a, "--help")) * 2; }
	}
	g_kmsg = open("/dev/kmsg", O_WRONLY | O_CLOEXEC | O_NOCTTY);
	if (g_adie_override && g_dry) g_adie = g_adie_override;

	if (sub && !strcmp(sub, "wifi-nvram"))
		return cmd_wifi_nvram(nvfile, force);

	if (g_dry || g_fg) {
		int rc = run_all();
		if (g_status_pipe >= 0) report(rc ? 'F' : 'K');
		return rc;
	}

	/* daemon mode: the child does everything, the parent only waits for the power-on verdict so scripts get an exit status */
	{
		int pfd[2];
		char pidp[300], logp[300], c = 0;
		if (!g_dry) mkdir(g_state, 0755);
		snprintf(pidp, sizeof pidp, "%s/mtk-wmt-init.pid", g_state);
		FILE *pf = fopen(pidp, "r");
		if (pf) {
			char ob[32] = "";
			int old = 0;
			if (fgets(ob, sizeof ob, pf))
				old = atoi(ob);
			if (old > 1 && kill(old, 0) == 0) {
				fclose(pf);
				logm("already running as pid %d (state in %s); nothing to do", old, g_state);
				return 0;
			}
			fclose(pf);
		}
		if (pipe(pfd) < 0) { perror("pipe"); return 1; }
		pid_t pid = fork();
		if (pid < 0) { perror("fork"); return 1; }
		if (pid > 0) {
			close(pfd[1]);
			struct pollfd p = { .fd = pfd[0], .events = POLLIN };
			int r = poll(&p, 1, g_wait_sec * 1000);
			if (r > 0 && read(pfd[0], &c, 1) == 1) {
				if (c == 'K') { logm("parent: connsys power-on OK (daemon pid %d keeps serving firmware requests)", pid); return 0; }
				loge("parent: bring-up FAILED (daemon pid %d; see log %s)", pid, g_logfile ? g_logfile : "<state>/mtk-wmt-init.log");
				return 2;
			}
			if (r == 0) { loge("parent: no verdict after %d s (daemon pid %d still running; check dmesg)", g_wait_sec, pid); return 3; }
			loge("parent: daemon (pid %d) exited without a verdict", pid);
			return 4;
		}
		close(pfd[0]);
		g_status_pipe = pfd[1];
		setsid();
		snprintf(logp, sizeof logp, "%s/mtk-wmt-init.log", g_state);
		int lf = open(g_logfile ? g_logfile : logp, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); }
		if (lf >= 0) dup2(lf, 2);
		FILE *w = fopen(pidp, "w");
		if (w) { fprintf(w, "%d\n", (int)getpid()); fclose(w); }
		chdir("/");
		int rc = run_all();
		if (rc) report('F');
		unlink(pidp);
		return rc;
	}
}
