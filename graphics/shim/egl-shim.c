/*
 * egl-shim.so - LD_PRELOAD for running unmodified Hyprland 0.56.x on Arm libmali r32 (which lacks EGL_KHR_debug).
 * Hyprland aborts at start if eglGetProcAddress("eglDebugMessageControlKHR") returns NULL.
 * This interposes eglGetProcAddress and returns a harmless stub for that one name; everything else is forwarded.
 * Built freestanding (no libc headers): dlsym is resolved at run time from glibc.
 */
extern void *dlsym(void *handle, const char *symbol);
#define RTLD_NEXT ((void *)-1L)

static int seq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
static int stub_debug_message_control(void *callback, const long *attribs) { (void)callback; (void)attribs; return 0x3000; /* EGL_SUCCESS */ }

/* r32 predates GBM's flags-aware modifier API. Preserve requested usage flags
 * with gbm_bo_create, then accept only a modifier the caller allowed. */
struct gbm_device;
struct gbm_bo;
extern struct gbm_bo *gbm_bo_create(struct gbm_device *, unsigned, unsigned, unsigned, unsigned);
extern struct gbm_bo *gbm_bo_create_with_modifiers(struct gbm_device *, unsigned, unsigned, unsigned, const unsigned long long *, unsigned);
extern unsigned long long gbm_bo_get_modifier(struct gbm_bo *);
extern void gbm_bo_destroy(struct gbm_bo *);
extern int *__errno_location(void);

__attribute__((visibility("default")))
struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *dev, unsigned w,
        unsigned h, unsigned format, const unsigned long long *mods,
        unsigned count, unsigned flags)
{
        struct gbm_bo *bo;
        unsigned i;
        unsigned long long modifier;
        if (!mods || !count) {
                *__errno_location() = 22;
                return 0;
        }
        if (!flags)
                return gbm_bo_create_with_modifiers(dev, w, h, format, mods, count);
        bo = gbm_bo_create(dev, w, h, format, flags);
        if (!bo)
                return 0;
        modifier = gbm_bo_get_modifier(bo);
        for (i = 0; i < count; ++i)
                if (mods[i] == modifier || mods[i] == 0x00ffffffffffffffULL)
                        return bo;
        gbm_bo_destroy(bo);
        *__errno_location() = 22;
        return 0;
}

__attribute__((visibility("default")))
void *eglGetProcAddress(const char *name)
{
	static void *(*real)(const char *);
	if (!real)
		real = (void *(*)(const char *))dlsym(RTLD_NEXT, "eglGetProcAddress");
	void *p = real ? real(name) : 0;
	if (!p && name && seq(name, "eglDebugMessageControlKHR"))
		return (void *)stub_debug_message_control;
	return p;
}
