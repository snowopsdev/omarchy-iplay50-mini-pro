/*
 * kmstest - dependency-free KMS / dma-buf smoke test for the tablet (aarch64 Linux, raw syscalls).
 * Built freestanding (no libc) so it runs on the bare initramfs / Arch rootfs without libdrm.
 *
 *   kmstest info              driver, caps, connectors, CRTCs, planes (formats, properties)
 *   kmstest legacy [secs]     dumb buffer + SETCRTC color bars (hold secs, default 5)
 *   kmstest flip [frames]     legacy PAGE_FLIP loop with vblank events, prints fps
 *   kmstest atomic [secs]     atomic TEST_ONLY then real commit (dumb buffer)
 *   kmstest heap [name] [secs] alloc from /dev/dma_heap/<name> (default system), PRIME import, scanout it
 *   kmstest bw                CPU write/read bandwidth: dumb (WC) vs dma-heap vs anonymous memory
 *
 * Exit code 0 only if the requested step succeeded.
 */
#include <linux/types.h>
#include <linux/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/time.h>
#include <asm/unistd.h>
#include <asm/fcntl.h>
#include <asm/mman.h>

#ifndef MAP_SHARED
#define MAP_SHARED 1
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif
#ifndef DRM_PLANE_TYPE_PRIMARY
#define DRM_PLANE_TYPE_PRIMARY 1
#endif
#ifndef DRM_MODE_ATOMIC_PAGE_FLIP_EVENT
#define DRM_MODE_ATOMIC_PAGE_FLIP_EVENT 0x01
#endif
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

/* ---------------------------------------------------------------- syscalls */
#if defined(__aarch64__)
static long sc6(long n, long a, long b, long c, long d, long e, long f)
{
	register long x8 __asm__("x8") = n;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
	return x0;
}
#else /* x86_64 host build, only for logic checks on the build machine */
static long sc6(long n, long a, long b, long c, long d, long e, long f)
{
	long ret;
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
	return ret;
}
#endif
#define sc3(n, a, b, c) sc6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)

static int g_errno;
static long chk(long r)
{
	if (r < 0 && r > -4096) {
		g_errno = (int)-r;
		return -1;
	}
	return r;
}
static int sys_openat(const char *p, int fl) { return (int)chk(sc6(__NR_openat, -100, (long)p, fl | O_CLOEXEC, 0, 0, 0)); }
static int sys_close(int fd) { return (int)chk(sc3(__NR_close, fd, 0, 0)); }
static int sys_ioctl(int fd, unsigned long req, void *arg) { return (int)chk(sc3(__NR_ioctl, fd, req, arg)); }
static void *sys_mmap(size_t len, int prot, int flags, int fd, u64 off)
{
	long r = sc6(__NR_mmap, 0, (long)len, prot, flags, fd, (long)off);
	if (r < 0 && r > -4096) {
		g_errno = (int)-r;
		return (void *)-1;
	}
	return (void *)r;
}
static ssize_t sys_write(int fd, const void *b, size_t n) { return chk(sc3(__NR_write, fd, b, n)); }
static ssize_t sys_read(int fd, void *b, size_t n) { return chk(sc3(__NR_read, fd, b, n)); }
static void sys_exit(int c) { for (;;) sc3(__NR_exit_group, c, 0, 0); }
static u64 now_ns(void)
{
	struct __kernel_timespec ts;
	sc3(__NR_clock_gettime, 1 /*MONOTONIC*/, &ts, 0);
	return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}
static void sleep_ms(unsigned ms)
{
	struct __kernel_timespec ts = {ms / 1000, (long long)(ms % 1000) * 1000000ll};
	sc6(__NR_nanosleep, (long)&ts, 0, 0, 0, 0, 0);
}
static int sys_poll(int fd, int ms)
{
	struct { int fd; short ev, rev; } p = {fd, 1, 0};
	struct __kernel_timespec ts = {ms / 1000, (long long)(ms % 1000) * 1000000ll};
	long r = sc6(__NR_ppoll, (long)&p, 1, (long)&ts, 0, 8, 0);
	return r > 0 && (p.rev & 1);
}

/* ---------------------------------------------------------------- libc bits */
__attribute__((optnone)) void *memset(void *d, int c, size_t n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
__attribute__((optnone)) void *memcpy(void *d, const void *s, size_t n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }

static void out(const char *s) { sys_write(1, s, slen(s)); }
static void outu(u64 v)
{
	char b[24]; int i = 23; b[i] = 0;
	if (!v) b[--i] = '0';
	while (v) { b[--i] = '0' + v % 10; v /= 10; }
	out(b + i);
}
static void outi(long v) { if (v < 0) { out("-"); outu((u64)-v); } else outu((u64)v); }
static void outx(u64 v)
{
	char b[20]; int i = 19; b[i] = 0;
	if (!v) b[--i] = '0';
	while (v) { b[--i] = "0123456789abcdef"[v & 15]; v >>= 4; }
	out("0x"); out(b + i);
}
static void fourcc(u32 f)
{
	char b[5] = {(char)f, (char)(f >> 8), (char)(f >> 16), (char)(f >> 24), 0};
	out(b);
}
static void fail(const char *what)
{
	out("FAIL: "); out(what); out(" errno="); outi(g_errno); out("\n");
}
static u64 atou(const char *s, u64 def)
{
	u64 v = 0; if (!s) return def;
	for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
	return v;
}

/* ---------------------------------------------------------------- DRM helpers */
static int fd;
#define MAXN 32
static u32 crtcs[MAXN], conns[MAXN], encs[MAXN];
static struct drm_mode_get_plane_res pres;
static u32 planes[MAXN];

static int drm_get_resources(struct drm_mode_card_res *r)
{
	memset(r, 0, sizeof *r);
	r->crtc_id_ptr = (u64)(long)crtcs; r->count_crtcs = MAXN;
	r->connector_id_ptr = (u64)(long)conns; r->count_connectors = MAXN;
	r->encoder_id_ptr = (u64)(long)encs; r->count_encoders = MAXN;
	return sys_ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, r);
}

struct prop_info { u32 id; u64 val; };
static int get_prop(u32 obj, u32 type, const char *name, struct prop_info *pi, int dump)
{
	u32 ids[64]; u64 vals[64];
	struct drm_mode_obj_get_properties g;
	memset(&g, 0, sizeof g);
	g.obj_id = obj; g.obj_type = type;
	g.props_ptr = (u64)(long)ids; g.prop_values_ptr = (u64)(long)vals; g.count_props = 64;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &g)) return -1;
	for (u32 i = 0; i < g.count_props && i < 64; i++) {
		struct drm_mode_get_property p;
		struct drm_mode_property_enum en[16];
		u64 pv[16];
		memset(&p, 0, sizeof p);
		p.prop_id = ids[i];
		p.enum_blob_ptr = (u64)(long)en; p.count_enum_blobs = 16;
		p.values_ptr = (u64)(long)pv; p.count_values = 16;
		if (sys_ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &p)) continue;
		if (dump) {
			out("      prop "); outu(ids[i]); out(" "); out(p.name); out(" = "); outu(vals[i]);
			if (p.flags & DRM_MODE_PROP_IMMUTABLE) out(" (immutable)");
			if (p.flags & DRM_MODE_PROP_ENUM) {
				out(" enum{");
				for (u32 k = 0; k < p.count_enum_blobs && k < 16; k++) { out(en[k].name); out(k + 1 < p.count_enum_blobs ? "," : ""); }
				out("}");
			}
			if (p.flags & DRM_MODE_PROP_RANGE) { out(" range["); outu(pv[0]); out(".."); outu(pv[1]); out("]"); }
			out("\n");
		}
		if (name && seq(p.name, name)) { pi->id = ids[i]; pi->val = vals[i]; if (!dump) return 0; }
	}
	return name ? (pi->id ? 0 : -1) : 0;
}

static int pick_output(u32 *conn, u32 *crtc, struct drm_mode_modeinfo *mode)
{
	struct drm_mode_card_res r;
	if (drm_get_resources(&r)) { fail("GETRESOURCES"); return -1; }
	for (u32 i = 0; i < r.count_connectors; i++) {
		struct drm_mode_modeinfo modes[64];
		u32 ce[8]; u32 ps[8]; u64 pvv[8];
		struct drm_mode_get_connector c;
		memset(&c, 0, sizeof c);
		c.connector_id = conns[i];
		c.modes_ptr = (u64)(long)modes; c.count_modes = 64;
		c.encoders_ptr = (u64)(long)ce; c.count_encoders = 8;
		c.props_ptr = (u64)(long)ps; c.prop_values_ptr = (u64)(long)pvv; c.count_props = 8;
		if (sys_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
		if (c.connection != 1 || !c.count_modes) continue;
		*conn = conns[i];
		*mode = modes[0];
		/* find a crtc: current encoder's crtc, else first crtc */
		*crtc = crtcs[0];
		if (c.encoder_id) {
			struct drm_mode_get_encoder e;
			memset(&e, 0, sizeof e); e.encoder_id = c.encoder_id;
			if (!sys_ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) && e.crtc_id) *crtc = e.crtc_id;
		}
		return 0;
	}
	out("FAIL: no connected connector with modes\n");
	return -1;
}

struct buf { u32 handle, pitch, fb, w, h; u64 size; u8 *map; int dmafd; };

static void fill_bars(struct buf *b, unsigned shift)
{
	static const u32 col[8] = {0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x202020};
	for (u32 y = 0; y < b->h; y++) {
		u32 *row = (u32 *)(b->map + (u64)y * b->pitch);
		for (u32 x = 0; x < b->w; x++) {
			u32 c = col[((x + shift) * 8 / b->w) & 7];
			if (y < 8 || y >= b->h - 8 || x < 8 || x >= b->w - 8) c = 0xff8000; /* orange frame */
			row[x] = c;
		}
	}
}

static int add_fb(struct buf *b)
{
	struct drm_mode_fb_cmd2 f;
	memset(&f, 0, sizeof f);
	f.width = b->w; f.height = b->h; f.pixel_format = 0x34325258 /* XR24 */;
	f.handles[0] = b->handle; f.pitches[0] = b->pitch;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &f)) { fail("ADDFB2"); return -1; }
	b->fb = f.fb_id;
	return 0;
}

static int make_dumb(struct buf *b, u32 w, u32 h)
{
	struct drm_mode_create_dumb c;
	struct drm_mode_map_dumb m;
	memset(&c, 0, sizeof c); c.width = w; c.height = h; c.bpp = 32;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) { fail("CREATE_DUMB"); return -1; }
	b->handle = c.handle; b->pitch = c.pitch; b->size = c.size; b->w = w; b->h = h; b->dmafd = -1;
	memset(&m, 0, sizeof m); m.handle = c.handle;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) { fail("MAP_DUMB"); return -1; }
	b->map = sys_mmap(c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
	if (b->map == (u8 *)-1) { fail("mmap dumb"); return -1; }
	out("  dumb buffer "); outu(w); out("x"); outu(h); out(" pitch="); outu(c.pitch); out(" size="); outu(c.size); out(" handle="); outu(c.handle); out("\n");
	return add_fb(b);
}

/* ---------------------------------------------------------------- commands */
static int cmd_info(void)
{
	struct drm_version v; char name[64], date[64], desc[128];
	memset(&v, 0, sizeof v);
	v.name = name; v.name_len = 63; v.date = date; v.date_len = 63; v.desc = desc; v.desc_len = 127;
	if (sys_ioctl(fd, DRM_IOCTL_VERSION, &v)) { fail("VERSION"); return 1; }
	name[v.name_len < 63 ? v.name_len : 63] = 0; desc[v.desc_len < 127 ? v.desc_len : 127] = 0;
	out("driver: "); out(name); out(" "); outu(v.version_major); out("."); outu(v.version_minor); out(" ("); out(desc); out(")\n");
	static const struct { u64 cap; const char *n; } caps[] = {
		{DRM_CAP_DUMB_BUFFER, "DUMB_BUFFER"}, {DRM_CAP_PRIME, "PRIME(1=imp,2=exp)"}, {DRM_CAP_TIMESTAMP_MONOTONIC, "TIMESTAMP_MONOTONIC"},
		{DRM_CAP_ASYNC_PAGE_FLIP, "ASYNC_PAGE_FLIP"}, {DRM_CAP_CURSOR_WIDTH, "CURSOR_WIDTH"}, {DRM_CAP_CURSOR_HEIGHT, "CURSOR_HEIGHT"},
		{DRM_CAP_ADDFB2_MODIFIERS, "ADDFB2_MODIFIERS"}, {DRM_CAP_CRTC_IN_VBLANK_EVENT, "CRTC_IN_VBLANK_EVENT"},
		{DRM_CAP_SYNCOBJ, "SYNCOBJ"}, {DRM_CAP_SYNCOBJ_TIMELINE, "SYNCOBJ_TIMELINE"}, {DRM_CAP_DUMB_PREFER_SHADOW, "DUMB_PREFER_SHADOW"},
		{DRM_CAP_DUMB_PREFERRED_DEPTH, "DUMB_PREFERRED_DEPTH"},
	};
	for (unsigned i = 0; i < sizeof caps / sizeof *caps; i++) {
		struct drm_get_cap g = {caps[i].cap, 0};
		sys_ioctl(fd, DRM_IOCTL_GET_CAP, &g);
		out("cap "); out(caps[i].n); out(" = "); outu(g.value); out("\n");
	}
	struct drm_set_client_cap cc = {DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1};
	out("client cap UNIVERSAL_PLANES: "); outi(sys_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cc)); out("\n");
	cc.capability = DRM_CLIENT_CAP_ATOMIC; cc.value = 1;
	out("client cap ATOMIC: "); outi(sys_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cc)); out("\n");

	struct drm_mode_card_res r;
	if (drm_get_resources(&r)) { fail("GETRESOURCES"); return 1; }
	out("fb size min "); outu(r.min_width); out("x"); outu(r.min_height); out(" max "); outu(r.max_width); out("x"); outu(r.max_height); out("\n");
	for (u32 i = 0; i < r.count_crtcs; i++) {
		out("crtc "); outu(crtcs[i]); out("\n");
		get_prop(crtcs[i], DRM_MODE_OBJECT_CRTC, 0, 0, 1);
	}
	for (u32 i = 0; i < r.count_connectors; i++) {
		struct drm_mode_modeinfo modes[64]; u32 ce[8];
		struct drm_mode_get_connector c;
		memset(&c, 0, sizeof c);
		c.connector_id = conns[i];
		c.modes_ptr = (u64)(long)modes; c.count_modes = 64;
		c.encoders_ptr = (u64)(long)ce; c.count_encoders = 8;
		if (sys_ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &c)) continue;
		out("connector "); outu(conns[i]); out(" type="); outu(c.connector_type); out(" status="); outu(c.connection);
		out(" mm="); outu(c.mm_width); out("x"); outu(c.mm_height); out(" encoder="); outu(c.encoder_id); out("\n");
		for (u32 m = 0; m < c.count_modes && m < 64; m++) {
			out("    mode "); out(modes[m].name); out(" "); outu(modes[m].hdisplay); out("x"); outu(modes[m].vdisplay);
			out("@"); outu(modes[m].vrefresh); out(" clock="); outu(modes[m].clock); out(" flags="); outx(modes[m].flags); out("\n");
		}
		get_prop(conns[i], DRM_MODE_OBJECT_CONNECTOR, 0, 0, 1);
	}
	memset(&pres, 0, sizeof pres);
	pres.plane_id_ptr = (u64)(long)planes; pres.count_planes = MAXN;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres)) { fail("GETPLANERESOURCES"); return 1; }
	for (u32 i = 0; i < pres.count_planes && i < MAXN; i++) {
		u32 fm[64]; struct drm_mode_get_plane p;
		memset(&p, 0, sizeof p);
		p.plane_id = planes[i]; p.format_type_ptr = (u64)(long)fm; p.count_format_types = 64;
		if (sys_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p)) continue;
		out("plane "); outu(planes[i]); out(" possible_crtcs="); outx(p.possible_crtcs); out(" formats:");
		for (u32 k = 0; k < p.count_format_types && k < 64; k++) { out(" "); fourcc(fm[k]); }
		out("\n");
		get_prop(planes[i], DRM_MODE_OBJECT_PLANE, 0, 0, 1);
	}
	return 0;
}

static int show_legacy(struct buf *b, u32 conn, u32 crtc, struct drm_mode_modeinfo *mode)
{
	struct drm_mode_crtc c;
	memset(&c, 0, sizeof c);
	c.crtc_id = crtc; c.fb_id = b->fb; c.set_connectors_ptr = (u64)(long)&conn; c.count_connectors = 1;
	c.mode_valid = 1; c.mode = *mode;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &c)) { fail("SETCRTC"); return -1; }
	return 0;
}

static int cmd_legacy(u64 secs)
{
	u32 conn, crtc; struct drm_mode_modeinfo mode; struct buf b;
	if (pick_output(&conn, &crtc, &mode)) return 1;
	out("using connector "); outu(conn); out(" crtc "); outu(crtc); out(" mode "); out(mode.name); out("\n");
	if (make_dumb(&b, mode.hdisplay, mode.vdisplay)) return 1;
	fill_bars(&b, 0);
	if (show_legacy(&b, conn, crtc, &mode)) return 1;
	out("OK: SETCRTC succeeded; you should see color bars with an orange frame for "); outu(secs); out("s\n");
	sleep_ms((unsigned)secs * 1000);
	return 0;
}

static int cmd_flip(u64 frames)
{
	u32 conn, crtc; struct drm_mode_modeinfo mode; struct buf b[2];
	if (pick_output(&conn, &crtc, &mode)) return 1;
	if (make_dumb(&b[0], mode.hdisplay, mode.vdisplay) || make_dumb(&b[1], mode.hdisplay, mode.vdisplay)) return 1;
	fill_bars(&b[0], 0);
	if (show_legacy(&b[0], conn, crtc, &mode)) return 1;
	u64 t0 = now_ns(), tmax = 0, tlast = t0, ev = 0, lost = 0;
	u64 tcpu = 0;
	for (u64 i = 1; i <= frames; i++) {
		struct buf *n = &b[i & 1];
		u64 c0 = now_ns();
		fill_bars(n, (unsigned)(i * 16));
		tcpu += now_ns() - c0;
		struct drm_mode_crtc_page_flip f;
		memset(&f, 0, sizeof f);
		f.crtc_id = crtc; f.fb_id = n->fb; f.flags = DRM_MODE_PAGE_FLIP_EVENT; f.user_data = i;
		if (sys_ioctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &f)) { fail("PAGE_FLIP"); return 1; }
		if (!sys_poll(fd, 1000)) { out("FAIL: no flip event within 1s at frame "); outu(i); out("\n"); return 1; }
		char evb[256];
		ssize_t r = sys_read(fd, evb, sizeof evb);
		if (r < 8) { lost++; continue; }
		ev++;
		u64 t = now_ns();
		if (t - tlast > tmax) tmax = t - tlast;
		tlast = t;
	}
	u64 dt = now_ns() - t0;
	out("flip: frames="); outu(frames); out(" events="); outu(ev); out(" lost="); outu(lost);
	out(" elapsed_ms="); outu(dt / 1000000); out(" fps="); outu(frames * 1000000000ull / (dt ? dt : 1));
	out(" worst_interval_ms="); outu(tmax / 1000000); out(" avg_cpu_fill_ms="); outu(tcpu / frames / 1000000); out("\n");
	return 0;
}

static int cmd_atomic(u64 secs)
{
	u32 conn, crtc; struct drm_mode_modeinfo mode; struct buf b;
	struct drm_set_client_cap cc = {DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1};
	sys_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cc);
	cc.capability = DRM_CLIENT_CAP_ATOMIC; cc.value = 1;
	if (sys_ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cc)) { fail("CLIENT_CAP_ATOMIC"); return 1; }
	if (pick_output(&conn, &crtc, &mode)) return 1;
	if (make_dumb(&b, mode.hdisplay, mode.vdisplay)) return 1;
	fill_bars(&b, 40);

	/* locate crtc index, and a primary plane for it */
	struct drm_mode_card_res r; drm_get_resources(&r);
	u32 cidx = 0; for (u32 i = 0; i < r.count_crtcs; i++) if (crtcs[i] == crtc) cidx = i;
	memset(&pres, 0, sizeof pres);
	pres.plane_id_ptr = (u64)(long)planes; pres.count_planes = MAXN;
	sys_ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &pres);
	u32 plane = 0;
	for (u32 i = 0; i < pres.count_planes && i < MAXN && !plane; i++) {
		struct drm_mode_get_plane p; memset(&p, 0, sizeof p); p.plane_id = planes[i];
		if (sys_ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p) || !(p.possible_crtcs & (1u << cidx))) continue;
		struct prop_info t = {0, 0};
		if (!get_prop(planes[i], DRM_MODE_OBJECT_PLANE, "type", &t, 0) && t.val == DRM_PLANE_TYPE_PRIMARY) plane = planes[i];
	}
	if (!plane) { out("FAIL: no primary plane for crtc\n"); return 1; }
	out("atomic: plane "); outu(plane); out(" crtc "); outu(crtc); out(" conn "); outu(conn); out("\n");

	struct drm_mode_create_blob cb; memset(&cb, 0, sizeof cb);
	cb.length = sizeof mode; cb.data = (u64)(long)&mode;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &cb)) { fail("CREATEPROPBLOB"); return 1; }

	struct prop_info pi_crtc_id = {0,0}, pi_active = {0,0}, pi_mode = {0,0};
	struct prop_info pl_fb = {0,0}, pl_crtc = {0,0}, sx = {0,0}, sy = {0,0}, sw = {0,0}, sh = {0,0}, cx = {0,0}, cy = {0,0}, cw = {0,0}, ch = {0,0};
	int bad = 0;
	bad |= get_prop(conn, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", &pi_crtc_id, 0);
	bad |= get_prop(crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE", &pi_active, 0);
	bad |= get_prop(crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID", &pi_mode, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "FB_ID", &pl_fb, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_ID", &pl_crtc, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "SRC_X", &sx, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "SRC_Y", &sy, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "SRC_W", &sw, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "SRC_H", &sh, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_X", &cx, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_Y", &cy, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_W", &cw, 0);
	bad |= get_prop(plane, DRM_MODE_OBJECT_PLANE, "CRTC_H", &ch, 0);
	if (bad) { out("FAIL: a required standard atomic property is missing\n"); return 1; }

	u32 objs[3] = {conn, crtc, plane};
	u32 cnt[3] = {1, 2, 10};
	u32 props[13] = {pi_crtc_id.id, pi_active.id, pi_mode.id, pl_fb.id, pl_crtc.id, sx.id, sy.id, sw.id, sh.id, cx.id, cy.id, cw.id, ch.id};
	u64 vals[13] = {crtc, 1, cb.blob_id, b.fb, crtc, 0, 0, (u64)mode.hdisplay << 16, (u64)mode.vdisplay << 16, 0, 0, mode.hdisplay, mode.vdisplay};
	u32 props13[13]; for (int i = 0; i < 13; i++) props13[i] = props[i];
	struct drm_mode_atomic a; memset(&a, 0, sizeof a);
	a.count_objs = 3; a.objs_ptr = (u64)(long)objs; a.count_props_ptr = (u64)(long)cnt;
	a.props_ptr = (u64)(long)props13; a.prop_values_ptr = (u64)(long)vals;
	a.flags = DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &a)) { fail("ATOMIC TEST_ONLY"); return 1; }
	out("OK: atomic TEST_ONLY accepted\n");
	a.flags = DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_PAGE_FLIP_EVENT;
	a.user_data = 1;
	if (sys_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &a)) { fail("ATOMIC commit"); return 1; }
	if (sys_poll(fd, 2000)) { char evb[256]; sys_read(fd, evb, sizeof evb); out("OK: atomic commit + completion event\n"); }
	else out("WARN: atomic commit succeeded but no flip event within 2s\n");
	/* second commit on same fb to check repeated nonblocking-style flips */
	fill_bars(&b, 200);
	a.flags = DRM_MODE_ATOMIC_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;
	{
		u32 o2[1] = {plane}; u32 c2[1] = {1}; u32 p2[1] = {pl_fb.id}; u64 v2[1] = {b.fb};
		struct drm_mode_atomic a2; memset(&a2, 0, sizeof a2);
		a2.count_objs = 1; a2.objs_ptr = (u64)(long)o2; a2.count_props_ptr = (u64)(long)c2;
		a2.props_ptr = (u64)(long)p2; a2.prop_values_ptr = (u64)(long)v2; a2.flags = a.flags; a2.user_data = 2;
		if (sys_ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &a2)) out("WARN: NONBLOCK|PAGE_FLIP_EVENT commit rejected\n");
		else if (sys_poll(fd, 2000)) { char evb[256]; sys_read(fd, evb, sizeof evb); out("OK: nonblocking atomic flip event\n"); }
		else out("WARN: nonblocking atomic flip: no event in 2s\n");
	}
	out("holding "); outu(secs); out("s\n");
	sleep_ms((unsigned)secs * 1000);
	return 0;
}

static int heap_alloc(const char *heap, u64 len)
{
	char path[64] = "/dev/dma_heap/";
	size_t n = slen(path); const char *p = heap;
	while (*p && n < 60) path[n++] = *p++;
	path[n] = 0;
	int hfd = sys_openat(path, O_RDWR);
	if (hfd < 0) { out("open "); out(path); out(" "); fail("open heap"); return -1; }
	struct dma_heap_allocation_data d; memset(&d, 0, sizeof d);
	d.len = len; d.fd_flags = O_RDWR | O_CLOEXEC;
	if (sys_ioctl(hfd, DMA_HEAP_IOCTL_ALLOC, &d)) { fail("DMA_HEAP_IOCTL_ALLOC"); sys_close(hfd); return -1; }
	sys_close(hfd);
	return (int)d.fd;
}

static int cmd_heap(const char *heap, u64 secs)
{
	u32 conn, crtc; struct drm_mode_modeinfo mode; struct buf b;
	if (pick_output(&conn, &crtc, &mode)) return 1;
	u32 w = mode.hdisplay, h = mode.vdisplay, pitch = w * 4; u64 size = ((u64)pitch * h + 4095) & ~4095ull;
	int dfd = heap_alloc(heap, size);
	if (dfd < 0) return 1;
	out("heap "); out(heap); out(": allocated "); outu(size); out(" bytes, dmabuf fd="); outi(dfd); out("\n");
	u8 *map = sys_mmap(size, PROT_READ | PROT_WRITE, MAP_SHARED, dfd, 0);
	if (map == (u8 *)-1) { fail("mmap dmabuf"); return 1; }
	struct dma_buf_sync s = {DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE};
	sys_ioctl(dfd, DMA_BUF_IOCTL_SYNC, &s);
	b.map = map; b.pitch = pitch; b.w = w; b.h = h;
	fill_bars(&b, 100);
	s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
	sys_ioctl(dfd, DMA_BUF_IOCTL_SYNC, &s);
	struct drm_prime_handle ph; memset(&ph, 0, sizeof ph); ph.fd = dfd;
	if (sys_ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph)) { fail("PRIME_FD_TO_HANDLE (display driver cannot import this heap's buffers)"); return 1; }
	out("OK: PRIME import of heap buffer, GEM handle="); outu(ph.handle); out("\n");
	b.handle = ph.handle; b.size = size;
	if (add_fb(&b)) return 1;
	out("OK: ADDFB2 on imported buffer\n");
	if (show_legacy(&b, conn, crtc, &mode)) return 1;
	out("OK: scanning out dma-heap buffer (bars shifted vs `legacy`) for "); outu(secs); out("s\n");
	sleep_ms((unsigned)secs * 1000);
	return 0;
}

static u64 bw_write(u8 *p, u64 n, int reps)
{
	u64 t = now_ns();
	for (int r = 0; r < reps; r++) {
		u64 *q = (u64 *)p; u64 v = 0x0101010101010101ull * (u64)(r + 1);
		for (u64 i = 0; i < n / 8; i++) q[i] = v;
		__asm__ volatile("" ::: "memory");
	}
	return now_ns() - t;
}
static u64 bw_read(u8 *p, u64 n, int reps, u64 *sink)
{
	u64 t = now_ns(), acc = 0;
	for (int r = 0; r < reps; r++) {
		volatile u64 *q = (volatile u64 *)p;
		for (u64 i = 0; i < n / 8; i++) acc += q[i];
	}
	*sink = acc;
	return now_ns() - t;
}
static void bw_one(const char *label, u8 *p, u64 n)
{
	u64 sink, tw = bw_write(p, n, 4), tr = bw_read(p, n, 2, &sink);
	out("  "); out(label); out(": write "); outu(n * 4 * 1000ull / (tw ? tw : 1)); out(" MB/s, read ");
	outu(n * 2 * 1000ull / (tr ? tr : 1)); out(" MB/s\n");
}
static int cmd_bw(void)
{
	u32 w = 1200, h = 1920; u64 size = ((u64)w * h * 4 + 4095) & ~4095ull;
	out("CPU bandwidth over one 1200x1920x4 frame ("); outu(size); out(" bytes); a 60 fps llvmpipe composite needs >= ~500 MB/s write plus reads for blending\n");
	u8 *anon = sys_mmap(size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (anon != (u8 *)-1) bw_one("anonymous (cached)   ", anon, size); else fail("anon mmap");
	struct buf b;
	if (!make_dumb(&b, w, h)) bw_one("KMS dumb buffer      ", b.map, b.size);
	static const char *heaps[] = {"system", "system-uncached", "mtk_mm", "mtk_mm-uncached", "mediatek"};
	for (unsigned i = 0; i < sizeof heaps / sizeof *heaps; i++) {
		int dfd = heap_alloc(heaps[i], size);
		if (dfd < 0) continue;
		u8 *m = sys_mmap(size, PROT_READ | PROT_WRITE, MAP_SHARED, dfd, 0);
		if (m == (u8 *)-1) { fail("mmap heap"); continue; }
		char lab[32]; size_t n = 0; const char *p = "heap/";
		while (*p) lab[n++] = *p++;
		for (p = heaps[i]; *p && n < 21; ) lab[n++] = *p++;
		while (n < 21) lab[n++] = ' ';
		lab[n] = 0;
		bw_one(lab, m, size);
	}
	return 0;
}

void c_main(long *sp)
{
	int argc = (int)sp[0];
	char **argv = (char **)(sp + 1);
	const char *cmd = argc > 1 ? argv[1] : "info";
	const char *a2 = argc > 2 ? argv[2] : 0;
	const char *a3 = argc > 3 ? argv[3] : 0;
	fd = sys_openat("/dev/dri/card0", O_RDWR);
	if (fd < 0) { fail("open /dev/dri/card0"); sys_exit(2); }
	int rc;
	if (seq(cmd, "info")) rc = cmd_info();
	else if (seq(cmd, "legacy")) rc = cmd_legacy(atou(a2, 5));
	else if (seq(cmd, "flip")) rc = cmd_flip(atou(a2, 120));
	else if (seq(cmd, "atomic")) rc = cmd_atomic(atou(a2, 5));
	else if (seq(cmd, "heap")) rc = cmd_heap(a2 && !(a2[0] >= '0' && a2[0] <= '9') ? a2 : "system", a3 ? atou(a3, 5) : (a2 ? atou(a2, 5) : 5));
	else if (seq(cmd, "bw")) rc = cmd_bw();
	else { out("usage: kmstest info|legacy [s]|flip [n]|atomic [s]|heap [name] [s]|bw\n"); rc = 2; }
	sys_exit(rc);
}

#if defined(__aarch64__)
__asm__(".globl _start\n_start:\n  mov x0, sp\n  and sp, x0, #-16\n  bl c_main\n");
#else
__asm__(".globl _start\n_start:\n  mov %rsp, %rdi\n  and $-16, %rsp\n  call c_main\n");
#endif
