/* Minimal EGL-on-Wayland window: clears to a colour that cycles red, green,
 * blue each second, for N seconds, and reports eglSwapBuffers failures and
 * frame times. Shows whether GL frames from Zink reach the compositor.
 * Build (tablet):
 *   wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.h
 *   wayland-scanner private-code  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.c
 *   cc -O2 egl-wayland-test.c xdg-shell.c -lwayland-client -lwayland-egl -lEGL -lGLESv2 -o egl-wayland-test */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell.h"

static struct wl_compositor *comp;
static struct xdg_wm_base *wm;
static int configured, width = 640, height = 480;

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v) {
    if (!strcmp(iface, wl_compositor_interface.name)) comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_l = {reg_global, reg_remove};
static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = {wm_ping};
static void surf_conf(void *d, struct xdg_surface *s, uint32_t serial) { xdg_surface_ack_configure(s, serial); configured = 1; }
static const struct xdg_surface_listener surf_l = {surf_conf};
static void top_conf(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st) { if (w > 0 && h > 0) { width = w; height = h; } }
static void top_close(void *d, struct xdg_toplevel *t) {}
static const struct xdg_toplevel_listener top_l = {top_conf, top_close};

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    double seconds = argc > 1 ? atof(argv[1]) : 6;
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) { fputs("no wayland display\n", stderr); return 1; }
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_l, NULL);
    wl_display_roundtrip(dpy);
    xdg_wm_base_add_listener(wm, &wm_l, NULL);
    struct wl_surface *surf = wl_compositor_create_surface(comp);
    struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surf);
    xdg_surface_add_listener(xs, &surf_l, NULL);
    struct xdg_toplevel *top = xdg_surface_get_toplevel(xs);
    xdg_toplevel_add_listener(top, &top_l, NULL);
    xdg_toplevel_set_title(top, "egl-wayland-test");
    xdg_toplevel_set_app_id(top, "egl-wayland-test");
    wl_surface_commit(surf);
    while (!configured) wl_display_dispatch(dpy);

    EGLDisplay ed = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, dpy, NULL);
    if (!eglInitialize(ed, NULL, NULL)) { fprintf(stderr, "eglInitialize %#x\n", eglGetError()); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg; EGLint n;
    if (!eglChooseConfig(ed, ca, &cfg, 1, &n) || !n) { fputs("no config\n", stderr); return 1; }
    EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(ed, cfg, EGL_NO_CONTEXT, xa);
    struct wl_egl_window *ew = wl_egl_window_create(surf, width, height);
    EGLSurface es = eglCreatePlatformWindowSurface(ed, cfg, ew, NULL);
    if (es == EGL_NO_SURFACE || !eglMakeCurrent(ed, es, es, ctx)) { fprintf(stderr, "surface %#x\n", eglGetError()); return 1; }
    printf("GL_RENDERER %s, window %dx%d\n", glGetString(GL_RENDERER), width, height);

    int frames = 0, fails = 0; double t0 = now(), worst = 0, last = t0;
    while (now() - t0 < seconds) {
        int phase = (int)(now() - t0) % 3;
        glClearColor(phase == 0, phase == 1, phase == 2, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (frames == 10) {
            unsigned char px[4] = {0};
            glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            printf("back buffer centre before swap: %u %u %u %u (phase %d)\n", px[0], px[1], px[2], px[3], phase);
        }
        if (!eglSwapBuffers(ed, es)) { fails++; fprintf(stderr, "eglSwapBuffers %#x\n", eglGetError()); }
        wl_display_dispatch_pending(dpy);
        double t = now(); if (t - last > worst) worst = t - last; last = t;
        frames++;
    }
    printf("frames %d in %.1f s (%.1f fps), swap failures %d, worst frame %.0f ms\n",
           frames, now() - t0, frames / (now() - t0), fails, worst * 1000);
    return fails ? 4 : 0;
}
