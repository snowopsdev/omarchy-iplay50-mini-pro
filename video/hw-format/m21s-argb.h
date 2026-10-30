#ifndef TABLET_M21S_ARGB_H
#define TABLET_M21S_ARGB_H
#include <stddef.h>
#include <stdint.h>
/* Convert contiguous linear NV12 to libyuv ARGB (BGRA bytes on little-endian).
 * Source UV starts at src_stride*height. Caller owns buffers and DMA sync.
 * H.264 matrix_coefficients: 1=BT709, 5/6=BT601; full_range is 0 or 1.
 * Limited601/709 use upstream unlimited blue coefficients129/135, avoiding
 * packaged coefficient128 clipping. Unknown matrices/ranges reject.
 * No guessed colorspace and no allocation.
 * Returns zero or negative errno. Existing M21S conversion API is unchanged.
 */
int tablet_nv12_to_argb(const uint8_t *src,size_t src_bytes,
                       uint32_t width,uint32_t height,uint32_t src_stride,
                       uint8_t *dst,size_t dst_bytes,uint32_t dst_stride,
                       unsigned matrix_coefficients,unsigned full_range);
#endif
