/* Atomic GL finish-policy lifecycle: real public wrappers, mock kernel only. */
#define main baseline_contract_main
#include "bridge-host-test.c"
#undef main
static unsigned dma_calls, fail_dma_on;
static int unset_metadata;
static int finish_ioctl(int fd,unsigned long request,...) {
    va_list ap; va_start(ap,request); void *arg=va_arg(ap,void *); va_end(ap);
    if ((uint32_t)request==DMA_HEAP_IOCTL_ALLOC && ++dma_calls==fail_dma_on)
        return fail(ENOMEM);
    if ((uint32_t)request==VIDIOC_DQEVENT) {
        struct v4l2_event *e=arg;
        e->type=V4L2_EVENT_SOURCE_CHANGE;
        e->u.src_change.changes=V4L2_EVENT_SRC_CH_RESOLUTION; return 0;
    }
    if ((uint32_t)request==VIDIOC_QUERY_EXT_CTRL) {
        struct v4l2_query_ext_ctrl *q=arg;
        q->type=V4L2_CTRL_TYPE_U32; q->elem_size=4; q->elems=17;
        q->nr_of_dims=1; q->dims[0]=17;
        q->flags=V4L2_CTRL_FLAG_READ_ONLY|V4L2_CTRL_FLAG_VOLATILE; return 0;
    }
    if ((uint32_t)request==VIDIOC_G_EXT_CTRLS) {
        struct v4l2_ext_controls *q=arg;
        struct tablet_mtk_color_desc c={.color_primaries=1,
            .transfer_characteristics=1,.matrix_coefficients=1,.full_range=0};
        if (unset_metadata) c.matrix_coefficients=UINT32_MAX;
        memcpy(q->controls[0].ptr,&c,sizeof(c)); return 0;
    }
    return fake_ioctl(fd,request,arg);
}
static int open_context(void) {
    int fd=open("/dev/null",O_RDWR|O_NONBLOCK|O_CLOEXEC); assert(fd>=0);
    struct v4l2_capability caps={0}; assert(ioctl(fd,VIDIOC_QUERYCAP,&caps)==0);
    assert(find_context(fd)); return fd;
}
static int capture_count(int fd,unsigned count) {
    struct v4l2_requestbuffers r={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
        .memory=V4L2_MEMORY_MMAP,.count=count};
    return ioctl(fd,VIDIOC_REQBUFS,&r);
}
static void prepare_rgb(int fd,uint32_t fourcc) {
    request_argb=1; rgb_view_fourcc=fourcc;
    struct v4l2_requestbuffers r={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
        .memory=V4L2_MEMORY_MMAP,.count=1};
    assert(ioctl(fd,VIDIOC_REQBUFS,&r)==0);
    struct context *c=find_context(fd); memcpy(c->out.slots[0].view.ptr,"TEST",4);
    struct v4l2_plane p={.bytesused=4};
    struct v4l2_buffer b={.type=r.type,.memory=r.memory,.length=1,.m.planes=&p};
    assert(ioctl(fd,VIDIOC_QBUF,&b)==0);
    struct v4l2_event e={0}; assert(ioctl(fd,VIDIOC_DQEVENT,&e)==0);
    struct v4l2_format f={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
    assert(ioctl(fd,VIDIOC_G_FMT,&f)==0);
    assert(f.fmt.pix_mp.pixelformat==(unset_metadata ? V4L2_PIX_FMT_NV12 : fourcc));
}
int main(void) {
    alarm(5);
    next_ioctl=finish_ioctl; next_close=dlsym(RTLD_NEXT,"close");
    next_mmap=dlsym(RTLD_NEXT,"mmap"); next_mmap64=dlsym(RTLD_NEXT,"mmap64");
    enabled=1; heap_fd=100000; unsigned checks=0;
    assert(atomic_is_lock_free(&finish_context_mask)); checks++;
    assert(tablet_v4l2_capture_requires_finish()==0); checks++;
    int a=open_context();
    assert(tablet_v4l2_capture_requires_finish()==0); checks++;
    assert(capture_count(a,2)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    unsigned cap=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    assert(ioctl(a,VIDIOC_STREAMON,&cap)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    assert(ioctl(a,VIDIOC_STREAMOFF,&cap)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    pthread_mutex_lock(&mutex);
    errno=EDOM; assert(tablet_v4l2_capture_requires_finish()==1 && errno==EDOM); checks++;
    pthread_mutex_unlock(&mutex);
    assert(capture_count(a,0)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    assert(close(a)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;

    int b=open_context(); prepare_rgb(b,V4L2_PIX_FMT_ABGR32);
    assert(tablet_v4l2_capture_requires_finish()==0); checks++;
    assert(capture_count(b,2)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    assert(close(b)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    b=open_context(); prepare_rgb(b,V4L2_PIX_FMT_BGR32);
    assert(capture_count(b,2)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    int c=open_context(); prepare_rgb(c,V4L2_PIX_FMT_BGR32);
    assert(capture_count(c,2)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    assert(close(c)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;

    a=open_context(); request_argb=0;
    assert(capture_count(a,2)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    assert(close(b)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    assert(capture_count(a,0)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    fail_dma_on=dma_calls+2; /* Native succeeds, view fails: complete rollback. */
    assert(capture_count(a,2)==-1 && errno==ENOMEM &&
           find_context(a)->cap.count==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    fail_dma_on=0;
    assert(capture_count(a,2)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    assert(pthread_atfork(fork_prepare,fork_parent,fork_child)==0);
    pid_t child=fork(); assert(child>=0);
    if (!child) _exit(tablet_v4l2_capture_requires_finish()==0 &&
                      find_context(a)->revoked ? 0 : 1);
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0); checks++;
    assert(tablet_v4l2_capture_requires_finish()==1); checks++;
    assert(close(a)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;

    c=open_context(); unset_metadata=1; prepare_rgb(c,V4L2_PIX_FMT_BGR32);
    assert(capture_count(c,2)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    assert(ioctl(c,VIDIOC_STREAMOFF,&cap)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    assert(capture_count(c,0)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    assert(capture_count(c,2)==0 && tablet_v4l2_capture_requires_finish()==1); checks++;
    pthread_mutex_lock(&mutex); find_context(c)->revoked=1; pthread_mutex_unlock(&mutex);
    struct v4l2_format f={.type=cap};
    assert(ioctl(c,VIDIOC_G_FMT,&f)==-1 && errno==EOWNERDEAD &&
           tablet_v4l2_capture_requires_finish()==0); checks++;
    assert(close(c)==0 && tablet_v4l2_capture_requires_finish()==0); checks++;
    alarm(0);
    printf("atomic finish-policy lifecycle: %u checks passed\n",checks);
    return 0;
}
