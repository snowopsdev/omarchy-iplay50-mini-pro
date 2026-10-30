/* Allocation-free runtime readback of the decoder's queue memory-mode guard.
 * Requires the vendor daemon ready before opening. Never submits count>0,
 * formats, buffers, DMA allocation, QBUF, or STREAMON. Run without any preload.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if (argc != 2 || getenv("LD_PRELOAD") || getenv("TABLET_V4L2_BRIDGE")) {
        fprintf(stderr,"usage: env -u LD_PRELOAD -u TABLET_V4L2_BRIDGE check-memory-guard /dev/videoN\n");
        return 2;
    }
    printf("ENTER open %s (daemon must already be ready)\n",argv[1]);
    int fd=open(argv[1],O_RDWR|O_NONBLOCK|O_CLOEXEC);
    printf("RETURN open fd=%d errno=%d\n",fd,fd<0 ? errno : 0);
    if (fd < 0) return 1;
    int result=1;
    struct v4l2_capability caps={0};
    if (ioctl(fd,VIDIOC_QUERYCAP,&caps)<0 ||
        strncmp((char *)caps.driver,"mtk-vcodec-dec",sizeof(caps.driver)) ||
        strncmp((char *)caps.card,"platform:mt6789",sizeof(caps.card))) goto done;
    unsigned types[]={V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
    for (unsigned i=0;i<2;i++) {
        struct v4l2_requestbuffers req={.type=types[i],.memory=V4L2_MEMORY_MMAP,.count=0};
        printf("ENTER REQBUFS type=%u memory=MMAP count=0\n",req.type);
        errno=0; int r=ioctl(fd,VIDIOC_REQBUFS,&req),e=errno;
        printf("RETURN ret=%d errno=%d count=%u capabilities=%08x\n",r,e,req.count,req.capabilities);
        /* This GKI rejects an unsupported memory type before filling the
         * capabilities field. Read back supported modes on the successful
         * zero-count DMABUF request below. */
        if (r != -1 || e != EINVAL || req.count ||
            (req.capabilities & V4L2_BUF_CAP_SUPPORTS_MMAP)) {
            fprintf(stderr,"GUARD FAIL: MMAP not cleanly refused; no nonzero allocation attempted\n");
            goto done;
        }
        req=(struct v4l2_requestbuffers){.type=types[i],.memory=V4L2_MEMORY_DMABUF,.count=0};
        printf("ENTER REQBUFS type=%u memory=DMABUF count=0\n",req.type);
        errno=0; r=ioctl(fd,VIDIOC_REQBUFS,&req); e=errno;
        printf("RETURN ret=%d errno=%d count=%u capabilities=%08x\n",r,e,req.count,req.capabilities);
        if (r || req.count || (req.capabilities & V4L2_BUF_CAP_SUPPORTS_MMAP) ||
            !(req.capabilities & V4L2_BUF_CAP_SUPPORTS_DMABUF)) goto done;
    }
    puts("GUARD PASS: both queues reject MMAP and advertise DMABUF; all requests count=0");
    result=0;
done:
    puts("ENTER close");
    if (close(fd) < 0) result=1;
    printf("RETURN close; result=%d\n",result);
    return result;
}
