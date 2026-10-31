/* LD_PRELOAD for GPU applications on Wayland (gpu-app-env).
 *
 * The tablet has no DRM render node, so Mesa EGL's hardware Wayland path
 * fails and falls back to its software path. That path uses Zink's Vulkan
 * swapchain ("kopper", which works on PanVK) only when LIBGL_ALWAYS_SOFTWARE
 * and MESA_LOADER_DRIVER_OVERRIDE=zink are both set; otherwise it copies
 * frames into wl_shm buffers, and from Zink those arrive black. But Zink also
 * reads LIBGL_ALWAYS_SOFTWARE and then insists on a CPU Vulkan device.
 *
 * This hides LIBGL_ALWAYS_SOFTWARE from code in libgallium (where only Zink's
 * device choice and a Windows-only d3d12 check read it), so EGL picks kopper
 * and Zink still picks the Mali GPU. Every other caller sees the real value.
 *
 * Build: cc -O2 -fPIC -shared -Wall -Wextra zink-kopper-hw.c -ldl -o zink-kopper-hw.so */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

static int from_gallium(const void *ret)
{
   Dl_info info;
   if (!dladdr(ret, &info) || !info.dli_fname)
      return 0;
   const char *base = strrchr(info.dli_fname, '/');
   base = base ? base + 1 : info.dli_fname;
   return !strncmp(base, "libgallium", 10);
}

static char *(*real_getenv)(const char *);
static char *(*real_secure_getenv)(const char *);

char *getenv(const char *name)
{
   if (!real_getenv)
      real_getenv = (char *(*)(const char *))dlsym(RTLD_NEXT, "getenv");
   if (!strcmp(name, "LIBGL_ALWAYS_SOFTWARE") &&
       from_gallium(__builtin_return_address(0)))
      return NULL;
   return real_getenv(name);
}

char *secure_getenv(const char *name)
{
   if (!real_secure_getenv)
      real_secure_getenv = (char *(*)(const char *))dlsym(RTLD_NEXT, "secure_getenv");
   if (!strcmp(name, "LIBGL_ALWAYS_SOFTWARE") &&
       from_gallium(__builtin_return_address(0)))
      return NULL;
   return real_secure_getenv(name);
}
