#include <string.h>
#ifndef PROVIDER_ID
#error PROVIDER_ID missing
#endif
typedef void (*proc)(void);
static int flushes,finishes;
static proc reentry;
static void run_reentry(void) { proc callback=reentry; reentry=0; if(callback)callback(); }
static void provider_flush(void) { ++flushes; run_reentry(); }
static void provider_finish(void) { ++finishes; run_reentry(); }
void set_reentry(proc callback) { reentry=callback; }
void glFlush(void) { provider_flush(); }
void glFinish(void) { provider_finish(); }
proc eglGetProcAddress(const char *name) {
 if (!strcmp(name,"glFlush")) return provider_flush;
 if (!strcmp(name,"glFinish")) return provider_finish;
 return 0;
}
int get_id(void) { return PROVIDER_ID; }
int get_flushes(void) { return flushes; }
int get_finishes(void) { return finishes; }
