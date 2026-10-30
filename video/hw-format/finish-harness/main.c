#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef void (*proc)(void);
extern void glFlush(void);
extern unsigned int eglSwapBuffers(void *,void *);
extern proc eglGetProcAddress(const char *);
extern int get_flushes(void),get_finishes(void),get_swaps(void);
int main(int argc,char **argv) {
 assert(argc>=2);
 int active=atoi(argv[1]);
 glFlush();
 proc flush=eglGetProcAddress("glFlush"); assert(flush); flush();
 assert(eglGetProcAddress("glFinish"));
 assert(eglSwapBuffers(0,0)==17);
 unsigned int (*swap)(void *,void *)=(unsigned int (*)(void *,void *))eglGetProcAddress("eglSwapBuffers");
 assert(swap && swap(0,0)==17);
 assert(!eglGetProcAddress("unknown"));
 assert(get_flushes()==(active ? 0 : 2));
 assert(get_finishes()==(active ? 4 : 0));
 assert(get_swaps()==2);
 printf("active=%d flush=%d finish=%d swap=%d forwarding=PASS\n",active,get_flushes(),get_finishes(),get_swaps());
}
