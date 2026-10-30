#include "m21s-argb.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <libyuv/convert_argb.h>
#include <libyuv/row.h>
int tablet_nv12_to_argb(const uint8_t *src,size_t src_bytes,
                       uint32_t width,uint32_t height,uint32_t src_stride,
                       uint8_t *dst,size_t dst_bytes,uint32_t dst_stride,
                       unsigned matrix_coefficients,unsigned full_range) {
    if (!src || !dst || !width || !height || width>8192 || height>8192 ||
        (width&1) || (height&1) || src_stride<width || src_stride>16384 ||
        (src_stride&1) || dst_stride<width*4U || dst_stride>32768 ||
        (dst_stride&3) || full_range>1) return -EINVAL;
    if (matrix_coefficients!=1 && matrix_coefficients!=5 && matrix_coefficients!=6)
        return -ENOTSUP;
    const struct YuvConstants *matrix;
    /* Same coefficients as upstream LIBYUV_UNLIMITED_BT601/BT709, using
     * public architecture-specific fields instead of byte-offset patching.
     * Packaged limited-range constants may clamp blue UB to128. */
    _Alignas(64) struct YuvConstants accurate_limited;
    if (!full_range) {
        unsigned ub=matrix_coefficients==1 ? 135 : 129;
        accurate_limited=matrix_coefficients==1 ? kYuvH709Constants : kYuvI601Constants;
#if defined(__aarch64__) || defined(__arm__) || defined(__riscv)
        accurate_limited.kUVCoeff[0]=(uint8_t)ub;
        accurate_limited.kRGBCoeffBias[1]=(int16_t)(ub*128-accurate_limited.kRGBCoeffBias[4]);
#elif defined(__x86_64__) || defined(__i386__)
        for (unsigned i=0;i<sizeof(accurate_limited.kUVToB);i+=2)
            accurate_limited.kUVToB[i]=(uint8_t)ub;
#else
        return -ENOTSUP;
#endif
        matrix=&accurate_limited;
    } else matrix=matrix_coefficients==1 ? &kYuvF709Constants : &kYuvJPEGConstants;
    size_t y_bytes=(size_t)src_stride*height;
    size_t src_required=y_bytes+y_bytes/2;
    size_t dst_required=(size_t)dst_stride*height;
    if (src_bytes<src_required || dst_bytes<dst_required) return -ENOSPC;
    uintptr_t source=(uintptr_t)src,dest=(uintptr_t)dst;
    if (source>UINTPTR_MAX-src_required || dest>UINTPTR_MAX-dst_required)
        return -EOVERFLOW;
    if (source<dest+dst_required && dest<source+src_required) return -EINVAL;
    int result=NV12ToARGBMatrix(src,(int)src_stride,src+y_bytes,(int)src_stride,
                                dst,(int)dst_stride,matrix,(int)width,(int)height);
    return result ? -EIO : 0;
}
