/* Offline adapter contract test. Mock kernel ioctls only; no device operations.
 * Exercises the real bridge functions/converter against actual captured tiles.
 */
#include "mt6789-v4l2-bridge.c"
#include <assert.h>
#include <sys/wait.h>
static unsigned fake_queued[2], fake_fd[2];
static unsigned long last_original_request;
static unsigned native_qbuf_calls, native_dqbuf_calls;
static int fake_capture_last;
static unsigned queue_id(unsigned type) { return type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; }
static int fake_ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    last_original_request=request;
    request=(uint32_t)request;
    va_list ap; va_start(ap,request); void *arg=va_arg(ap,void *); va_end(ap);
    if (request == UINT32_C(0x12345678)) return 77;
    if (request == VIDIOC_QUERYCAP) {
        struct v4l2_capability *caps=arg;
        memset(caps,0,sizeof(*caps));
        strcpy((char *)caps->driver,"mtk-vcodec-dec");
        strcpy((char *)caps->card,"platform:mt6789"); return 0;
    }
    if (request == DMA_HEAP_IOCTL_ALLOC) {
        struct dma_heap_allocation_data *a=arg;
        int n=memfd_create("mock-dma-allocation",MFD_CLOEXEC);
        assert(n>=0 && ftruncate(n,(off_t)a->len)==0); a->fd=(unsigned)n; return 0;
    }
    if (request == DMA_BUF_IOCTL_SYNC) return 0;
    if (request == VIDIOC_G_FMT) {
        struct v4l2_format *fmt=arg;
        fmt->fmt.pix_mp=(struct v4l2_pix_format_mplane){.width=320,.height=256,
            .num_planes=1,.pixelformat=queue_id(fmt->type) ? M21S : V4L2_PIX_FMT_H264};
        fmt->fmt.pix_mp.plane_fmt[0].sizeimage=queue_id(fmt->type) ? 122880 : 2097152;
        fmt->fmt.pix_mp.plane_fmt[0].bytesperline=320; return 0;
    }
    if (request == VIDIOC_REQBUFS) {
        struct v4l2_requestbuffers *r=arg; assert(r->memory==V4L2_MEMORY_DMABUF);
        return 0;
    }
    if (request == VIDIOC_QBUF) {
        native_qbuf_calls++;
        struct v4l2_buffer *b=arg; unsigned id=queue_id(b->type);
        assert(b->memory==V4L2_MEMORY_DMABUF && b->length==1);
        if (id) assert(b->request_fd==-1);
        fake_queued[id]=b->index; fake_fd[id]=(unsigned)b->m.planes[0].m.fd; return 0;
    }
    if (request == VIDIOC_DQBUF) {
        native_dqbuf_calls++;
        struct v4l2_buffer *b=arg; unsigned id=queue_id(b->type);
        assert(b->memory==V4L2_MEMORY_DMABUF && b->length==1);
        b->index=fake_queued[id]; b->m.planes[0].bytesused=id ? 122880 : 4;
        if (id && fake_capture_last) { b->m.planes[0].bytesused=0; b->flags=V4L2_BUF_FLAG_LAST; }
        return 0;
    }
    if (request == VIDIOC_STREAMON || request == VIDIOC_STREAMOFF || request == VIDIOC_DECODER_CMD) return 0;
    assert(!"unexpected mock ioctl"); return -1;
}
int main(int argc,char **argv) {
    assert(argc==3);
    next_ioctl=fake_ioctl; next_close=dlsym(RTLD_NEXT,"close");
    next_mmap=dlsym(RTLD_NEXT,"mmap"); next_mmap64=dlsym(RTLD_NEXT,"mmap64");
    enabled=1; heap_fd=100000;
    struct context *c=&contexts[0]; *c=(struct context){.active=1,.fd=12345};
    c->out.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    c->cap.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    unsigned tests=0;
    /* Chromium V4L2Device takes int request; high-bit commands sign-extend
     * when passed to libc ioctl(unsigned long). Exercise the public wrapper. */
    int matched=open("/dev/null",O_RDWR | O_NONBLOCK | O_CLOEXEC); assert(matched>=0);
    struct v4l2_capability signed_caps={0};
    unsigned long signed_query=(unsigned long)(long)(int)VIDIOC_QUERYCAP;
    assert(ioctl(matched,signed_query,&signed_caps)==0 && find_context(matched)); tests++;
    assert(last_original_request==signed_query); tests++;
    struct v4l2_requestbuffers signed_req={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
        .memory=V4L2_MEMORY_MMAP,.count=2};
    unsigned long signed_reqbufs=(unsigned long)(long)(int)VIDIOC_REQBUFS;
    assert(ioctl(matched,signed_reqbufs,&signed_req)==0 && signed_req.memory==V4L2_MEMORY_MMAP); tests++;
    signed_req.count=0;
    assert(ioctl(matched,signed_reqbufs,&signed_req)==0); tests++;
    assert(close(matched)==0); tests++;
    unsigned long unrelated=UINT64_C(0xffffffff12345678);
    assert(ioctl(-1,unrelated,&signed_caps)==77 && last_original_request==unrelated); tests++;
    struct v4l2_format fmt={.type=c->cap.type};
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); tests++;
    fmt.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_MM21;
    assert(bridge_ioctl(c,VIDIOC_S_FMT,&fmt)==-1 && errno==ENOTSUP); tests++;
    struct v4l2_requestbuffers req={.type=c->out.type,.memory=V4L2_MEMORY_MMAP,.count=2};
    assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0); tests++;
    req=(struct v4l2_requestbuffers){.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.count=2};
    assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0); tests++;
    struct v4l2_plane plane={0};
    struct v4l2_buffer b={.type=c->out.type,.memory=V4L2_MEMORY_MMAP,.length=1,.m.planes=&plane};
    assert(bridge_ioctl(c,VIDIOC_QUERYBUF,&b)==0); tests++;
    unsigned char *mapped=bridge_mmap(NULL,plane.length,PROT_READ|PROT_WRITE,MAP_SHARED,c->fd,plane.m.mem_offset,0);
    assert(mapped!=MAP_FAILED); memcpy(mapped,"TEST",4); plane.bytesused=4;
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==0); tests++;
    assert(memcmp(c->out.slots[0].native.ptr,"TEST",4)==0); tests++;
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==-1 && errno==EINVAL); tests++;
    assert(bridge_ioctl(c,VIDIOC_DQBUF,&b)==0 && b.memory==V4L2_MEMORY_MMAP); tests++;
    munmap(mapped,c->out.slots[0].view.len);
    FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(c->cap.slots[0].native.ptr,1,122880,f)==122880); fclose(f);
    plane=(struct v4l2_plane){0};
    b=(struct v4l2_buffer){.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.length=1,.m.planes=&plane};
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==0); tests++;
    assert(bridge_ioctl(c,VIDIOC_DQBUF,&b)==0 && plane.bytesused==122880); tests++;
    unsigned char *nv12=c->cap.slots[0].view.ptr;
    unsigned char ref[115200]; f=fopen(argv[2],"rb"); assert(f);
    assert(fread(ref,1,sizeof(ref),f)==sizeof(ref)); fclose(f);
    assert(memcmp(nv12,ref,320*240)==0); tests++;
    for (unsigned y=0;y<120;y++) for (unsigned x=0;x<160;x++) {
        unsigned ix=y*160+x, uv=320*256+y*320+2*x;
        assert(nv12[uv]==ref[320*240+ix]);
        assert(nv12[uv+1]==ref[320*240+160*120+ix]);
    }
    tests++;
    struct v4l2_exportbuffer exp={.type=c->cap.type,.index=0,.flags=O_CLOEXEC};
    assert(bridge_ioctl(c,VIDIOC_EXPBUF,&exp)==0 && exp.fd>=0); tests++;
    assert(fcntl(exp.fd,F_GETFD)&FD_CLOEXEC); tests++; next_close(exp.fd);
    exp.plane=1; assert(bridge_ioctl(c,VIDIOC_EXPBUF,&exp)==-1 && errno==ENOTSUP); tests++;
    req=(struct v4l2_requestbuffers){.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.count=0};
    assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0); tests++;
    capture_headroom=8;
    req=(struct v4l2_requestbuffers){.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.count=11};
    assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0 && req.count==19 && c->cap.count==19); tests++;
    assert(c->cap.slots[18].native.ptr && c->cap.slots[18].view.ptr && c->out.count==2); tests++;
    req.count=0; assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0); tests++;
    req.count=28;
    assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0 && req.count==32 && c->cap.count==32); tests++;
    req.count=0; assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0); tests++;
    capture_headroom=0;
    decouple_capture=1;
    req=(struct v4l2_requestbuffers){.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.count=3};
    assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0 && req.count==3); tests++;
    for (unsigned i=0;i<3;i++) {
        f=fopen(argv[1],"rb"); assert(f);
        assert(fread(c->cap.slots[i].native.ptr,1,122880,f)==122880); fclose(f);
    }
    unsigned before_q=native_qbuf_calls;
    plane=(struct v4l2_plane){0};
    b=(struct v4l2_buffer){.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.index=2,.length=1,.m.planes=&plane};
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==0 && native_qbuf_calls==before_q+3); tests++;
    assert(bridge_ioctl(c,VIDIOC_DQBUF,&b)==0 && b.index==2 && c->cap.slots[2].native_queued); tests++;
    unsigned char *held=malloc(122880); assert(held); memcpy(held,c->cap.slots[2].view.ptr,122880);
    memset(c->cap.slots[2].native.ptr,0xab,122880);
    b.index=0; plane=(struct v4l2_plane){0};
    before_q=native_qbuf_calls;
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==0 && native_qbuf_calls==before_q); tests++;
    assert(bridge_ioctl(c,VIDIOC_DQBUF,&b)==0 && b.index==0 && c->cap.slots[2].native_queued); tests++;
    assert(memcmp(held,c->cap.slots[2].view.ptr,122880)==0 &&
           memcmp(held,c->cap.slots[0].view.ptr,122880)!=0); tests++;
    unsigned before_dq=native_dqbuf_calls;
    assert(bridge_ioctl(c,VIDIOC_DQBUF,&b)==-1 && errno==EAGAIN && native_dqbuf_calls==before_dq); tests++;
    b.index=1; plane=(struct v4l2_plane){0};
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==0); tests++;
    fake_capture_last=1; before_q=native_qbuf_calls;
    assert(bridge_ioctl(c,VIDIOC_DQBUF,&b)==0 && b.index==1 && !plane.bytesused &&
           (b.flags&V4L2_BUF_FLAG_LAST) && !c->cap.slots[2].native_queued &&
           c->capture_drained && native_qbuf_calls==before_q); tests++;
    b.index=0; b.flags=0; plane=(struct v4l2_plane){0};
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==0 && native_qbuf_calls==before_q); tests++;
    fake_capture_last=0; c->cap.streaming=1;
    struct v4l2_decoder_cmd start={.cmd=V4L2_DEC_CMD_START};
    assert(bridge_ioctl(c,VIDIOC_DECODER_CMD,&start)==0 && c->capture_drained &&
           view_policy(c)->resume_pending && !c->cap.slots[2].native_queued &&
           native_qbuf_calls==before_q); tests++;
    struct v4l2_plane resume_plane={.bytesused=4};
    struct v4l2_buffer resume={.type=c->out.type,.memory=V4L2_MEMORY_MMAP,
        .length=1,.m.planes=&resume_plane};
    assert(bridge_ioctl(c,VIDIOC_QBUF,&resume)==0 && !c->capture_drained &&
           !view_policy(c)->resume_pending && c->cap.slots[2].native_queued &&
           native_qbuf_calls==before_q+2); tests++;
    unsigned capture_type=c->cap.type;
    assert(bridge_ioctl(c,VIDIOC_STREAMOFF,&capture_type)==0); tests++;
    for (unsigned i=0;i<3;i++) assert(!c->cap.slots[i].queued && !c->cap.slots[i].native_queued);
    tests++;
    req.count=0; assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0); tests++;
    free(held); decouple_capture=0;
    assert(pthread_atfork(fork_prepare,fork_parent,fork_child)==0);
    pid_t child=fork(); assert(child>=0);
    if (!child) {
        struct v4l2_format inherited={.type=c->out.type};
        _exit(ioctl(c->fd,VIDIOC_G_FMT,&inherited)==-1 && errno==EOWNERDEAD ? 0 : 1);
    }
    int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    tests++;
    assert(!c->revoked); tests++;
    free_queue(&c->out);
    printf("offline bridge contract: %u checks passed; converted actual tile artifact matches independent I420 exactly\n",tests);
    return 0;
}
