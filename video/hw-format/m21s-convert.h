/* SPDX-License-Identifier: MIT */
#ifndef TABLET_M21S_CONVERT_H
#define TABLET_M21S_CONVERT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Convert verified 8-bit contiguous MM21 tiles to linear contiguous NV12.
 * Y/UV source boundary: src_stride * coded_height. Destination boundary:
 * dst_stride * coded_height. No crop, allocation, device ioctls or implicit
 * synchronization. Caller owns source READ / destination WRITE dma-buf cache
 * synchronization and CAPTURE buffer lifetime. Source and destination may
 * not overlap. M2CS, MT21C and 10-bit layouts are not accepted by this API.
 * Returns 0, -EINVAL, -ENOSPC, or -EOVERFLOW. Geometry is validated before any
 * pixel access; full source/destination coded-frame allocations are required.
 */
int tablet_m21s_to_nv12(const uint8_t *src, size_t src_bytes,
                       uint32_t coded_width, uint32_t coded_height,
                       uint32_t src_stride, uint8_t *dst, size_t dst_bytes,
                       uint32_t dst_stride);

#ifdef __cplusplus
}
#endif
#endif
