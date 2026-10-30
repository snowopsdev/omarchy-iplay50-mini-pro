/* Deferred START/LAST recycling contract; real bridge, mock kernel only. */
#define main baseline_contract_main
#include "bridge-host-test.c"
#undef main
static unsigned capture_submits,output_submits,dma_calls,fail_dma_on;
static int reject_output;
static char submit_order[128]; static unsigned order_length;
static int resume_ioctl(int fd,unsigned long request,...) {
    va_list ap; va_start(ap,request); void *arg=va_arg(ap,void *); va_end(ap);
    if ((uint32_t)request==DMA_HEAP_IOCTL_ALLOC && ++dma_calls==fail_dma_on)
        return fail(ENOMEM);
    if ((uint32_t)request==VIDIOC_QBUF) {
        struct v4l2_buffer *b=arg;
        if (b->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE && reject_output)
            return fail(EINVAL);
        if (b->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            capture_submits++; submit_order[order_length++]='C';
        } else { output_submits++; submit_order[order_length++]='O'; }
    }
    return fake_ioctl(fd,request,arg);
}
int main(void) {
    next_ioctl=resume_ioctl; next_close=dlsym(RTLD_NEXT,"close");
    next_mmap=dlsym(RTLD_NEXT,"mmap"); next_mmap64=dlsym(RTLD_NEXT,"mmap64");
    enabled=1; decouple_capture=1; heap_fd=100000;
    int fd=open("/dev/null",O_RDWR|O_NONBLOCK|O_CLOEXEC); assert(fd>=0);
    struct context *c=&contexts[0]; *c=(struct context){.active=1,.fd=fd};
    c->out.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    c->cap.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    unsigned checks=0;
    struct v4l2_requestbuffers r={.type=c->out.type,.memory=V4L2_MEMORY_MMAP,.count=2};
    assert(ioctl(fd,VIDIOC_REQBUFS,&r)==0);
    r.type=c->cap.type; assert(ioctl(fd,VIDIOC_REQBUFS,&r)==0);
    assert(tablet_v4l2_capture_requires_finish()==1); checks++;
    unsigned type=c->cap.type;
    assert(ioctl(fd,VIDIOC_STREAMON,&type)==0);
    struct v4l2_plane p={0};
    struct v4l2_buffer cap={.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.length=1,.m.planes=&p};
    assert(ioctl(fd,VIDIOC_QBUF,&cap)==0 && capture_submits==2); checks++;
    fake_capture_last=1;
    assert(ioctl(fd,VIDIOC_DQBUF,&cap)==0 && (cap.flags&V4L2_BUF_FLAG_LAST) &&
           c->capture_drained && !view_policy(c)->resume_pending); checks++;
    struct v4l2_decoder_cmd start={.cmd=V4L2_DEC_CMD_START};
    assert(ioctl(fd,VIDIOC_DECODER_CMD,&start)==0 &&
           view_policy(c)->resume_pending && c->capture_drained && capture_submits==2); checks++;
    cap.index=1; cap.flags=0; p=(struct v4l2_plane){0};
    assert(ioctl(fd,VIDIOC_QBUF,&cap)==0 && capture_submits==2); checks++;
    struct v4l2_plane op={.bytesused=4};
    struct v4l2_buffer out={.type=c->out.type,.memory=V4L2_MEMORY_MMAP,.length=1,.m.planes=&op};
    reject_output=1;
    assert(ioctl(fd,VIDIOC_QBUF,&out)==-1 && errno==EINVAL &&
           view_policy(c)->resume_pending && c->capture_drained && capture_submits==2); checks++;
    reject_output=0; op.bytesused=0;
    assert(ioctl(fd,VIDIOC_QBUF,&out)==0 && capture_submits==2 &&
           view_policy(c)->resume_pending); checks++;
    assert(ioctl(fd,VIDIOC_DQBUF,&out)==0);
    op=(struct v4l2_plane){.bytesused=4}; out.m.planes=&op;
    unsigned before=order_length;
    assert(ioctl(fd,VIDIOC_QBUF,&out)==0 && !c->capture_drained &&
           !view_policy(c)->resume_pending && capture_submits==3 &&
           submit_order[before]=='O' && submit_order[before+1]=='C'); checks++;
    assert(ioctl(fd,VIDIOC_DECODER_CMD,&start)==0 && capture_submits==3); checks++;
    assert(ioctl(fd,VIDIOC_DQBUF,&cap)==0 && c->capture_drained);
    assert(ioctl(fd,VIDIOC_DECODER_CMD,&start)==0 && view_policy(c)->resume_pending);
    assert(ioctl(fd,VIDIOC_STREAMOFF,&type)==0 && capture_submits==3 &&
           !view_policy(c)->resume_pending && !c->capture_drained &&
           tablet_v4l2_capture_requires_finish()==1); checks++;
    type=c->out.type;
    assert(ioctl(fd,VIDIOC_STREAMOFF,&type)==0 && capture_submits==3); checks++;
    r.type=c->cap.type; r.count=0;
    assert(ioctl(fd,VIDIOC_REQBUFS,&r)==0 && !view_policy(c)->resume_pending &&
           tablet_v4l2_capture_requires_finish()==0); checks++;
    view_policy(c)->resume_pending=1; c->capture_drained=1;
    r.count=2; fail_dma_on=dma_calls+2;
    assert(ioctl(fd,VIDIOC_REQBUFS,&r)==-1 && errno==ENOMEM &&
           !c->cap.count && !view_policy(c)->resume_pending &&
           tablet_v4l2_capture_requires_finish()==0); checks++;
    fail_dma_on=0;
    r.count=2; assert(ioctl(fd,VIDIOC_REQBUFS,&r)==0);
    view_policy(c)->resume_pending=1; c->capture_drained=1;
    r.count=0; assert(ioctl(fd,VIDIOC_REQBUFS,&r)==0 &&
                       !view_policy(c)->resume_pending && !c->capture_drained); checks++;
    view_policy(c)->resume_pending=1; c->capture_drained=1;
    r.type=c->out.type;
    assert(ioctl(fd,VIDIOC_REQBUFS,&r)==0 && !view_policy(c)->resume_pending &&
           c->capture_drained); checks++;
    view_policy(c)->resume_pending=1;
    assert(close(fd)==0 && !c->active && !view_policy(c)->resume_pending &&
           tablet_v4l2_capture_requires_finish()==0); checks++;
    printf("deferred capture resume: %u checks passed\n",checks);
    return 0;
}
