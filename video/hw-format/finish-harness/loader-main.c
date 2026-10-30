#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
typedef void (*proc)(void);
typedef proc (*get_proc_fn)(const char *);
int main(int argc,char **argv) {
 assert(argc>=2); int active=atoi(argv[1]);
 if (argc>3) {
  unsetenv("TABLET_CHROMIUM_GL_FINISH"); unsetenv("GALLIUM_DRIVER");
  unsetenv("LIBGL_ALWAYS_SOFTWARE");
 }
 int (*chain)(void)=(int (*)(void))dlsym(RTLD_DEFAULT,"chain_probe");
 assert(chain && chain()==42);
 void *providers[2]={dlopen("./provider-a.so",RTLD_NOW|RTLD_LOCAL),dlopen("./provider-b.so",RTLD_NOW|RTLD_LOCAL)};
 assert(providers[0] && providers[1]);
 for(int i=0;i<2;i++) {
  void *h=providers[i];
  int (*id)(void)=(int (*)(void))dlsym(h,"get_id"); assert(id && id()==i+1);
  proc flush=(proc)dlsym(h,"glFlush"); assert(flush); flush();
  get_proc_fn get=(get_proc_fn)dlsym(h,"eglGetProcAddress"); assert(get);
  flush=get("glFlush"); assert(flush); flush();
  assert(!get("unknown"));
  int (*f)(void)=(int (*)(void))dlsym(h,"get_flushes");
  int (*done)(void)=(int (*)(void))dlsym(h,"get_finishes");
  assert(f && done && f()==(active?0:2) && done()==(active?2:0));
  printf("provider=%d flush=%d finish=%d PASS\n",i+1,f(),done());
 }
 puts("RTLD_NEXT original caller chain A->B=42 PASS");
 void *local=dlopen("./local-caller.so",RTLD_NOW|RTLD_LOCAL); assert(local);
 int (*local_probe)(void)=(int (*)(void))dlsym(local,"local_default_probe");
 assert(local_probe && local_probe()==77);
 assert(!dlsym(RTLD_DEFAULT,"local_only_value"));
 puts("RTLD_DEFAULT local caller sees own dependency=77; main cannot see it PASS");
 int pipes[2],waiting=0; assert(pipe(pipes)==0);
 assert(write(pipes[1],"abc",3)==3);
 assert(ioctl(pipes[0],FIONREAD,&waiting)==0 && waiting==3);
 struct pollfd pfd={.fd=pipes[0],.events=POLLIN};
 assert(poll(&pfd,1,0)==1 && (pfd.revents&POLLIN));
 void *mem=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(mem!=MAP_FAILED && munmap(mem,4096)==0);
 assert(close(pipes[0])==0 && close(pipes[1])==0);
 puts("ioctl FIONREAD/poll/mmap/close forwarding PASS");
 /* Leave DSOs loaded: returned wrapper function pointers require providers to
  * remain loaded, as with ordinary driver entry pointers. */
 return 0;
}
