/* Offline only. Reuse the real bridge/mock kernel, never device ioctls. */
#define main bridge_baseline_contract_main
#include "bridge-host-test.c"
#undef main
static unsigned metadata_queries;
static int bad_shape, metadata_failure;
static struct tablet_mtk_color_desc descriptor;
static int color_ioctl(int fd,unsigned long request,...) {
    va_list ap; va_start(ap,request); void *arg=va_arg(ap,void *); va_end(ap);
    if ((uint32_t)request==VIDIOC_DQEVENT) {
        struct v4l2_event *e=arg;
        e->type=V4L2_EVENT_SOURCE_CHANGE;
        e->u.src_change.changes=V4L2_EVENT_SRC_CH_RESOLUTION;
        return 0;
    }
    if ((uint32_t)request==VIDIOC_QUERY_EXT_CTRL) {
        metadata_queries++;
        if (metadata_failure) { errno=EINVAL; return -1; }
        struct v4l2_query_ext_ctrl *q=arg;
        assert(q->id==TABLET_MTK_COLOR_DESC_ID);
        q->type=V4L2_CTRL_TYPE_U32; q->elem_size=4; q->elems=bad_shape ? 16 : 17;
        q->nr_of_dims=1; q->dims[0]=17;
        q->flags=V4L2_CTRL_FLAG_READ_ONLY|V4L2_CTRL_FLAG_VOLATILE;
        return 0;
    }
    if ((uint32_t)request==VIDIOC_G_EXT_CTRLS) {
        struct v4l2_ext_controls *c=arg;
        assert(c->count==1 && c->controls->id==TABLET_MTK_COLOR_DESC_ID &&
               c->controls->size==sizeof(descriptor));
        memcpy(c->controls->ptr,&descriptor,sizeof(descriptor)); return 0;
    }
    return fake_ioctl(fd,request,arg);
}
int main(int argc,char **argv) {
    assert(argc==3);
    next_ioctl=color_ioctl; next_close=dlsym(RTLD_NEXT,"close");
    next_mmap=dlsym(RTLD_NEXT,"mmap"); next_mmap64=dlsym(RTLD_NEXT,"mmap64");
    enabled=1; heap_fd=100000;
    struct context *c=&contexts[0]; *c=(struct context){.active=1,.fd=12345};
    c->out.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    c->cap.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    unsigned checks=0;
    struct v4l2_format fmt={.type=c->cap.type};
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 &&
           fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12 && !metadata_queries); checks++;
    request_argb=1;
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 &&
           fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12 && !metadata_queries &&
           !view_policy(c)->settled); checks++;
    struct v4l2_event event={0};
    assert(bridge_ioctl(c,VIDIOC_DQEVENT,&event)==0 && !view_policy(c)->header_seen); checks++;
    struct v4l2_requestbuffers output_req={.type=c->out.type,.memory=V4L2_MEMORY_MMAP,.count=1};
    assert(bridge_ioctl(c,VIDIOC_REQBUFS,&output_req)==0); checks++;
    struct v4l2_plane bitstream_plane={.bytesused=4};
    struct v4l2_buffer bitstream={.type=c->out.type,.memory=V4L2_MEMORY_MMAP,
        .length=1,.m.planes=&bitstream_plane};
    memcpy(c->out.slots[0].view.ptr,"TEST",4);
    assert(bridge_ioctl(c,VIDIOC_QBUF,&bitstream)==0 && view_policy(c)->input_queued); checks++;
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && !metadata_queries &&
           !view_policy(c)->settled); checks++;
    assert(bridge_ioctl(c,VIDIOC_DQEVENT,&event)==0 && view_policy(c)->header_seen); checks++;
    descriptor=(struct tablet_mtk_color_desc){.color_primaries=1,
        .transfer_characteristics=1,.matrix_coefficients=1,.full_range=0};
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 &&
           fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_ABGR32 && metadata_queries==1); checks++;
    assert(fmt.fmt.pix_mp.plane_fmt[0].bytesperline==1280 &&
           fmt.fmt.pix_mp.plane_fmt[0].sizeimage==327680); checks++;
    struct v4l2_fmtdesc desc={.type=c->cap.type};
    assert(bridge_ioctl(c,VIDIOC_ENUM_FMT,&desc)==0 && desc.pixelformat==V4L2_PIX_FMT_ABGR32); checks++;
    assert(bridge_ioctl(c,VIDIOC_S_FMT,&fmt)==0 && metadata_queries==1); checks++;
    struct v4l2_requestbuffers req={.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.count=2};
    assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0 && c->cap.count==2); checks++;
    assert(c->cap.slots[0].native.len==122880 && c->cap.slots[0].view.len==327680 &&
           view_policy(c)->scratch_bytes==122880 && view_policy(c)->scratch); checks++;
    FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(c->cap.slots[0].native.ptr,1,122880,f)==122880); fclose(f);
    struct v4l2_plane plane={0};
    struct v4l2_buffer b={.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.length=1,.m.planes=&plane};
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==0); checks++;
    assert(bridge_ioctl(c,VIDIOC_DQBUF,&b)==0 && plane.bytesused==327680); checks++;
    uint8_t *pixels=c->cap.slots[0].view.ptr;
    for (unsigned i=0;i<327680;i+=4) assert(pixels[i+3]==255);
    checks++;
    f=fopen(argv[2],"wb"); assert(f && fwrite(pixels,1,327680,f)==327680); fclose(f);
    struct v4l2_exportbuffer exp={.type=c->cap.type,.flags=O_CLOEXEC};
    assert(bridge_ioctl(c,VIDIOC_EXPBUF,&exp)==0); checks++;
    void *mapped=next_mmap(NULL,327680,PROT_READ,MAP_SHARED,exp.fd,0);
    assert(mapped!=MAP_FAILED && !memcmp(mapped,pixels,327680)); checks++;
    munmap(mapped,327680); next_close(exp.fd);
    req.count=0; assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0 &&
                        !view_policy(c)->scratch && !view_policy(c)->settled); checks++;
    descriptor.matrix_coefficients=UINT32_MAX;
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    const uint32_t invalid_transfer[]={0,2,16,18,UINT32_MAX};
    descriptor.matrix_coefficients=1;
    for (unsigned i=0;i<sizeof(invalid_transfer)/sizeof(invalid_transfer[0]);i++) {
        reset_view_policy(c); descriptor.transfer_characteristics=invalid_transfer[i];
        assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    }
    descriptor.transfer_characteristics=1; descriptor.full_range=UINT32_MAX;
    reset_view_policy(c);
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    descriptor.full_range=0; descriptor.is_hdr=1; reset_view_policy(c);
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    descriptor.is_hdr=0; bad_shape=1; reset_view_policy(c);
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    bad_shape=0; metadata_failure=1; reset_view_policy(c);
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    reset_view_policy(c);
    unsigned out_type=c->out.type;
    assert(bridge_ioctl(c,VIDIOC_STREAMOFF,&out_type)==0 &&
           !view_policy(c)->header_seen && !view_policy(c)->input_queued); checks++;
    unsigned before_queries=metadata_queries;
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 &&
           !view_policy(c)->settled && metadata_queries==before_queries); checks++;
    /* Same actual BGRA conversion, with XRGB/opaque V4L2 semantics. */
    metadata_failure=0; descriptor=(struct tablet_mtk_color_desc){
        .color_primaries=1,.transfer_characteristics=1,
        .matrix_coefficients=1,.full_range=0};
    rgb_view_fourcc=V4L2_PIX_FMT_BGR32;
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && !view_policy(c)->settled &&
           fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    bitstream_plane.bytesused=4;
    assert(bridge_ioctl(c,VIDIOC_QBUF,&bitstream)==0); checks++;
    assert(bridge_ioctl(c,VIDIOC_DQEVENT,&event)==0); checks++;
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 &&
           fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_BGR32 &&
           fmt.fmt.pix_mp.plane_fmt[0].bytesperline==1280 &&
           fmt.fmt.pix_mp.plane_fmt[0].sizeimage==327680); checks++;
    desc=(struct v4l2_fmtdesc){.type=c->cap.type};
    assert(bridge_ioctl(c,VIDIOC_ENUM_FMT,&desc)==0 && desc.pixelformat==V4L2_PIX_FMT_BGR32); checks++;
    assert(bridge_ioctl(c,VIDIOC_S_FMT,&fmt)==0); checks++;
    struct v4l2_format wrong=fmt; wrong.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_ABGR32;
    assert(bridge_ioctl(c,VIDIOC_S_FMT,&wrong)==-1 && errno==ENOTSUP); checks++;
    req.count=2; assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0); checks++;
    f=fopen(argv[1],"rb"); assert(f && fread(c->cap.slots[0].native.ptr,1,122880,f)==122880); fclose(f);
    plane=(struct v4l2_plane){0};
    b=(struct v4l2_buffer){.type=c->cap.type,.memory=V4L2_MEMORY_MMAP,.length=1,.m.planes=&plane};
    assert(bridge_ioctl(c,VIDIOC_QBUF,&b)==0); checks++;
    assert(bridge_ioctl(c,VIDIOC_DQBUF,&b)==0 && plane.bytesused==327680); checks++;
    uint8_t *expected=malloc(327680); assert(expected);
    f=fopen(argv[2],"rb"); assert(f && fread(expected,1,327680,f)==327680); fclose(f);
    assert(!memcmp(c->cap.slots[0].view.ptr,expected,327680)); checks++;
    memset(c->cap.slots[0].native.ptr,0,122880);
    assert(!memcmp(c->cap.slots[0].view.ptr,expected,327680)); checks++;
    exp=(struct v4l2_exportbuffer){.type=c->cap.type,.flags=O_CLOEXEC};
    assert(bridge_ioctl(c,VIDIOC_EXPBUF,&exp)==0); checks++;
    mapped=next_mmap(NULL,327680,PROT_READ,MAP_SHARED,exp.fd,0);
    assert(mapped!=MAP_FAILED && !memcmp(mapped,expected,327680)); checks++;
    munmap(mapped,327680); next_close(exp.fd); free(expected);
    req.count=0; assert(bridge_ioctl(c,VIDIOC_REQBUFS,&req)==0 && !view_policy(c)->scratch); checks++;
    descriptor.matrix_coefficients=UINT32_MAX;
    assert(bridge_ioctl(c,VIDIOC_G_FMT,&fmt)==0 && fmt.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    desc=(struct v4l2_fmtdesc){.type=c->cap.type};
    assert(bridge_ioctl(c,VIDIOC_ENUM_FMT,&desc)==0 && desc.pixelformat==V4L2_PIX_FMT_NV12); checks++;
    reset_view_policy(c); free_queue(&c->out);
    printf("packed RGB bridge: %u offline checks passed; actual BGRA artifact written %s\n",checks,argv[2]);
    return 0;
}
