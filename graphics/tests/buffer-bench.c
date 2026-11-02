/* GPU cost of the buffers GPU apps hand to Hyprland. Renders (fill) and
 * samples (full-screen textured quad, straight and rotated 90 degrees) with
 * GPU-native textures and with linear GBM buffers from card0 imported as
 * dma-bufs (what Chromium's GPU process and the compositor use), timing each
 * frame with glFinish; then the cost of many small render passes in one
 * flush (Chromium's tile raster makes ~40 per frame). Run in the GPU browser
 * environment, e.g. through electron-gpu-process-launcher (Zink on PanVK,
 * GBM_ALWAYS_SOFTWARE=1). Args: [W H]. BENCH_LOOP=S: only 32-pass frames
 * for S seconds (for tracing).
 * Build: cc -O2 buffer-bench.c -lEGL -lGLESv2 -lgbm -o buffer-bench */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int W = 1920, H = 1200, N = 40;
static EGLDisplay d;
static struct gbm_device *gbm;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(s, sizeof log, NULL, log); fprintf(stderr, "shader: %s\n", log); exit(2); }
    return s;
}
static GLuint program(const char *fs) {
    const char *vs = "#version 300 es\nuniform int rot;out vec2 uv;void main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));"
                     "vec2 q=p[gl_VertexID];uv=q*0.5+0.5;if(rot==1)uv=vec2(uv.y,1.0-uv.x);gl_Position=vec4(q,0,1);}";
    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, vs)); glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { fputs("link failed\n", stderr); exit(2); }
    return p;
}

/* A texture: GPU-native storage, or a linear GBM buffer imported as a dma-buf. */
static GLuint texture(int linear, int w, int h) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    if (!linear) {
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    } else {
        struct gbm_bo *bo = gbm_bo_create(gbm, w, h, GBM_FORMAT_ARGB8888,
            GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
        if (!bo) { fputs("gbm_bo_create failed\n", stderr); exit(5); }
        int fd = gbm_bo_get_fd(bo);
        EGLAttrib ia[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_LINUX_DRM_FOURCC_EXT, GBM_FORMAT_ARGB8888,
            EGL_DMA_BUF_PLANE0_FD_EXT, fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, gbm_bo_get_stride(bo), EGL_NONE};
        EGLImage img = eglCreateImage(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, ia);
        close(fd);
        if (img == EGL_NO_IMAGE) { fprintf(stderr, "eglCreateImage %#x\n", eglGetError()); exit(5); }
        image_target(GL_TEXTURE_2D, img);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return t;
}
static GLuint fbo_for(GLuint tex) {
    GLuint f; glGenFramebuffers(1, &f); glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fputs("fbo incomplete\n", stderr); exit(2); }
    return f;
}

/* ms per frame (median of N, glFinish after each). src 0 = solid fill. */
static double run(GLuint fbo, GLuint prog, GLuint src, int rot) {
    double t[256];
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H);
    glUseProgram(prog); glUniform1i(glGetUniformLocation(prog, "rot"), rot);
    if (src) glBindTexture(GL_TEXTURE_2D, src);
    for (int i = 0; i < N + 3; i++) {
        double t0 = now();
        glUniform1f(glGetUniformLocation(prog, "k"), i / (float)N);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        if (i >= 3) t[i - 3] = (now() - t0) * 1000;
    }
    for (int i = 1; i < N; i++) for (int j = i; j > 0 && t[j] < t[j - 1]; j--) { double x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
    return t[N / 2];
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (argc > 2) { W = atoi(argv[1]); H = atoi(argv[2]); }
    int loop = getenv("BENCH_LOOP") ? atoi(getenv("BENCH_LOOP")) : 0; /* seconds of 32-pass frames only */
    int card = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    gbm = gbm_create_device(card);
    d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (!gbm || !eglInitialize(d, NULL, NULL)) { fputs("init failed\n", stderr); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
    if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { fputs("context failed\n", stderr); return 1; }
    image_target = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    printf("GL_RENDERER %s, %dx%d\n", glGetString(GL_RENDERER), W, H);

    if (loop) {
        GLuint f = program("#version 300 es\nprecision mediump float;uniform float k;out vec4 c;void main(){c=vec4(k,0.5,0.25,1);}");
        GLuint lt[32], lf[32];
        for (int i = 0; i < 32; i++) { lt[i] = texture(0, 256, 256); lf[i] = fbo_for(lt[i]); }
        glUseProgram(f);
        double t0 = now(); int frames = 0;
        while (now() - t0 < loop) {
            for (int i = 0; i < 32; i++) { glBindFramebuffer(GL_FRAMEBUFFER, lf[i]); glViewport(0, 0, 256, 256); glDrawArrays(GL_TRIANGLES, 0, 3); }
            glFinish(); frames++;
        }
        printf("loop: %.2f ms per 32-pass frame\n", (now() - t0) * 1000 / frames);
        return 0;
    }
    GLuint fill = program("#version 300 es\nprecision mediump float;uniform float k;out vec4 c;void main(){c=vec4(k,0.5,0.25,1);}");
    GLuint blit = program("#version 300 es\nprecision mediump float;uniform sampler2D s;in vec2 uv;out vec4 c;void main(){c=texture(s,uv);}");
    GLuint tn = texture(0, W, H), tl = texture(1, W, H);
    GLuint dn = texture(0, W, H), dl = texture(1, W, H);
    GLuint fn = fbo_for(dn), fl = fbo_for(dl), fsn = fbo_for(tn), fsl = fbo_for(tl);
    run(fsn, fill, 0, 0); run(fsl, fill, 0, 0); /* give the sources content */

    printf("fill  -> native            %6.2f ms\n", run(fn, fill, 0, 0));
    printf("fill  -> linear dma-buf    %6.2f ms\n", run(fl, fill, 0, 0));
    printf("native -> native           %6.2f ms\n", run(fn, blit, tn, 0));
    printf("native -> native, rotated  %6.2f ms\n", run(fn, blit, tn, 1));
    printf("linear -> native           %6.2f ms\n", run(fn, blit, tl, 0));
    printf("linear -> native, rotated  %6.2f ms\n", run(fn, blit, tl, 1));
    printf("native -> linear           %6.2f ms\n", run(fl, blit, tn, 0));
    printf("linear -> linear, rotated  %6.2f ms\n", run(fl, blit, tl, 1));

    /* Many small render passes in one flush, as Chromium's tile raster makes:
     * each pass is one tiler + one fragment job on the Mali. */
    enum { P = 40 };
    GLuint pt[P], pf[P];
    for (int i = 0; i < P; i++) { pt[i] = texture(0, 256, 256); pf[i] = fbo_for(pt[i]); }
    for (int passes = 1; passes <= P; passes *= 2) {
        double best = 1e9;
        glUseProgram(fill);
        for (int r = 0; r < 12; r++) {
            double t0 = now();
            for (int i = 0; i < passes; i++) {
                glBindFramebuffer(GL_FRAMEBUFFER, pf[i]); glViewport(0, 0, 256, 256);
                glUniform1f(glGetUniformLocation(fill, "k"), r / 12.0f);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            glFinish();
            double t = (now() - t0) * 1000; if (t < best) best = t;
        }
        printf("%2d passes of 256x256      %6.2f ms (%.2f ms/pass)\n", passes, best, best / passes);
    }
    return glGetError() ? 4 : 0;
}
