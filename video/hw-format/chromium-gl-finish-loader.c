/* GPU-only diagnostic: dlsym-aware GL flush -> same-provider real GL finish.
 * Build -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror -ldl -pthread.
 * Opt-in env and --type=gpu-process selection match chromium-gl-finish.c.
 * RTLD_NEXT and RTLD_DEFAULT pass through an assembly tail trampoline, preserving the
 * ORIGINAL caller return address used by glibc's caller-relative lookup.
 * No fence APIs or GL/EGL return values are fabricated.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Optional bridge export is lock-free. Missing export keeps the original
 * standalone real-finish behavior; allocated NV12 requests completion while
 * RGB-only/no-context state uses ordinary same-provider flushing. */
extern int tablet_v4l2_capture_requires_finish(void) __attribute__((weak));
typedef void (*gl_fn)(void);
typedef gl_fn (*get_proc_fn)(const char *);
typedef void *(*dlsym_fn)(void *,const char *);
static _Atomic(dlsym_fn) real_lookup;
static pthread_once_t select_once=PTHREAD_ONCE_INIT;
static pthread_mutex_t slots_lock=PTHREAD_MUTEX_INITIALIZER;
static int selected, opt_in_at_load;
/* TABLET_CHROMIUM_GL_FINISH_ALWAYS=1 (cached at load): always real glFinish.
 * With the RGB-only flush shortcut, Chromium handed the compositor llvmpipe
 * frames before rasterisation finished; unfinished 64x64 tiles showed as
 * staircase-shaped holes while pages loaded (2026-10-29). */
static int always_finish;
static _Atomic(unsigned long) lookup_calls;
static _Thread_local int completing;
static _Thread_local int resolving;
#define SLOT_COUNT 8
struct slot {
    get_proc_fn provider;
    void *handle;
    int direct;
    _Atomic(gl_fn) flush,finish;
    _Atomic(unsigned long) calls;
};
static struct slot slots[SLOT_COUNT];
static unsigned used;
static int env_is(const char *key,const char *value) {
    const char *p=getenv(key); return p && !strcmp(p,value);
}
static void select_process(void) {
    if (!env_is("TABLET_CHROMIUM_GL_FINISH","1") ||
        !env_is("GALLIUM_DRIVER","llvmpipe") ||
        !env_is("LIBGL_ALWAYS_SOFTWARE","1")) return;
    char data[32768];
    int fd=open("/proc/self/cmdline",O_RDONLY|O_CLOEXEC);
    if (fd<0) return;
    ssize_t count=read(fd,data,sizeof(data)-1); close(fd);
    if (count<=0) return;
    data[count]=0;
    for (size_t p=0;p<(size_t)count;) {
        size_t n=strnlen(data+p,(size_t)count-p);
        if (!strcmp(data+p,"--type=gpu-process")) selected=1;
        p+=n+1;
    }
    if (selected) fprintf(stderr,"tablet-loader-finish pid=%ld selected llvmpipe GPU\n",(long)getpid());
}
/* Chromium may sanitize its environment after library constructors. Cache
 * the authorized diagnostic selection while the preload environment exists. */
__attribute__((constructor))
static void tablet_finish_constructor(void) {
    opt_in_at_load=env_is("TABLET_CHROMIUM_GL_FINISH","1");
    always_finish=env_is("TABLET_CHROMIUM_GL_FINISH_ALWAYS","1");
    pthread_once(&select_once,select_process);
    if (opt_in_at_load)
        fprintf(stderr,"tablet-loader-finish pid=%ld constructor opt-in=1 llvmpipe=%d software=%d selected=%d\n",
                (long)getpid(),env_is("GALLIUM_DRIVER","llvmpipe"),
                env_is("LIBGL_ALWAYS_SOFTWARE","1"),selected);
}
__attribute__((used,visibility("hidden")))
dlsym_fn tablet_get_real_dlsym(void) {
    dlsym_fn fn=atomic_load(&real_lookup);
    if (!fn) {
        if (resolving) _exit(127);
        resolving=1;
#if defined(__aarch64__)
        fn=(dlsym_fn)dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.17");
#elif defined(__x86_64__)
        fn=(dlsym_fn)dlvsym(RTLD_NEXT,"dlsym","GLIBC_2.2.5");
#else
#error Unsupported architecture: verify glibc baseline and caller-preserving trampoline
#endif
        resolving=0;
        if (!fn) _exit(127);
        atomic_store(&real_lookup,fn);
    }
    return fn;
}
static void wrapped_flush(unsigned index) {
    struct slot *s=&slots[index];
    gl_fn flush=atomic_load(&s->flush),finish=atomic_load(&s->finish);
    if (!flush || !finish) abort();
    if (completing) { flush(); return; }
    int needs_finish=always_finish || !tablet_v4l2_capture_requires_finish ||
                     tablet_v4l2_capture_requires_finish();
    unsigned long n=atomic_fetch_add(&s->calls,1)+1;
    if (n<=3 || n%300==0)
        fprintf(stderr,"tablet-loader-finish pid=%ld slot=%u direct=%d real-%s count=%lu\n",
                (long)getpid(),index,s->direct,needs_finish ? "finish" : "flush",n);
    completing=1;
    if (needs_finish) finish();
    else flush();
    completing=0;
}
static gl_fn wrapped_get_proc(unsigned index,const char *name);
#define WRAPPERS(N) \
static void flush_##N(void) { wrapped_flush(N); } \
static gl_fn get_##N(const char *name) { return wrapped_get_proc(N,name); }
WRAPPERS(0) WRAPPERS(1) WRAPPERS(2) WRAPPERS(3)
WRAPPERS(4) WRAPPERS(5) WRAPPERS(6) WRAPPERS(7)
static const gl_fn flush_wrappers[SLOT_COUNT]={flush_0,flush_1,flush_2,flush_3,flush_4,flush_5,flush_6,flush_7};
static const get_proc_fn get_wrappers[SLOT_COUNT]={get_0,get_1,get_2,get_3,get_4,get_5,get_6,get_7};
static gl_fn wrapped_get_proc(unsigned index,const char *name) {
    struct slot *s=&slots[index];
    gl_fn real=s->provider(name);
    if (!real || !name || strcmp(name,"glFlush")) return real;
    /* Both functions must come from this exact EGL implementation. */
    gl_fn finish=s->provider("glFinish");
    if (!finish) return real;
    atomic_store(&s->flush,real); atomic_store(&s->finish,finish);
    unsigned long n=atomic_load(&s->calls);
    if (!n) fprintf(stderr,"tablet-loader-finish pid=%ld slot=%u intercepted EGL glFlush\n",(long)getpid(),index);
    return flush_wrappers[index];
}
__attribute__((used,visibility("hidden")))
void *tablet_intercept_dlsym(void *handle,const char *name,dlsym_fn lookup) {
    /* Caller-relative pseudo-handles NEVER reach here: preserve real caller. */
    void *real=lookup(handle,name);
    if (opt_in_at_load && selected) {
        unsigned long n=atomic_fetch_add(&lookup_calls,1)+1;
        if (n<=3)
            fprintf(stderr,"tablet-loader-finish pid=%ld explicit dlsym count=%lu name=%s\n",
                    (long)getpid(),n,name ? name : "(null)");
    }
    if (!real || !name || (strcmp(name,"eglGetProcAddress") && strcmp(name,"glFlush"))) return real;
    pthread_once(&select_once,select_process);
    if (!selected) return real;
    int direct=!strcmp(name,"glFlush");
    gl_fn finish=NULL;
    if (direct) {
        /* Explicit handle, same provider. Reject existing wrappers below. */
        finish=(gl_fn)lookup(handle,"glFinish");
        if (!finish) return real;
    }
    for (unsigned i=0;i<SLOT_COUNT;i++) {
        if (real==(void *)flush_wrappers[i] || real==(void *)get_wrappers[i] ||
            finish==flush_wrappers[i]) return real;
    }
    pthread_mutex_lock(&slots_lock);
    unsigned index=0;
    for (;index<used;index++) {
        struct slot *s=&slots[index];
        if (s->direct==direct &&
            (direct ? (s->handle==handle && atomic_load(&s->flush)==(gl_fn)real)
                    : (s->provider==(get_proc_fn)real))) break;
    }
    if (index==used) {
        if (used==SLOT_COUNT) { pthread_mutex_unlock(&slots_lock); return real; }
        struct slot *s=&slots[index];
        s->direct=direct; s->handle=handle;
        if (direct) {
            atomic_store(&s->flush,(gl_fn)real); atomic_store(&s->finish,finish);
        } else s->provider=(get_proc_fn)real;
        ++used;
        fprintf(stderr,"tablet-loader-finish pid=%ld registered slot=%u direct=%d\n",(long)getpid(),index,direct);
    }
    pthread_mutex_unlock(&slots_lock);
    return direct ? (void *)flush_wrappers[index] : (void *)get_wrappers[index];
}
#if defined(__x86_64__)
__asm__(".text\n.globl dlsym\n.type dlsym,@function\n"
"dlsym:\n"
"push %rdi\n push %rsi\n sub $8,%rsp\n"
"call tablet_get_real_dlsym\n"
"add $8,%rsp\n pop %rsi\n pop %rdi\n"
"test %rdi,%rdi\n je 1f\n cmp $-1,%rdi\n je 1f\n"
"mov %rax,%rdx\n jmp tablet_intercept_dlsym\n"
"1: jmp *%rax\n.size dlsym,.-dlsym\n");
#elif defined(__aarch64__)
__asm__(".text\n.globl dlsym\n.type dlsym,%function\n"
"dlsym:\n"
"stp x29,x30,[sp,#-32]!\n stp x0,x1,[sp,#16]\n"
"bl tablet_get_real_dlsym\n mov x2,x0\n"
"ldp x0,x1,[sp,#16]\n ldp x29,x30,[sp],#32\n"
"cbz x0,1f\n cmn x0,#1\n b.eq 1f\n"
"b tablet_intercept_dlsym\n"
"1: br x2\n.size dlsym,.-dlsym\n");
#endif
