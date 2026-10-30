#include <string.h>
typedef void (*proc)(void);
static int flushes, finishes, swaps;
static void private_flush(void) { ++flushes; }
static void private_finish(void) { ++finishes; }
static unsigned int private_swap(void *d, void *s) { (void)d; (void)s; ++swaps; return 17; }
void glFlush(void) { private_flush(); }
void glFinish(void) { private_finish(); }
unsigned int eglSwapBuffers(void *d, void *s) { return private_swap(d,s); }
proc eglGetProcAddress(const char *name) {
 if (!strcmp(name,"glFlush")) return private_flush;
 if (!strcmp(name,"glFinish")) return private_finish;
 if (!strcmp(name,"eglSwapBuffers")) return (proc)private_swap;
 return 0;
}
int get_flushes(void) { return flushes; }
int get_finishes(void) { return finishes; }
int get_swaps(void) { return swaps; }
