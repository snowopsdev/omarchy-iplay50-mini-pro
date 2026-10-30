/* Tablet-only Aquamarine 0.15.1 compatibility shim.
 *
 * With kms_swrast, the primary DRM node has no EGL device. Aquamarine retries
 * its unused secondary blit renderer after every commit. The primary display
 * already receives buffers from Hyprland's working GBM software renderer.
 * Skip that secondary maintenance only on the explicitly selected software
 * primary. Secondary GPUs retain the packaged implementation. No C++ object
 * layouts are accessed; both methods are exported by the installed library.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern char *program_invocation_short_name;

/* Hyprland 0.56.2 classifies software rendering using the DRM name. Our
 * display-only mediatek node therefore misses its llvmpipe glFinish path.
 * Change only the compositor's flush; clients keep their normal behavior. */
void glFlush(void) {
    typedef void (*gl_fn)(void);
    static gl_fn flush;
    static gl_fn finish;
    static int selected = -1;
    static bool reported;
    if (!flush) flush = (gl_fn)dlsym(RTLD_NEXT, "glFlush");
    if (!finish) finish = (gl_fn)dlsym(RTLD_NEXT, "glFinish");
    if (selected < 0) {
        const char *enabled = getenv("TABLET_FINISH_SOFTWARE_FRAMES");
        selected = enabled && !strcmp(enabled, "1") &&
                   program_invocation_short_name &&
                   !strcmp(program_invocation_short_name, "Hyprland");
    }
    if (selected && finish) {
        if (!reported) {
            fputs("[tablet] Finishing Hyprland llvmpipe frames before presentation\n", stderr);
            reported = true;
        }
        finish();
    } else if (flush) {
        flush();
    }
}

#define UPDATE "_ZN10Aquamarine11CDRMBackend28updateSecondaryRendererStateEPSt6vectorIN9Hyprutils6Memory14CSharedPointerINS_6CDRMFBEEESaIS6_EE"
#define SHOULD_BLIT "_ZN10Aquamarine11CDRMBackend10shouldBlitEv"

bool tablet_update_secondary(void *self, void *retired) __asm__(UPDATE);

bool tablet_update_secondary(void *self, void *retired) {
    typedef bool (*update_fn)(void *, void *);
    typedef bool (*blit_fn)(void *);
    static update_fn original;
    static blit_fn should_blit;
    static int selected = -1;
    static bool reported;
    if (!original) original = (update_fn)dlsym(RTLD_NEXT, UPDATE);
    if (!should_blit) should_blit = (blit_fn)dlsym(RTLD_NEXT, SHOULD_BLIT);
    if (selected < 0) {
        const char *enabled = getenv("AQ_TABLET_SOFTWARE_PRIMARY");
        const char *driver = getenv("GALLIUM_DRIVER");
        selected = enabled && !strcmp(enabled, "1") && driver && !strcmp(driver, "llvmpipe");
    }
    if (selected && should_blit && !should_blit(self)) {
        if (!reported) {
            fputs("[tablet] Skipping unused Aquamarine secondary renderer on llvmpipe primary\n", stderr);
            reported = true;
        }
        return true;
    }
    return original ? original(self, retired) : false;
}
