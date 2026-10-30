#define _GNU_SOURCE
#include <dlfcn.h>
int chain_value(void) { return 99; }
int chain_probe(void) {
 int (*next)(void)=(int (*)(void))dlsym(RTLD_NEXT,"chain_value");
 return next ? next() : -1;
}
