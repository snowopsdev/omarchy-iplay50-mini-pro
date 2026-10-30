/* Real Linux eventfd/pipe/thread/signal tests; no decoder/device operations. */
#include "mt6789-v4l2-bridge.c"
#include <assert.h>
#include <signal.h>
#include <dirent.h>
static pthread_t test_thread;
struct action { int kind, fd, extra; struct context *c; };
static void *act(void *arg) {
    struct action *a=arg;
    struct timespec pause={.tv_nsec=5000000}; nanosleep(&pause,NULL);
    if (a->kind == 0) assert(write(a->fd,"x",1)==1);
    else if (a->kind == 1) {
        pthread_mutex_lock(&mutex); a->c->cap.slots[0].queued=1;
        wake_view_waiters(a->c); pthread_mutex_unlock(&mutex);
    } else if (a->kind == 2) assert(pthread_kill(test_thread,SIGUSR1)==0);
    else if (a->kind == 3) { assert(close(a->fd)==0); assert(write(a->extra,"x",1)==1); }
    return NULL;
}
static unsigned count_fds(void) {
    DIR *d=opendir("/proc/self/fd"); assert(d); unsigned n=0;
    while (readdir(d)) n++;
    closedir(d); return n;
}
static void signal_handler(int signal) { (void)signal; }
int main(void) {
    enabled=1; decouple_capture=1; poll_backpressure=1;
    test_thread=pthread_self(); unsigned tests=0;
    int video=eventfd(1,EFD_NONBLOCK|EFD_CLOEXEC); assert(video>=0);
    struct context *c=&contexts[0]; *c=(struct context){.active=1,.fd=video};
    c->cap.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; c->cap.streaming=1; c->cap.count=1;
    context_wakes[0]=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC); assert(context_wakes[0]>=0);
    struct pollfd fds[2]={{.fd=video,.events=POLLIN}};
    int64_t begin=monotonic_ns();
    assert(poll(fds,1,100)==1 && fds[0].revents==POLLIN);
    assert(monotonic_ns()-begin>=15000000); tests++;
    assert(poll(fds,1,0)==1 && fds[0].revents==POLLIN); tests++;

    uint64_t value; assert(read(video,&value,sizeof(value))==8);
    begin=monotonic_ns(); assert(poll(fds,1,35)==0);
    assert(monotonic_ns()-begin>=30000000 && !fds[0].revents); tests++;
    int pipefd[2]; assert(pipe2(pipefd,O_CLOEXEC|O_NONBLOCK)==0);
    struct pollfd unrelated={.fd=pipefd[0],.events=POLLIN};
    begin=monotonic_ns(); assert(poll(&unrelated,1,25)==0);
    assert(monotonic_ns()-begin>=20000000); tests++;
    value=1; assert(write(video,&value,sizeof(value))==8);

    fds[1]=(struct pollfd){.fd=pipefd[0],.events=POLLIN};
    struct action action={.kind=0,.fd=pipefd[1]}; pthread_t worker;
    assert(pthread_create(&worker,NULL,act,&action)==0);
    assert(poll(fds,2,-1)==1 && fds[1].revents==POLLIN && !fds[0].revents); tests++;
    assert(pthread_join(worker,NULL)==0); char ch; assert(read(pipefd[0],&ch,1)==1);

    action=(struct action){.kind=1,.c=c};
    assert(pthread_create(&worker,NULL,act,&action)==0);
    begin=monotonic_ns(); assert(poll(fds,1,-1)==1 && fds[0].revents==POLLIN);
    assert(monotonic_ns()-begin<200000000 && c->cap.slots[0].queued); tests++;
    assert(pthread_join(worker,NULL)==0);
    c->cap.slots[0].queued=0;
    begin=monotonic_ns(); assert(poll(fds,1,100)==1);
    assert(monotonic_ns()-begin>=15000000); tests++; /* stale event drained */

    struct sigaction sa={.sa_handler=signal_handler}; sigemptyset(&sa.sa_mask);
    assert(sigaction(SIGUSR1,&sa,NULL)==0);
    action=(struct action){.kind=2};
    assert(pthread_create(&worker,NULL,act,&action)==0);
    fds[0].revents=0x5a5a;
    assert(poll(fds,1,-1)==-1 && errno==EINTR && fds[0].revents==0x5a5a); tests++;
    assert(pthread_join(worker,NULL)==0);

    unsigned before=count_fds();
    for (unsigned i=0;i<5;i++) assert(poll(fds,1,100)==1 && fds[0].revents==POLLIN);
    assert(count_fds()==before); tests++;
    int hup[2]; assert(pipe2(hup,O_CLOEXEC)==0); next_close(hup[1]);
    fds[1]=(struct pollfd){.fd=hup[0],.events=POLLIN};
    assert(poll(fds,2,-1)>=1 && (fds[1].revents&POLLHUP)); tests++;
    next_close(hup[0]);

    /* Closing a context never closes the waiter's duplicate. The caller's own
     * reset wakefd still returns, with original revents visible. */
    fds[1]=(struct pollfd){.fd=pipefd[0],.events=POLLIN};
    action=(struct action){.kind=3,.fd=video,.extra=pipefd[1]};
    assert(pthread_create(&worker,NULL,act,&action)==0);
    assert(poll(fds,2,-1)>=1 && (fds[1].revents&POLLIN)); tests++;
    assert(pthread_join(worker,NULL)==0);
    assert(!c->active && context_wakes[0]==-1); tests++;
    assert(read(pipefd[0],&ch,1)==1);
    next_close(pipefd[0]); next_close(pipefd[1]);
    printf("poll lifecycle/backpressure: %u checks passed\n",tests);
    return 0;
}
