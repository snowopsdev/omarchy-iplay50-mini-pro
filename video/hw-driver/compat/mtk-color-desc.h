/* Related vendor ABI, independently confirmed by retained module g_v_ctrl.
 * Read-only post-header metadata; never infer range/matrix from resolution.
 */
#ifndef TABLET_MTK_COLOR_DESC_H
#define TABLET_MTK_COLOR_DESC_H
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <linux/videodev2.h>
#define TABLET_MTK_COLOR_DESC_ID UINT32_C(0x00992007)
struct tablet_mtk_color_desc {
    uint32_t color_primaries, transfer_characteristics, matrix_coefficients;
    uint32_t display_primaries_x[3], display_primaries_y[3];
    uint32_t white_point_x, white_point_y;
    uint32_t max_mastering_luminance, min_mastering_luminance;
    uint32_t max_content_light_level, max_picture_light_level;
    uint32_t is_hdr, full_range;
};
_Static_assert(sizeof(struct tablet_mtk_color_desc)==68,"vendor color ABI");
static int tablet_query_color_desc(int fd,
        int (*io)(int,unsigned long,...), struct tablet_mtk_color_desc *out,
        struct v4l2_query_ext_ctrl *shape) {
    struct v4l2_query_ext_ctrl q={.id=TABLET_MTK_COLOR_DESC_ID};
    if (io(fd,VIDIOC_QUERY_EXT_CTRL,&q)<0) return -1;
    if (shape) *shape=q;
    if (q.id!=TABLET_MTK_COLOR_DESC_ID || q.type!=V4L2_CTRL_TYPE_U32 ||
        q.elem_size!=4 || q.elems!=17 || q.nr_of_dims!=1 || q.dims[0]!=17 ||
        !(q.flags & V4L2_CTRL_FLAG_READ_ONLY) ||
        !(q.flags & V4L2_CTRL_FLAG_VOLATILE) ||
        (q.flags & (V4L2_CTRL_FLAG_DISABLED|V4L2_CTRL_FLAG_WRITE_ONLY))) {
        errno=ENOTSUP; return -1;
    }
    struct tablet_mtk_color_desc result={0};
    struct v4l2_ext_control control={.id=TABLET_MTK_COLOR_DESC_ID,
                                   .size=sizeof(result),.ptr=&result};
    struct v4l2_ext_controls controls={.which=V4L2_CTRL_WHICH_CUR_VAL,
                                     .count=1,.controls=&control};
    if (io(fd,VIDIOC_G_EXT_CTRLS,&controls)<0) return -1;
    if (control.size!=sizeof(result)) { errno=ENOTSUP; return -1; }
    *out=result;
    return 0;
}
static int tablet_color_desc_rgb_supported(const struct tablet_mtk_color_desc *c) {
    return c && !c->is_hdr && c->full_range<=1 &&
           (c->matrix_coefficients==1 || c->matrix_coefficients==5 ||
            c->matrix_coefficients==6) &&
           (c->transfer_characteristics==1 || c->transfer_characteristics==6 ||
            c->transfer_characteristics==13 || c->transfer_characteristics==14 ||
            c->transfer_characteristics==15);
}
#endif
