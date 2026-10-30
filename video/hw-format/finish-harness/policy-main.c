#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
typedef void (*proc)(void);
typedef proc (*get_proc_fn)(const char *);
static void (*set_policy)(int);
static proc nested_flush;
static int next_policy;
static void nested(void) { set_policy(next_policy); nested_flush(); }
int main(void) {
 set_policy=(void (*)(int))dlsym(RTLD_DEFAULT,"tablet_test_set_finish_policy");
 assert(set_policy);
 void *handles[2]={dlopen("./provider-a.so",RTLD_NOW|RTLD_LOCAL),dlopen("./provider-b.so",RTLD_NOW|RTLD_LOCAL)};
 assert(handles[0] && handles[1]);
 for(int i=0;i<2;i++) {
  void *h=handles[i];
  proc direct=(proc)dlsym(h,"glFlush");
  get_proc_fn get=(get_proc_fn)dlsym(h,"eglGetProcAddress");
  proc resolved=get("glFlush");
  int (*flushes)(void)=(int (*)(void))dlsym(h,"get_flushes");
  int (*finishes)(void)=(int (*)(void))dlsym(h,"get_finishes");
  void (*set_reentry)(proc)=(void (*)(proc))dlsym(h,"set_reentry");
  assert(direct && resolved && flushes && finishes && set_reentry);
  set_policy(0);direct();assert(flushes()==1 && finishes()==0);
  set_policy(1);direct();assert(flushes()==1 && finishes()==1);
  set_policy(0);resolved();assert(flushes()==2 && finishes()==1);
  set_policy(1);resolved();assert(flushes()==2 && finishes()==2);
  /* A policy change during downstream GL execution must not recursively
   * upgrade an inner flush into finish. Both normal and finish paths guard. */
  nested_flush=resolved;
  next_policy=1;set_policy(0);set_reentry(nested);direct();
  assert(flushes()==4 && finishes()==2);
  next_policy=0;set_policy(1);set_reentry(nested);direct();
  assert(flushes()==5 && finishes()==3);
  printf("provider=%d dynamic 0->1->0->1 and nested flush/finish PASS\n",i+1);
 }
 return 0;
}
