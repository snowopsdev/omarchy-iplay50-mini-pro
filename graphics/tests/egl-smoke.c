/* Verify GLES shader drawing/readback through an isolated EGL display. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <xf86drm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static GLuint shader(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "shader: %s\n", log); exit(2);
    }
    return s;
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    int fd = -1;
    struct gbm_device *gbm = NULL;
    struct gbm_bo *bo = NULL;
    EGLImage image = EGL_NO_IMAGE;
    EGLDisplay d;
    if (argc == 2 && !strcmp(argv[1], "gbm")) {
        fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (fd < 0) { perror("card0"); return 1; }
        gbm = gbm_create_device(fd);
        if (!gbm) { fputs("gbm_create_device failed\n", stderr); return 1; }
        d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
    } else {
        d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    }
    EGLint major, minor;
    if (!eglInitialize(d, &major, &minor)) {
        fprintf(stderr, "eglInitialize: %#x\n", eglGetError()); return 1;
    }
    printf("EGL %d.%d: %s\n", major, minor, eglQueryString(d, EGL_VENDOR));
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return 1;
    EGLint attrs[] = {EGL_SURFACE_TYPE, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    EGLint count;
    if (!eglChooseConfig(d, attrs, &config, 1, &count) || !count) {
        EGLConfig configs[128]; EGLint n = 0;
        eglGetConfigs(d, configs, 128, &n);
        fprintf(stderr, "No GLES3 config; available configs: %d\n", n);
        for (int i = 0; i < n && i < 16; i++) {
            EGLint render, surface, red, alpha;
            eglGetConfigAttrib(d, configs[i], EGL_RENDERABLE_TYPE, &render);
            eglGetConfigAttrib(d, configs[i], EGL_SURFACE_TYPE, &surface);
            eglGetConfigAttrib(d, configs[i], EGL_RED_SIZE, &red);
            eglGetConfigAttrib(d, configs[i], EGL_ALPHA_SIZE, &alpha);
            fprintf(stderr, "config %d: render=%#x surface=%#x red=%d alpha=%d\n", i, render, surface, red, alpha);
        }
        return 1;
    }
    EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(d, config, EGL_NO_CONTEXT, ca);
    if (ctx == EGL_NO_CONTEXT ||
        !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        fprintf(stderr, "EGL context/surface: %#x\n", eglGetError()); return 1;
    }
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    printf("GL renderer: %s\nGL version: %s\n", renderer, glGetString(GL_VERSION));
    if (!renderer || !strstr(renderer, "Mali") || strstr(renderer, "llvmpipe")) {
        fputs("Refusing to count software rendering as acceleration\n", stderr); return 3;
    }
    const char *vs = "#version 300 es\nvoid main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);}";
    const char *fs = "#version 300 es\nprecision highp float;out vec4 c;void main(){c=vec4(0.25,0.5,0.75,1.0);}";
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { fputs("Program link failed\n", stderr); return 2; }
    GLuint tex, fbo;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    if (gbm) {
        bo = gbm_bo_create(gbm, 32, 32, GBM_FORMAT_ARGB8888,
            GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
        if (!bo) { fputs("GBM scanout allocation failed\n", stderr); return 5; }
        int dma = gbm_bo_get_fd(bo);
        int display = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        uint32_t handle;
        if (dma < 0 || display < 0 || drmPrimeFDToHandle(display, dma, &handle)) {
            perror("KMS PRIME import"); return 5;
        }
        close(display);
        printf("GBM linear buffer exports a KMS-importable dma-buf, stride %u\n", gbm_bo_get_stride(bo));
        EGLAttrib ia[] = {EGL_WIDTH, 32, EGL_HEIGHT, 32,
            EGL_LINUX_DRM_FOURCC_EXT, GBM_FORMAT_ARGB8888,
            EGL_DMA_BUF_PLANE0_FD_EXT, dma,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, gbm_bo_get_stride(bo), EGL_NONE};
        image = eglCreateImage(d, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, ia);
        close(dma);
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target =
            (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (image == EGL_NO_IMAGE || !target) {
            fprintf(stderr, "dma-buf EGL image: %#x\n", eglGetError()); return 5;
        }
        target(GL_TEXTURE_2D, image);
    } else {
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 32, 32);
    }
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 2;
    glUseProgram(p);
    glViewport(0, 0, 32, 32);
    glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    unsigned char pixel[4] = {0};
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum error = glGetError();
    printf("readback: %u %u %u %u; GL error %#x\n", pixel[0], pixel[1], pixel[2], pixel[3], error);
    if (error || abs((int)pixel[0]-64) > 1 || abs((int)pixel[1]-128) > 1 ||
        abs((int)pixel[2]-191) > 1 || pixel[3] != 255) return 4;
    puts("PASS: hardware GLES shader draw and readback");
    const char *tfvs = "#version 300 es\nout highp float captured;void main(){captured=float(gl_VertexID)*3.0+2.0;gl_Position=vec4(0,0,0,1);}";
    GLuint tv = shader(GL_VERTEX_SHADER, tfvs), tp = glCreateProgram(), tbo;
    glAttachShader(tp, tv); glAttachShader(tp, f);
    const char *varying = "captured";
    glTransformFeedbackVaryings(tp, 1, &varying, GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(tp); glGetProgramiv(tp, GL_LINK_STATUS, &ok);
    if (!ok) { fputs("Transform-feedback link failed\n", stderr); return 6; }
    glGenBuffers(1, &tbo); glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, tbo);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, 4 * sizeof(float), NULL, GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, tbo);
    glUseProgram(tp); glEnable(GL_RASTERIZER_DISCARD);
    glBeginTransformFeedback(GL_POINTS); glDrawArrays(GL_POINTS, 0, 4);
    glEndTransformFeedback(); glDisable(GL_RASTERIZER_DISCARD); glFinish();
    float *captured = glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
        4 * sizeof(float), GL_MAP_READ_BIT);
    if (!captured) { fputs("Transform-feedback map failed\n", stderr); return 6; }
    for (int i = 0; i < 4; i++) {
        if (captured[i] != i * 3.0f + 2.0f) {
            fprintf(stderr, "Transform feedback %d: got %f\n", i, captured[i]); return 6;
        }
    }
    glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
    if (glGetError()) return 6;
    puts("PASS: GLES3 transform-feedback capture and readback");
    glDeleteBuffers(1, &tbo); glDeleteProgram(tp); glDeleteShader(tv);
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex);
    if (image != EGL_NO_IMAGE) eglDestroyImage(d, image);
    if (bo) gbm_bo_destroy(bo);
    glDeleteProgram(p); glDeleteShader(v); glDeleteShader(f);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, ctx); eglTerminate(d);
    if (gbm) gbm_device_destroy(gbm);
    if (fd >= 0) close(fd);
    return 0;
}
