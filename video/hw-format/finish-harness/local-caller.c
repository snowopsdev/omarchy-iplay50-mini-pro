#define _GNU_SOURCE
#include <dlfcn.h>
extern int local_only_value(void);
int local_default_probe(void) {
 int (*value)(void)=(int (*)(void))dlsym(RTLD_DEFAULT,"local_only_value");
 return value ? value() : -1;
}
int force_needed_reference(void) { return local_only_value(); }
