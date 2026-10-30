#include <stdatomic.h>
#include <stdlib.h>
static _Atomic(int) required;
__attribute__((constructor)) static void initialize(void) {
 const char *p=getenv("TABLET_TEST_FINISH_POLICY");
 atomic_store(&required,p && p[0]=='1' && !p[1]);
}
int tablet_v4l2_capture_requires_finish(void) { return atomic_load(&required); }
void tablet_test_set_finish_policy(int value) { atomic_store(&required,value); }
