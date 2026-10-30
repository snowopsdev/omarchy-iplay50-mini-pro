/* Fortified poll ABI contract; no decoder or tablet operations. */
#include "mt6789-v4l2-bridge.c"
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>
static unsigned original_calls;
static struct pollfd *original_pointer;
static nfds_t original_count;
static size_t original_bytes;
static int mock_original(struct pollfd *fds,nfds_t count,int timeout,size_t bytes) {
    (void)timeout; original_calls++; original_pointer=fds;
    original_count=count; original_bytes=bytes; errno=EOVERFLOW; return -1;
}
static void abort_seen(int signal) { (void)signal; _exit(77); }
int main(void) {
    unsigned tests=0;
    int (*original)(struct pollfd *,nfds_t,int,size_t)=next_poll_chk;
    assert(original);
    int fd=eventfd(1,EFD_NONBLOCK|EFD_CLOEXEC); assert(fd>=0);
    struct pollfd f={.fd=fd,.events=POLLIN};
    assert(__poll_chk(&f,1,0,sizeof(f))==1 && f.revents==POLLIN); tests++;
    assert(__poll_chk(NULL,0,0,0)==0); tests++;
    assert(__poll_chk(NULL,1,0,sizeof(f))==-1 && errno==EFAULT); tests++;
    next_poll_chk=mock_original;
    struct pollfd *invalid=(struct pollfd *)(uintptr_t)1;
    assert(__poll_chk(invalid,2,0,sizeof(f))==-1 && errno==EOVERFLOW); tests++;
    assert(original_calls==1 && original_pointer==invalid && original_count==2 && original_bytes==sizeof(f)); tests++;
    assert(__poll_chk(invalid,(nfds_t)SIZE_MAX,0,SIZE_MAX)==-1 && original_calls==2); tests++;
    assert(__poll_chk(&f,1,0,sizeof(f))==1 && original_calls==2); tests++;
    assert(__poll_chk(invalid,0,0,0)==0 && original_calls==2); tests++;
    next_poll_chk=original;

    enabled=1; poll_backpressure=1; decouple_capture=1;
    struct context *c=&contexts[0]; *c=(struct context){.active=1,.fd=fd};
    c->cap.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    c->cap.count=1; c->cap.streaming=1;
    context_wakes[0]=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC); assert(context_wakes[0]>=0);
    int64_t begin=monotonic_ns();
    assert(__poll_chk(&f,1,100,sizeof(f))==1 && f.revents==POLLIN);
    assert(monotonic_ns()-begin>=15000000); tests++;
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        signal(SIGABRT,abort_seen);
        int quiet=open("/dev/null",O_WRONLY|O_CLOEXEC);
        if (quiet>=0) { dup2(quiet,STDERR_FILENO); next_close(quiet); }
        (void)__poll_chk(invalid,2,0,sizeof(f)); _exit(1);
    }
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==77); tests++;
    assert(close(fd)==0); tests++;
    printf("fortified poll ABI: %u checks passed\n",tests);
    return 0;
}
