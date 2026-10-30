/* Offline role parser + pre-constructor syscall fallback checks. No tablet. */
#include "mt6789-v4l2-bridge.c"
#include <assert.h>
#include <sys/wait.h>
int main(void) {
    unsigned checks=0;
#define ROLE(value, expected) do { \
        const char data[]=value; \
        assert(gpu_role_from_cmdline(data,sizeof(data)-1)==expected); checks++; \
    } while (0)
    ROLE("chromium\0",0);
    ROLE("chromium\0--type=gpu-process\0",1);
    ROLE("chromium\0--type=renderer\0",0);
    ROLE("chromium\0--type=zygote\0",0);
    ROLE("chromium\0--type=utility\0",0);
    ROLE("chromium\0--type=gpu-process-other\0",0);
    ROLE("chromium\0prefix--type=gpu-process\0",0);
    ROLE("chromium\0--\0--type=gpu-process\0",0);
    ROLE("--type=gpu-process\0",0);
    ROLE("chromium\0--type=gpu-process\0--type=gpu-process\0",0);
    ROLE("chromium\0--type=renderer\0--type=gpu-process\0",0);
    ROLE("chromium\0--type\0gpu-process\0",0);
    ROLE("chromium\0--type=gpu-process",0);
    assert(!gpu_role_from_cmdline(NULL,0)); checks++;
    assert(!process_policy_allows("invalid")); checks++;
    assert(!process_policy_allows("")); checks++;
    assert(process_policy_allows(NULL)); checks++;

    unsigned headroom=99;
    assert(parse_capture_headroom(NULL,&headroom) && headroom==0); checks++;
    assert(parse_capture_headroom("0",&headroom) && headroom==0); checks++;
    assert(parse_capture_headroom("08",&headroom) && headroom==8); checks++;
    assert(parse_capture_headroom("16",&headroom) && headroom==16); checks++;
    const char *invalid[]={"17","-1","+8",""," 8","8 ","9999999999999999999999999"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        assert(!parse_capture_headroom(invalid[i],&headroom)); checks++;
    }

    /* Explicitly emulate calls made before this library's constructor resolves
     * RTLD_NEXT. Do not use dlsym from the fallback paths. */
    int (*saved_poll)(struct pollfd *,nfds_t,int)=next_poll;
    int (*saved_close)(int)=next_close;
    int (*saved_ioctl)(int,unsigned long,...)=next_ioctl;
    void *(*saved_mmap)(void *,size_t,int,int,int,off_t)=next_mmap;
    void *(*saved_mmap64)(void *,size_t,int,int,int,off64_t)=next_mmap64;
    enabled=0; next_mmap=NULL; next_mmap64=NULL;
    next_poll=NULL; assert(poll(NULL,0,0)==0); checks++;
    void *a=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(a!=MAP_FAILED); memset(a,42,4096); assert(munmap(a,4096)==0); checks++;
    a=mmap64(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(a!=MAP_FAILED); memset(a,43,4096); assert(munmap(a,4096)==0); checks++;
    int fd=open("/dev/null",O_RDONLY|O_CLOEXEC); assert(fd>=0);
    next_close=NULL; assert(close(fd)==0); checks++;
    next_ioctl=NULL; struct v4l2_capability caps={0};
    assert(ioctl(-1,VIDIOC_QUERYCAP,&caps)==-1 && errno==EBADF); checks++;
    enabled=1;
    assert(poll(NULL,0,0)==-1 && errno==ENOSYS); checks++;
    assert(mmap(NULL,4096,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED && errno==ENOSYS); checks++;
    assert(close(-1)==-1 && errno==ENOSYS); checks++;
    assert(ioctl(-1,VIDIOC_QUERYCAP,&caps)==-1 && errno==ENOSYS); checks++;
    enabled=0; next_close=saved_close; next_ioctl=saved_ioctl;
    next_mmap=saved_mmap; next_mmap64=saved_mmap64; next_poll=saved_poll;
    assert(pthread_atfork(fork_prepare,fork_parent,fork_child)==0);
    heap_fd=memfd_create("mock-private-gpu-heap",MFD_CLOEXEC); assert(heap_fd>=0);
    int parent_heap=heap_fd; enabled=1; gpu_process_policy=1;
    pid_t child=fork(); assert(child>=0);
    if (!child) _exit(heap_fd==-1 && fcntl(parent_heap,F_GETFD)==-1 && errno==EBADF ? 0 : 1);
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0); checks++;
    assert(heap_fd==parent_heap && fcntl(parent_heap,F_GETFD)>=0); checks++;
    next_close(heap_fd); heap_fd=-1; enabled=0; gpu_process_policy=0;
    printf("role parser + early fallback: %u checks passed\n",checks);
    return 0;
}
