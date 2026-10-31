/* EGL_ANDROID_native_fence_sync round trip, the way Chromium uses it:
 * draw, create a native fence, flush, then eglDupNativeFenceFDANDROID.
 * Mode "late" finishes the GPU work before the dup (the case that returned
 * -1 and crashed Chromium's GPU process); "early" dups right after the
 * flush. Each fd must be >= 0 and become readable (signalled); it is then
 * imported back with EGL_SYNC_NATIVE_FENCE_FD_ANDROID and waited on.
 * Build: cc -O2 egl-fence-test.c -lEGL -lGLESv2 -o egl-fence-test */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, __VA_ARGS__); return 1; } } while (0)

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    int late = argc > 1 && !strcmp(argv[1], "late");
    int iters = argc > 2 ? atoi(argv[2]) : 200;

    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    CHECK(eglInitialize(d, NULL, NULL), "eglInitialize: %#x\n", eglGetError());
    const char *ext = eglQueryString(d, EGL_EXTENSIONS);
    CHECK(strstr(ext, "EGL_ANDROID_native_fence_sync"), "no EGL_ANDROID_native_fence_sync\n");
    PFNEGLCREATESYNCKHRPROC create_sync = (void *)eglGetProcAddress("eglCreateSyncKHR");
    PFNEGLDESTROYSYNCKHRPROC destroy_sync = (void *)eglGetProcAddress("eglDestroySyncKHR");
    PFNEGLWAITSYNCKHRPROC wait_sync = (void *)eglGetProcAddress("eglWaitSyncKHR");
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_fd = (void *)eglGetProcAddress("eglDupNativeFenceFDANDROID");

    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint attrs[] = {EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig config; EGLint count;
    CHECK(eglChooseConfig(d, attrs, &config, 1, &count) && count, "no config\n");
    EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(d, config, EGL_NO_CONTEXT, ca);
    CHECK(ctx && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx), "context: %#x\n", eglGetError());
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    printf("GL renderer: %s\n", renderer);
    CHECK(renderer && strstr(renderer, "Mali"), "not on the Mali GPU\n");

    GLuint tex, fbo;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 512, 512);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "fbo incomplete\n");

    int neg = 0, unsignalled = 0, import_fail = 0;
    for (int i = 0; i < iters; i++) {
        glClearColor((i & 255) / 255.0f, 0.5f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        EGLint sa[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE};
        EGLSyncKHR s = create_sync(d, EGL_SYNC_NATIVE_FENCE_ANDROID, sa);
        CHECK(s != EGL_NO_SYNC_KHR, "eglCreateSyncKHR: %#x\n", eglGetError());
        glFlush();
        if (late) { glFinish(); usleep(2000); }
        int fd = dup_fd(d, s);
        destroy_sync(d, s);
        if (fd < 0) { neg++; continue; }
        struct pollfd p = {.fd = fd, .events = POLLIN};
        if (poll(&p, 1, 2000) != 1 || !(p.revents & POLLIN)) unsignalled++;
        EGLint ia[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE};
        EGLSyncKHR in = create_sync(d, EGL_SYNC_NATIVE_FENCE_ANDROID, ia); /* takes the fd */
        if (in == EGL_NO_SYNC_KHR || !wait_sync(d, in, 0)) { import_fail++; close(fd); }
        if (in != EGL_NO_SYNC_KHR) destroy_sync(d, in);
    }
    glFinish();
    unsigned char px[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("mode %s, %d iterations: fd -1: %d, not signalled in 2 s: %d, import/wait failed: %d; last pixel %u %u %u, GL error %#x\n",
           late ? "late" : "early", iters, neg, unsignalled, import_fail, px[0], px[1], px[2], glGetError());
    int ok = !neg && !unsignalled && !import_fail;
    puts(ok ? "PASS" : "FAIL");
    return ok ? 0 : 4;
}
