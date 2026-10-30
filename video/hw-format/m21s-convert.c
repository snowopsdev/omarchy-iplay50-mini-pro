/* SPDX-License-Identifier: MIT */
#include "m21s-convert.h"
#include <errno.h>
#include <libyuv/convert.h>

int tablet_m21s_to_nv12(const uint8_t *src, size_t src_bytes,
                       uint32_t coded_width, uint32_t coded_height,
                       uint32_t src_stride, uint8_t *dst, size_t dst_bytes,
                       uint32_t dst_stride)
{
    if (!src || !dst || !coded_width || !coded_height ||
        coded_width > 8192 || coded_height > 8192 ||
        coded_width % 2 || coded_height % 32 ||
        src_stride < coded_width || src_stride > 16384 || src_stride % 16 ||
        dst_stride < coded_width || dst_stride > 16384 || dst_stride % 2)
        return -EINVAL;

    /* Validated limits keep these products below 2^28 even on 32-bit hosts. */
    const size_t src_y_bytes = (size_t)src_stride * coded_height;
    const size_t dst_y_bytes = (size_t)dst_stride * coded_height;
    const size_t src_need = src_y_bytes + src_y_bytes / 2;
    const size_t dst_need = dst_y_bytes + dst_y_bytes / 2;
    if (src_bytes < src_need || dst_bytes < dst_need)
        return -ENOSPC;
    const uintptr_t src_begin = (uintptr_t)src;
    const uintptr_t dst_begin = (uintptr_t)dst;
    if (src_begin > UINTPTR_MAX - src_need || dst_begin > UINTPTR_MAX - dst_need)
        return -EOVERFLOW;
    if (src_begin < dst_begin + dst_need && dst_begin < src_begin + src_need)
        return -EINVAL;

    return MM21ToNV12(src, (int)src_stride, src + src_y_bytes, (int)src_stride,
                      dst, (int)dst_stride, dst + dst_y_bytes, (int)dst_stride,
                      (int)coded_width, (int)coded_height) == 0 ? 0 : -EINVAL;
}
