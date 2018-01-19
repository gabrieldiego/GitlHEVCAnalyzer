/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <assert.h>
#include <stdio.h>
#include <limits.h>

#include "./aom_scale_rtcd.h"
#include "./aom_dsp_rtcd.h"
#include "./aom_config.h"

#include "aom/aom_integer.h"
#include "aom_dsp/blend.h"

#include "av1/common/blockd.h"
#include "av1/common/reconinter.h"
#include "av1/common/reconintra.h"
#include "av1/common/onyxc_int.h"
#include "av1/common/obmc.h"

// This function will determine whether or not to create a warped
// prediction.
static INLINE int allow_warp(const MODE_INFO *const mi,
                             const WarpTypesAllowed *const warp_types,
                             const WarpedMotionParams *const gm_params,
                             int build_for_obmc, int x_scale, int y_scale,
                             WarpedMotionParams *final_warp_params) {
  if (x_scale != SCALE_SUBPEL_SHIFTS || y_scale != SCALE_SUBPEL_SHIFTS)
    return 0;

  const MB_MODE_INFO *const mbmi = &mi->mbmi;
  *final_warp_params = default_warp_params;

  if (mbmi->wm_params[0].invalid) return 0;
  if (gm_params->invalid) return 0;

  if (warp_types->local_warp_allowed) {
    if ((build_for_obmc && WARP_WM_NEIGHBORS_WITH_OBMC) || (!build_for_obmc)) {
      memcpy(final_warp_params, &mbmi->wm_params[0],
             sizeof(*final_warp_params));
      return 1;
    }
  } else if (warp_types->global_warp_allowed &&
             (WARP_GM_NEIGHBORS_WITH_OBMC || !build_for_obmc)) {
    memcpy(final_warp_params, gm_params, sizeof(*final_warp_params));
    return 1;
  }

  return 0;
}

static INLINE void av1_make_inter_predictor(
    const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride,
    const int subpel_x, const int subpel_y, const struct scale_factors *sf,
    int w, int h, ConvolveParams *conv_params, InterpFilters interp_filters,
    const WarpTypesAllowed *warp_types, int p_col, int p_row, int plane,
    int ref, const MODE_INFO *mi, int build_for_obmc, int xs, int ys,
    const MACROBLOCKD *xd) {
  (void)xd;

  // Make sure the selected motion mode is valid for this configuration
  assert_motion_mode_valid(mi->mbmi.motion_mode, xd->global_motion, xd, mi);

  WarpedMotionParams final_warp_params;
  const int do_warp =
      (w >= 8 && h >= 8 &&
       allow_warp(mi, warp_types, &xd->global_motion[mi->mbmi.ref_frame[ref]],
                  build_for_obmc, xs, ys, &final_warp_params));
  if (do_warp
#if CONFIG_AMVR
      && xd->cur_frame_force_integer_mv == 0
#endif
      ) {
    const struct macroblockd_plane *const pd = &xd->plane[plane];
    const struct buf_2d *const pre_buf = &pd->pre[ref];
    av1_warp_plane(&final_warp_params,
                   xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH, xd->bd,
                   pre_buf->buf0, pre_buf->width, pre_buf->height,
                   pre_buf->stride, dst, p_col, p_row, w, h, dst_stride,
                   pd->subsampling_x, pd->subsampling_y, conv_params);
    return;
  }
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    highbd_inter_predictor(src, src_stride, dst, dst_stride, subpel_x, subpel_y,
                           sf, w, h, conv_params, interp_filters, xs, ys,
                           xd->bd);
    return;
  }
  inter_predictor(src, src_stride, dst, dst_stride, subpel_x, subpel_y, sf, w,
                  h, conv_params, interp_filters, xs, ys);
}

#define NSMOOTHERS 1

// [smoother][negative][direction]
DECLARE_ALIGNED(16, static uint8_t,
                wedge_mask_obl[NSMOOTHERS][2][WEDGE_DIRECTIONS]
                              [MASK_MASTER_SIZE * MASK_MASTER_SIZE]);

DECLARE_ALIGNED(16, static uint8_t,
                wedge_signflip_lookup[BLOCK_SIZES_ALL][MAX_WEDGE_TYPES]);

// 4 * MAX_WEDGE_SQUARE is an easy to compute and fairly tight upper bound
// on the sum of all mask sizes up to an including MAX_WEDGE_SQUARE.
DECLARE_ALIGNED(16, static uint8_t,
                wedge_mask_buf[2 * MAX_WEDGE_TYPES * 4 * MAX_WEDGE_SQUARE]);

static wedge_masks_type wedge_masks[BLOCK_SIZES_ALL][2];

// Some unused wedge codebooks left temporarily to facilitate experiments.
// To be removed when settled.
/*
static wedge_code_type wedge_codebook_8_hgtw[8] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_OBLIQUE27, 4, 2 },  { WEDGE_OBLIQUE27, 4, 6 },
  { WEDGE_OBLIQUE153, 4, 2 }, { WEDGE_OBLIQUE153, 4, 6 },
};

static wedge_code_type wedge_codebook_8_hltw[8] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_OBLIQUE63, 2, 4 },  { WEDGE_OBLIQUE63, 6, 4 },
  { WEDGE_OBLIQUE117, 2, 4 }, { WEDGE_OBLIQUE117, 6, 4 },
};

static wedge_code_type wedge_codebook_8_heqw[8] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 2 }, { WEDGE_HORIZONTAL, 4, 6 },
  { WEDGE_VERTICAL, 2, 4 },   { WEDGE_VERTICAL, 6, 4 },
};

static const wedge_code_type wedge_codebook_32_hgtw[32] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 2 }, { WEDGE_HORIZONTAL, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 6 }, { WEDGE_VERTICAL, 4, 4 },
  { WEDGE_OBLIQUE27, 4, 1 },  { WEDGE_OBLIQUE27, 4, 2 },
  { WEDGE_OBLIQUE27, 4, 3 },  { WEDGE_OBLIQUE27, 4, 5 },
  { WEDGE_OBLIQUE27, 4, 6 },  { WEDGE_OBLIQUE27, 4, 7 },
  { WEDGE_OBLIQUE153, 4, 1 }, { WEDGE_OBLIQUE153, 4, 2 },
  { WEDGE_OBLIQUE153, 4, 3 }, { WEDGE_OBLIQUE153, 4, 5 },
  { WEDGE_OBLIQUE153, 4, 6 }, { WEDGE_OBLIQUE153, 4, 7 },
  { WEDGE_OBLIQUE63, 1, 4 },  { WEDGE_OBLIQUE63, 2, 4 },
  { WEDGE_OBLIQUE63, 3, 4 },  { WEDGE_OBLIQUE63, 5, 4 },
  { WEDGE_OBLIQUE63, 6, 4 },  { WEDGE_OBLIQUE63, 7, 4 },
  { WEDGE_OBLIQUE117, 1, 4 }, { WEDGE_OBLIQUE117, 2, 4 },
  { WEDGE_OBLIQUE117, 3, 4 }, { WEDGE_OBLIQUE117, 5, 4 },
  { WEDGE_OBLIQUE117, 6, 4 }, { WEDGE_OBLIQUE117, 7, 4 },
};

static const wedge_code_type wedge_codebook_32_hltw[32] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_VERTICAL, 2, 4 },   { WEDGE_VERTICAL, 4, 4 },
  { WEDGE_VERTICAL, 6, 4 },   { WEDGE_HORIZONTAL, 4, 4 },
  { WEDGE_OBLIQUE27, 4, 1 },  { WEDGE_OBLIQUE27, 4, 2 },
  { WEDGE_OBLIQUE27, 4, 3 },  { WEDGE_OBLIQUE27, 4, 5 },
  { WEDGE_OBLIQUE27, 4, 6 },  { WEDGE_OBLIQUE27, 4, 7 },
  { WEDGE_OBLIQUE153, 4, 1 }, { WEDGE_OBLIQUE153, 4, 2 },
  { WEDGE_OBLIQUE153, 4, 3 }, { WEDGE_OBLIQUE153, 4, 5 },
  { WEDGE_OBLIQUE153, 4, 6 }, { WEDGE_OBLIQUE153, 4, 7 },
  { WEDGE_OBLIQUE63, 1, 4 },  { WEDGE_OBLIQUE63, 2, 4 },
  { WEDGE_OBLIQUE63, 3, 4 },  { WEDGE_OBLIQUE63, 5, 4 },
  { WEDGE_OBLIQUE63, 6, 4 },  { WEDGE_OBLIQUE63, 7, 4 },
  { WEDGE_OBLIQUE117, 1, 4 }, { WEDGE_OBLIQUE117, 2, 4 },
  { WEDGE_OBLIQUE117, 3, 4 }, { WEDGE_OBLIQUE117, 5, 4 },
  { WEDGE_OBLIQUE117, 6, 4 }, { WEDGE_OBLIQUE117, 7, 4 },
};

static const wedge_code_type wedge_codebook_32_heqw[32] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 2 }, { WEDGE_HORIZONTAL, 4, 6 },
  { WEDGE_VERTICAL, 2, 4 },   { WEDGE_VERTICAL, 6, 4 },
  { WEDGE_OBLIQUE27, 4, 1 },  { WEDGE_OBLIQUE27, 4, 2 },
  { WEDGE_OBLIQUE27, 4, 3 },  { WEDGE_OBLIQUE27, 4, 5 },
  { WEDGE_OBLIQUE27, 4, 6 },  { WEDGE_OBLIQUE27, 4, 7 },
  { WEDGE_OBLIQUE153, 4, 1 }, { WEDGE_OBLIQUE153, 4, 2 },
  { WEDGE_OBLIQUE153, 4, 3 }, { WEDGE_OBLIQUE153, 4, 5 },
  { WEDGE_OBLIQUE153, 4, 6 }, { WEDGE_OBLIQUE153, 4, 7 },
  { WEDGE_OBLIQUE63, 1, 4 },  { WEDGE_OBLIQUE63, 2, 4 },
  { WEDGE_OBLIQUE63, 3, 4 },  { WEDGE_OBLIQUE63, 5, 4 },
  { WEDGE_OBLIQUE63, 6, 4 },  { WEDGE_OBLIQUE63, 7, 4 },
  { WEDGE_OBLIQUE117, 1, 4 }, { WEDGE_OBLIQUE117, 2, 4 },
  { WEDGE_OBLIQUE117, 3, 4 }, { WEDGE_OBLIQUE117, 5, 4 },
  { WEDGE_OBLIQUE117, 6, 4 }, { WEDGE_OBLIQUE117, 7, 4 },
};
*/

static const wedge_code_type wedge_codebook_16_hgtw[16] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 2 }, { WEDGE_HORIZONTAL, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 6 }, { WEDGE_VERTICAL, 4, 4 },
  { WEDGE_OBLIQUE27, 4, 2 },  { WEDGE_OBLIQUE27, 4, 6 },
  { WEDGE_OBLIQUE153, 4, 2 }, { WEDGE_OBLIQUE153, 4, 6 },
  { WEDGE_OBLIQUE63, 2, 4 },  { WEDGE_OBLIQUE63, 6, 4 },
  { WEDGE_OBLIQUE117, 2, 4 }, { WEDGE_OBLIQUE117, 6, 4 },
};

static const wedge_code_type wedge_codebook_16_hltw[16] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_VERTICAL, 2, 4 },   { WEDGE_VERTICAL, 4, 4 },
  { WEDGE_VERTICAL, 6, 4 },   { WEDGE_HORIZONTAL, 4, 4 },
  { WEDGE_OBLIQUE27, 4, 2 },  { WEDGE_OBLIQUE27, 4, 6 },
  { WEDGE_OBLIQUE153, 4, 2 }, { WEDGE_OBLIQUE153, 4, 6 },
  { WEDGE_OBLIQUE63, 2, 4 },  { WEDGE_OBLIQUE63, 6, 4 },
  { WEDGE_OBLIQUE117, 2, 4 }, { WEDGE_OBLIQUE117, 6, 4 },
};

static const wedge_code_type wedge_codebook_16_heqw[16] = {
  { WEDGE_OBLIQUE27, 4, 4 },  { WEDGE_OBLIQUE63, 4, 4 },
  { WEDGE_OBLIQUE117, 4, 4 }, { WEDGE_OBLIQUE153, 4, 4 },
  { WEDGE_HORIZONTAL, 4, 2 }, { WEDGE_HORIZONTAL, 4, 6 },
  { WEDGE_VERTICAL, 2, 4 },   { WEDGE_VERTICAL, 6, 4 },
  { WEDGE_OBLIQUE27, 4, 2 },  { WEDGE_OBLIQUE27, 4, 6 },
  { WEDGE_OBLIQUE153, 4, 2 }, { WEDGE_OBLIQUE153, 4, 6 },
  { WEDGE_OBLIQUE63, 2, 4 },  { WEDGE_OBLIQUE63, 6, 4 },
  { WEDGE_OBLIQUE117, 2, 4 }, { WEDGE_OBLIQUE117, 6, 4 },
};

const wedge_params_type wedge_params_lookup[BLOCK_SIZES_ALL] = {
  { 0, NULL, NULL, 0, NULL },
  { 0, NULL, NULL, 0, NULL },
  { 0, NULL, NULL, 0, NULL },
  { 4, wedge_codebook_16_heqw, wedge_signflip_lookup[BLOCK_8X8], 0,
    wedge_masks[BLOCK_8X8] },
  { 4, wedge_codebook_16_hgtw, wedge_signflip_lookup[BLOCK_8X16], 0,
    wedge_masks[BLOCK_8X16] },
  { 4, wedge_codebook_16_hltw, wedge_signflip_lookup[BLOCK_16X8], 0,
    wedge_masks[BLOCK_16X8] },
  { 4, wedge_codebook_16_heqw, wedge_signflip_lookup[BLOCK_16X16], 0,
    wedge_masks[BLOCK_16X16] },
  { 4, wedge_codebook_16_hgtw, wedge_signflip_lookup[BLOCK_16X32], 0,
    wedge_masks[BLOCK_16X32] },
  { 4, wedge_codebook_16_hltw, wedge_signflip_lookup[BLOCK_32X16], 0,
    wedge_masks[BLOCK_32X16] },
  { 4, wedge_codebook_16_heqw, wedge_signflip_lookup[BLOCK_32X32], 0,
    wedge_masks[BLOCK_32X32] },
  { 0, NULL, NULL, 0, NULL },
  { 0, NULL, NULL, 0, NULL },
  { 0, NULL, NULL, 0, NULL },
#if CONFIG_EXT_PARTITION
  { 0, NULL, NULL, 0, NULL },
  { 0, NULL, NULL, 0, NULL },
  { 0, NULL, NULL, 0, NULL },
#endif  // CONFIG_EXT_PARTITION
  { 0, wedge_codebook_16_hgtw, wedge_signflip_lookup[BLOCK_4X16], 0,
    wedge_masks[BLOCK_4X16] },
  { 0, wedge_codebook_16_hltw, wedge_signflip_lookup[BLOCK_16X4], 0,
    wedge_masks[BLOCK_16X4] },
  { 4, wedge_codebook_16_hgtw, wedge_signflip_lookup[BLOCK_8X32], 0,
    wedge_masks[BLOCK_8X32] },
  { 4, wedge_codebook_16_hltw, wedge_signflip_lookup[BLOCK_32X8], 0,
    wedge_masks[BLOCK_32X8] },
  { 0, NULL, NULL, 0, NULL },
  { 0, NULL, NULL, 0, NULL },
#if CONFIG_EXT_PARTITION
  { 0, NULL, NULL, 0, NULL },
  { 0, NULL, NULL, 0, NULL },
#endif  // CONFIG_EXT_PARTITION
};

static const uint8_t *get_wedge_mask_inplace(int wedge_index, int neg,
                                             BLOCK_SIZE sb_type) {
  const uint8_t *master;
  const int bh = block_size_high[sb_type];
  const int bw = block_size_wide[sb_type];
  const wedge_code_type *a =
      wedge_params_lookup[sb_type].codebook + wedge_index;
  const int smoother = wedge_params_lookup[sb_type].smoother;
  int woff, hoff;
  const uint8_t wsignflip = wedge_params_lookup[sb_type].signflip[wedge_index];

  assert(wedge_index >= 0 &&
         wedge_index < (1 << get_wedge_bits_lookup(sb_type)));
  woff = (a->x_offset * bw) >> 3;
  hoff = (a->y_offset * bh) >> 3;
  master = wedge_mask_obl[smoother][neg ^ wsignflip][a->direction] +
           MASK_MASTER_STRIDE * (MASK_MASTER_SIZE / 2 - hoff) +
           MASK_MASTER_SIZE / 2 - woff;
  return master;
}

const uint8_t *av1_get_soft_mask(int wedge_index, int wedge_sign,
                                 BLOCK_SIZE sb_type, int offset_x,
                                 int offset_y) {
  const uint8_t *mask =
      get_wedge_mask_inplace(wedge_index, wedge_sign, sb_type);
  if (mask) mask -= (offset_x + offset_y * MASK_MASTER_STRIDE);
  return mask;
}

static uint8_t *invert_mask(uint8_t *mask_inv_buffer, const uint8_t *const mask,
                            int h, int w, int stride) {
  int i, j;

  for (i = 0; i < h; ++i)
    for (j = 0; j < w; ++j) {
      mask_inv_buffer[i * stride + j] =
          AOM_BLEND_A64_MAX_ALPHA - mask[i * stride + j];
    }
  return mask_inv_buffer;
}

const uint8_t *av1_get_compound_type_mask_inverse(
    const INTERINTER_COMPOUND_DATA *const comp_data, uint8_t *mask_buffer,
    int h, int w, int stride, BLOCK_SIZE sb_type) {
  assert(is_masked_compound_type(comp_data->interinter_compound_type));
  (void)sb_type;
  switch (comp_data->interinter_compound_type) {
    case COMPOUND_WEDGE:
      return av1_get_contiguous_soft_mask(comp_data->wedge_index,
                                          !comp_data->wedge_sign, sb_type);
    case COMPOUND_SEG:
      return invert_mask(mask_buffer, comp_data->seg_mask, h, w, stride);
    default: assert(0); return NULL;
  }
}

const uint8_t *av1_get_compound_type_mask(
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type) {
  assert(is_masked_compound_type(comp_data->interinter_compound_type));
  (void)sb_type;
  switch (comp_data->interinter_compound_type) {
    case COMPOUND_WEDGE:
      return av1_get_contiguous_soft_mask(comp_data->wedge_index,
                                          comp_data->wedge_sign, sb_type);
    case COMPOUND_SEG: return comp_data->seg_mask;
    default: assert(0); return NULL;
  }
}

#if COMPOUND_SEGMENT_TYPE == 0
static void uniform_mask(uint8_t *mask, int which_inverse, BLOCK_SIZE sb_type,
                         int h, int w, int mask_val) {
  int i, j;
  int block_stride = block_size_wide[sb_type];
  for (i = 0; i < h; ++i)
    for (j = 0; j < w; ++j) {
      mask[i * block_stride + j] =
          which_inverse ? AOM_BLEND_A64_MAX_ALPHA - mask_val : mask_val;
    }
}

void build_compound_seg_mask(uint8_t *mask, SEG_MASK_TYPE mask_type,
                             const uint8_t *src0, int src0_stride,
                             const uint8_t *src1, int src1_stride,
                             BLOCK_SIZE sb_type, int h, int w) {
  (void)src0;
  (void)src1;
  (void)src0_stride;
  (void)src1_stride;
  switch (mask_type) {
    case UNIFORM_45: uniform_mask(mask, 0, sb_type, h, w, 45); break;
    case UNIFORM_45_INV: uniform_mask(mask, 1, sb_type, h, w, 45); break;
    default: assert(0);
  }
}

void build_compound_seg_mask_highbd(uint8_t *mask, SEG_MASK_TYPE mask_type,
                                    const uint8_t *src0, int src0_stride,
                                    const uint8_t *src1, int src1_stride,
                                    BLOCK_SIZE sb_type, int h, int w, int bd) {
  (void)src0;
  (void)src1;
  (void)src0_stride;
  (void)src1_stride;
  (void)bd;
  switch (mask_type) {
    case UNIFORM_45: uniform_mask(mask, 0, sb_type, h, w, 45); break;
    case UNIFORM_45_INV: uniform_mask(mask, 1, sb_type, h, w, 45); break;
    default: assert(0);
  }
}

#elif COMPOUND_SEGMENT_TYPE == 1
#define DIFF_FACTOR 16

static void diffwtd_mask_d32(uint8_t *mask, int which_inverse, int mask_base,
                             const int32_t *src0, int src0_stride,
                             const int32_t *src1, int src1_stride,
                             BLOCK_SIZE sb_type, int h, int w,
                             ConvolveParams *conv_params, int bd) {
  int round =
      2 * FILTER_BITS - conv_params->round_0 - conv_params->round_1 + (bd - 8);
  int i, j, m, diff;
  int block_stride = block_size_wide[sb_type];
  for (i = 0; i < h; ++i) {
    for (j = 0; j < w; ++j) {
      diff = abs(src0[i * src0_stride + j] - src1[i * src1_stride + j]);
      diff = ROUND_POWER_OF_TWO(diff, round);
      m = clamp(mask_base + (diff / DIFF_FACTOR), 0, AOM_BLEND_A64_MAX_ALPHA);
      mask[i * block_stride + j] =
          which_inverse ? AOM_BLEND_A64_MAX_ALPHA - m : m;
    }
  }
}

static void build_compound_seg_mask_d32(uint8_t *mask, SEG_MASK_TYPE mask_type,
                                        const int32_t *src0, int src0_stride,
                                        const int32_t *src1, int src1_stride,
                                        BLOCK_SIZE sb_type, int h, int w,
                                        ConvolveParams *conv_params, int bd) {
  switch (mask_type) {
    case DIFFWTD_38:
      diffwtd_mask_d32(mask, 0, 38, src0, src0_stride, src1, src1_stride,
                       sb_type, h, w, conv_params, bd);
      break;
    case DIFFWTD_38_INV:
      diffwtd_mask_d32(mask, 1, 38, src0, src0_stride, src1, src1_stride,
                       sb_type, h, w, conv_params, bd);
      break;
    default: assert(0);
  }
}

static void diffwtd_mask(uint8_t *mask, int which_inverse, int mask_base,
                         const uint8_t *src0, int src0_stride,
                         const uint8_t *src1, int src1_stride,
                         BLOCK_SIZE sb_type, int h, int w) {
  int i, j, m, diff;
  int block_stride = block_size_wide[sb_type];
  for (i = 0; i < h; ++i) {
    for (j = 0; j < w; ++j) {
      diff =
          abs((int)src0[i * src0_stride + j] - (int)src1[i * src1_stride + j]);
      m = clamp(mask_base + (diff / DIFF_FACTOR), 0, AOM_BLEND_A64_MAX_ALPHA);
      mask[i * block_stride + j] =
          which_inverse ? AOM_BLEND_A64_MAX_ALPHA - m : m;
    }
  }
}

void build_compound_seg_mask(uint8_t *mask, SEG_MASK_TYPE mask_type,
                             const uint8_t *src0, int src0_stride,
                             const uint8_t *src1, int src1_stride,
                             BLOCK_SIZE sb_type, int h, int w) {
  switch (mask_type) {
    case DIFFWTD_38:
      diffwtd_mask(mask, 0, 38, src0, src0_stride, src1, src1_stride, sb_type,
                   h, w);
      break;
    case DIFFWTD_38_INV:
      diffwtd_mask(mask, 1, 38, src0, src0_stride, src1, src1_stride, sb_type,
                   h, w);
      break;
    default: assert(0);
  }
}

static void diffwtd_mask_highbd(uint8_t *mask, int which_inverse, int mask_base,
                                const uint16_t *src0, int src0_stride,
                                const uint16_t *src1, int src1_stride,
                                BLOCK_SIZE sb_type, int h, int w, int bd) {
  int i, j, m, diff;
  int block_stride = block_size_wide[sb_type];
  for (i = 0; i < h; ++i) {
    for (j = 0; j < w; ++j) {
      diff = abs((int)src0[i * src0_stride + j] -
                 (int)src1[i * src1_stride + j]) >>
             (bd - 8);
      m = clamp(mask_base + (diff / DIFF_FACTOR), 0, AOM_BLEND_A64_MAX_ALPHA);
      mask[i * block_stride + j] =
          which_inverse ? AOM_BLEND_A64_MAX_ALPHA - m : m;
    }
  }
}

void build_compound_seg_mask_highbd(uint8_t *mask, SEG_MASK_TYPE mask_type,
                                    const uint8_t *src0, int src0_stride,
                                    const uint8_t *src1, int src1_stride,
                                    BLOCK_SIZE sb_type, int h, int w, int bd) {
  switch (mask_type) {
    case DIFFWTD_38:
      diffwtd_mask_highbd(mask, 0, 38, CONVERT_TO_SHORTPTR(src0), src0_stride,
                          CONVERT_TO_SHORTPTR(src1), src1_stride, sb_type, h, w,
                          bd);
      break;
    case DIFFWTD_38_INV:
      diffwtd_mask_highbd(mask, 1, 38, CONVERT_TO_SHORTPTR(src0), src0_stride,
                          CONVERT_TO_SHORTPTR(src1), src1_stride, sb_type, h, w,
                          bd);
      break;
    default: assert(0);
  }
}
#endif  // COMPOUND_SEGMENT_TYPE

#if MASK_MASTER_SIZE == 64
static const uint8_t wedge_master_oblique_odd[NSMOOTHERS][MASK_MASTER_SIZE] = {
  {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  6,  18,
      37, 53, 60, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  }
};
static const uint8_t wedge_master_oblique_even[NSMOOTHERS][MASK_MASTER_SIZE] = {
  {
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  4,  11, 27,
      46, 58, 62, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
      64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
  }
};
static const uint8_t wedge_master_vertical[NSMOOTHERS][MASK_MASTER_SIZE] = { {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  2,  7,  21,
    43, 57, 62, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
} };

static void shift_copy(const uint8_t *src, uint8_t *dst, int shift, int width) {
  if (shift >= 0) {
    memcpy(dst + shift, src, width - shift);
    memset(dst, src[0], shift);
  } else {
    shift = -shift;
    memcpy(dst, src + shift, width - shift);
    memset(dst + width - shift, src[width - 1], shift);
  }
}
#else
static const double smoother_param[NSMOOTHERS] = { 3.0 };
#endif  // MASK_MASTER_SIZE == 64

static void init_wedge_master_masks() {
  int i, j, s;
  const int w = MASK_MASTER_SIZE;
  const int h = MASK_MASTER_SIZE;
  const int stride = MASK_MASTER_STRIDE;
  for (s = 0; s < NSMOOTHERS; s++) {
// Note: index [0] stores the masters, and [1] its complement.
#if MASK_MASTER_SIZE == 64
    // Generate prototype by shifting the masters
    int shift = h / 4;
    for (i = 0; i < h; i += 2) {
      shift_copy(wedge_master_oblique_even[s],
                 &wedge_mask_obl[s][0][WEDGE_OBLIQUE63][i * stride], shift,
                 MASK_MASTER_SIZE);
      shift--;
      shift_copy(wedge_master_oblique_odd[s],
                 &wedge_mask_obl[s][0][WEDGE_OBLIQUE63][(i + 1) * stride],
                 shift, MASK_MASTER_SIZE);
      memcpy(&wedge_mask_obl[s][0][WEDGE_VERTICAL][i * stride],
             wedge_master_vertical[s],
             MASK_MASTER_SIZE * sizeof(wedge_master_vertical[s][0]));
      memcpy(&wedge_mask_obl[s][0][WEDGE_VERTICAL][(i + 1) * stride],
             wedge_master_vertical[s],
             MASK_MASTER_SIZE * sizeof(wedge_master_vertical[s][0]));
    }
#else
    const int a[2] = { 2, 1 };
    const double asqrt = sqrt(a[0] * a[0] + a[1] * a[1]);
    for (i = 0; i < h; i++) {
      for (j = 0; j < w; ++j) {
        int x = (2 * j + 1 - w);
        int y = (2 * i + 1 - h);
        double d = (a[0] * x + a[1] * y) / asqrt;
        const int msk = (int)rint((1.0 + tanh(d / smoother_param[s])) * 32);
        wedge_mask_obl[s][0][WEDGE_OBLIQUE63][i * stride + j] = msk;
        const int mskx = (int)rint((1.0 + tanh(x / smoother_param[s])) * 32);
        wedge_mask_obl[s][0][WEDGE_VERTICAL][i * stride + j] = mskx;
      }
    }
#endif  // MASK_MASTER_SIZE == 64
    for (i = 0; i < h; ++i) {
      for (j = 0; j < w; ++j) {
        const int msk = wedge_mask_obl[s][0][WEDGE_OBLIQUE63][i * stride + j];
        wedge_mask_obl[s][0][WEDGE_OBLIQUE27][j * stride + i] = msk;
        wedge_mask_obl[s][0][WEDGE_OBLIQUE117][i * stride + w - 1 - j] =
            wedge_mask_obl[s][0][WEDGE_OBLIQUE153][(w - 1 - j) * stride + i] =
                (1 << WEDGE_WEIGHT_BITS) - msk;
        wedge_mask_obl[s][1][WEDGE_OBLIQUE63][i * stride + j] =
            wedge_mask_obl[s][1][WEDGE_OBLIQUE27][j * stride + i] =
                (1 << WEDGE_WEIGHT_BITS) - msk;
        wedge_mask_obl[s][1][WEDGE_OBLIQUE117][i * stride + w - 1 - j] =
            wedge_mask_obl[s][1][WEDGE_OBLIQUE153][(w - 1 - j) * stride + i] =
                msk;
        const int mskx = wedge_mask_obl[s][0][WEDGE_VERTICAL][i * stride + j];
        wedge_mask_obl[s][0][WEDGE_HORIZONTAL][j * stride + i] = mskx;
        wedge_mask_obl[s][1][WEDGE_VERTICAL][i * stride + j] =
            wedge_mask_obl[s][1][WEDGE_HORIZONTAL][j * stride + i] =
                (1 << WEDGE_WEIGHT_BITS) - mskx;
      }
    }
  }
}

// If the signs for the wedges for various blocksizes are
// inconsistent flip the sign flag. Do it only once for every
// wedge codebook.
static void init_wedge_signs() {
  BLOCK_SIZE sb_type;
  memset(wedge_signflip_lookup, 0, sizeof(wedge_signflip_lookup));
  for (sb_type = BLOCK_4X4; sb_type < BLOCK_SIZES_ALL; ++sb_type) {
    const int bw = block_size_wide[sb_type];
    const int bh = block_size_high[sb_type];
    const wedge_params_type wedge_params = wedge_params_lookup[sb_type];
    const int wbits = wedge_params.bits;
    const int wtypes = 1 << wbits;
    int i, w;
    if (wbits == 0) continue;
    for (w = 0; w < wtypes; ++w) {
      // Get the mask master, i.e. index [0]
      const uint8_t *mask = get_wedge_mask_inplace(w, 0, sb_type);
      int avg = 0;
      for (i = 0; i < bw; ++i) avg += mask[i];
      for (i = 1; i < bh; ++i) avg += mask[i * MASK_MASTER_STRIDE];
      avg = (avg + (bw + bh - 1) / 2) / (bw + bh - 1);
      // Default sign of this wedge is 1 if the average < 32, 0 otherwise.
      // If default sign is 1:
      //   If sign requested is 0, we need to flip the sign and return
      //   the complement i.e. index [1] instead. If sign requested is 1
      //   we need to flip the sign and return index [0] instead.
      // If default sign is 0:
      //   If sign requested is 0, we need to return index [0] the master
      //   if sign requested is 1, we need to return the complement index [1]
      //   instead.
      wedge_params.signflip[w] = (avg < 32);
      // printf("%d[%d] = %d\n", sb_type, w, wedge_params.signflip[w]);
    }
  }
}

static void init_wedge_masks() {
  uint8_t *dst = wedge_mask_buf;
  BLOCK_SIZE bsize;
  memset(wedge_masks, 0, sizeof(wedge_masks));
  for (bsize = BLOCK_4X4; bsize < BLOCK_SIZES_ALL; ++bsize) {
    const uint8_t *mask;
    const int bw = block_size_wide[bsize];
    const int bh = block_size_high[bsize];
    const wedge_params_type *wedge_params = &wedge_params_lookup[bsize];
    const int wbits = wedge_params->bits;
    const int wtypes = 1 << wbits;
    int w;
    if (wbits == 0) continue;
    for (w = 0; w < wtypes; ++w) {
      mask = get_wedge_mask_inplace(w, 0, bsize);
      aom_convolve_copy(mask, MASK_MASTER_STRIDE, dst, bw, NULL, 0, NULL, 0, bw,
                        bh);
      wedge_params->masks[0][w] = dst;
      dst += bw * bh;

      mask = get_wedge_mask_inplace(w, 1, bsize);
      aom_convolve_copy(mask, MASK_MASTER_STRIDE, dst, bw, NULL, 0, NULL, 0, bw,
                        bh);
      wedge_params->masks[1][w] = dst;
      dst += bw * bh;
    }
    assert(sizeof(wedge_mask_buf) >= (size_t)(dst - wedge_mask_buf));
  }
}

// Equation of line: f(x, y) = a[0]*(x - a[2]*w/8) + a[1]*(y - a[3]*h/8) = 0
void av1_init_wedge_masks() {
  init_wedge_master_masks();
  init_wedge_signs();
  init_wedge_masks();
}

static void build_masked_compound_no_round(
    CONV_BUF_TYPE *dst, int dst_stride, const CONV_BUF_TYPE *src0,
    int src0_stride, const CONV_BUF_TYPE *src1, int src1_stride,
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type, int h,
    int w) {
  // Derive subsampling from h and w passed in. May be refactored to
  // pass in subsampling factors directly.
  const int subh = (2 << b_height_log2_lookup[sb_type]) == h;
  const int subw = (2 << b_width_log2_lookup[sb_type]) == w;
  const uint8_t *mask = av1_get_compound_type_mask(comp_data, sb_type);
  aom_blend_a64_d32_mask(dst, dst_stride, src0, src0_stride, src1, src1_stride,
                         mask, block_size_wide[sb_type], h, w, subh, subw);
}

static void build_masked_compound(
    uint8_t *dst, int dst_stride, const uint8_t *src0, int src0_stride,
    const uint8_t *src1, int src1_stride,
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type, int h,
    int w) {
  // Derive subsampling from h and w passed in. May be refactored to
  // pass in subsampling factors directly.
  const int subh = (2 << b_height_log2_lookup[sb_type]) == h;
  const int subw = (2 << b_width_log2_lookup[sb_type]) == w;
  const uint8_t *mask = av1_get_compound_type_mask(comp_data, sb_type);
  aom_blend_a64_mask(dst, dst_stride, src0, src0_stride, src1, src1_stride,
                     mask, block_size_wide[sb_type], h, w, subh, subw);
}

static void build_masked_compound_highbd(
    uint8_t *dst_8, int dst_stride, const uint8_t *src0_8, int src0_stride,
    const uint8_t *src1_8, int src1_stride,
    const INTERINTER_COMPOUND_DATA *const comp_data, BLOCK_SIZE sb_type, int h,
    int w, int bd) {
  // Derive subsampling from h and w passed in. May be refactored to
  // pass in subsampling factors directly.
  const int subh = (2 << b_height_log2_lookup[sb_type]) == h;
  const int subw = (2 << b_width_log2_lookup[sb_type]) == w;
  const uint8_t *mask = av1_get_compound_type_mask(comp_data, sb_type);
  // const uint8_t *mask =
  //     av1_get_contiguous_soft_mask(wedge_index, wedge_sign, sb_type);
  aom_highbd_blend_a64_mask(dst_8, dst_stride, src0_8, src0_stride, src1_8,
                            src1_stride, mask, block_size_wide[sb_type], h, w,
                            subh, subw, bd);
}

void av1_make_masked_inter_predictor(
    const uint8_t *pre, int pre_stride, uint8_t *dst, int dst_stride,
    const int subpel_x, const int subpel_y, const struct scale_factors *sf,
    int w, int h, ConvolveParams *conv_params, InterpFilters interp_filters,
    int xs, int ys, int plane, const WarpTypesAllowed *warp_types, int p_col,
    int p_row, int ref, MACROBLOCKD *xd) {
  const MODE_INFO *mi = xd->mi[0];

  const INTERINTER_COMPOUND_DATA comp_data = {
    mi->mbmi.wedge_index, mi->mbmi.wedge_sign, mi->mbmi.mask_type, xd->seg_mask,
    mi->mbmi.interinter_compound_type
  };

// We're going to call av1_make_inter_predictor to generate a prediction into
// a temporary buffer, then will blend that temporary buffer with that from
// the other reference.
//
// If the rounding mode is CONVOLVE_OPT_NO_ROUND
// then the predictions are at 32-bits, so we'll need 32 bits per
// pixel. Otherwise, we'll need up to 16 bits per pixel if
// CONFIG_HIGHBITDEPTH or just 8 otherwise.
#define INTER_PRED_BYTES_PER_PIXEL 4

  DECLARE_ALIGNED(16, uint8_t,
                  tmp_buf[INTER_PRED_BYTES_PER_PIXEL * MAX_SB_SQUARE]);
#undef INTER_PRED_BYTES_PER_PIXEL

  uint8_t *tmp_dst = (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
                         ? CONVERT_TO_BYTEPTR(tmp_buf)
                         : tmp_buf;

  const int tmp_buf_stride = MAX_SB_SIZE;
  const int is_conv_no_round = conv_params->round == CONVOLVE_OPT_NO_ROUND;
  CONV_BUF_TYPE *org_dst = conv_params->dst;
  int org_dst_stride = conv_params->dst_stride;
  CONV_BUF_TYPE *tmp_buf32 = (CONV_BUF_TYPE *)tmp_buf;
  if (is_conv_no_round) {
    conv_params->dst = tmp_buf32;
    conv_params->dst_stride = tmp_buf_stride;
    assert(conv_params->do_average == 0);
  }

  // This will generate a prediction in tmp_buf for the second reference
  av1_make_inter_predictor(pre, pre_stride, tmp_dst, MAX_SB_SIZE, subpel_x,
                           subpel_y, sf, w, h, conv_params, interp_filters,
                           warp_types, p_col, p_row, plane, ref, mi, 0, xs, ys,
                           xd);

  if (!plane && comp_data.interinter_compound_type == COMPOUND_SEG) {
    if (is_conv_no_round) {
      build_compound_seg_mask_d32(comp_data.seg_mask, comp_data.mask_type,
                                  org_dst, org_dst_stride, tmp_buf32,
                                  tmp_buf_stride, mi->mbmi.sb_type, h, w,
                                  conv_params, xd->bd);
    } else {
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
        build_compound_seg_mask_highbd(comp_data.seg_mask, comp_data.mask_type,
                                       dst, dst_stride, tmp_dst, MAX_SB_SIZE,
                                       mi->mbmi.sb_type, h, w, xd->bd);
      } else {
        build_compound_seg_mask(comp_data.seg_mask, comp_data.mask_type, dst,
                                dst_stride, tmp_dst, MAX_SB_SIZE,
                                mi->mbmi.sb_type, h, w);
      }
    }
  }

  if (is_conv_no_round) {
    build_masked_compound_no_round(org_dst, org_dst_stride, org_dst,
                                   org_dst_stride, tmp_buf32, tmp_buf_stride,
                                   &comp_data, mi->mbmi.sb_type, h, w);

    const int convolve_rounding_bits =
        FILTER_BITS * 2 - conv_params->round_0 - conv_params->round_1;
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
      av1_highbd_convolve_rounding(org_dst, org_dst_stride, dst, dst_stride, w,
                                   h, convolve_rounding_bits, xd->bd);
    else
      av1_convolve_rounding(org_dst, org_dst_stride, dst, dst_stride, w, h,
                            convolve_rounding_bits);

    conv_params->do_post_rounding = 0;
  } else {
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
      build_masked_compound_highbd(dst, dst_stride, dst, dst_stride, tmp_dst,
                                   MAX_SB_SIZE, &comp_data, mi->mbmi.sb_type, h,
                                   w, xd->bd);
    else
      build_masked_compound(dst, dst_stride, dst, dst_stride, tmp_dst,
                            MAX_SB_SIZE, &comp_data, mi->mbmi.sb_type, h, w);
  }
}

// TODO(sarahparker) av1_highbd_build_inter_predictor and
// av1_build_inter_predictor should be combined with
// av1_make_inter_predictor
void av1_highbd_build_inter_predictor(
    const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride,
    const MV *src_mv, const struct scale_factors *sf, int w, int h, int ref,
    InterpFilters interp_filters, const WarpTypesAllowed *warp_types, int p_col,
    int p_row, int plane, enum mv_precision precision, int x, int y,
    const MACROBLOCKD *xd) {
  const int is_q4 = precision == MV_PRECISION_Q4;
  const MV mv_q4 = { is_q4 ? src_mv->row : src_mv->row * 2,
                     is_q4 ? src_mv->col : src_mv->col * 2 };
  MV32 mv = av1_scale_mv(&mv_q4, x, y, sf);
  mv.col += SCALE_EXTRA_OFF;
  mv.row += SCALE_EXTRA_OFF;
  const int subpel_x = mv.col & SCALE_SUBPEL_MASK;
  const int subpel_y = mv.row & SCALE_SUBPEL_MASK;
  ConvolveParams conv_params = get_conv_params(ref, ref, plane);

  src += (mv.row >> SCALE_SUBPEL_BITS) * src_stride +
         (mv.col >> SCALE_SUBPEL_BITS);

  av1_make_inter_predictor(src, src_stride, dst, dst_stride, subpel_x, subpel_y,
                           sf, w, h, &conv_params, interp_filters, warp_types,
                           p_col, p_row, plane, ref, xd->mi[0], 0,
                           sf->x_step_q4, sf->y_step_q4, xd);
}

void av1_build_inter_predictor(
    const uint8_t *src, int src_stride, uint8_t *dst, int dst_stride,
    const MV *src_mv, const struct scale_factors *sf, int w, int h,
    ConvolveParams *conv_params, InterpFilters interp_filters,
    const WarpTypesAllowed *warp_types, int p_col, int p_row, int plane,
    int ref, enum mv_precision precision, int x, int y, const MACROBLOCKD *xd) {
  const int is_q4 = precision == MV_PRECISION_Q4;
  const MV mv_q4 = { is_q4 ? src_mv->row : src_mv->row * 2,
                     is_q4 ? src_mv->col : src_mv->col * 2 };
  MV32 mv = av1_scale_mv(&mv_q4, x, y, sf);
  mv.col += SCALE_EXTRA_OFF;
  mv.row += SCALE_EXTRA_OFF;
  const int subpel_x = mv.col & SCALE_SUBPEL_MASK;
  const int subpel_y = mv.row & SCALE_SUBPEL_MASK;

  src += (mv.row >> SCALE_SUBPEL_BITS) * src_stride +
         (mv.col >> SCALE_SUBPEL_BITS);

  av1_make_inter_predictor(src, src_stride, dst, dst_stride, subpel_x, subpel_y,
                           sf, w, h, conv_params, interp_filters, warp_types,
                           p_col, p_row, plane, ref, xd->mi[0], 0,
                           sf->x_step_q4, sf->y_step_q4, xd);
}

typedef struct SubpelParams {
  int xs;
  int ys;
  int subpel_x;
  int subpel_y;
} SubpelParams;

#if CONFIG_JNT_COMP
void av1_jnt_comp_weight_assign(const AV1_COMMON *cm, const MB_MODE_INFO *mbmi,
                                int order_idx, int *fwd_offset, int *bck_offset,
                                int *use_jnt_comp_avg, int is_compound) {
  assert(fwd_offset != NULL && bck_offset != NULL);
  if (!is_compound || mbmi->compound_idx) {
    *use_jnt_comp_avg = 0;
    return;
  }

  *use_jnt_comp_avg = 1;
  const int bck_idx = cm->frame_refs[mbmi->ref_frame[0] - LAST_FRAME].idx;
  const int fwd_idx = cm->frame_refs[mbmi->ref_frame[1] - LAST_FRAME].idx;
  const int cur_frame_index = cm->cur_frame->cur_frame_offset;
  int bck_frame_index = 0, fwd_frame_index = 0;

  if (bck_idx >= 0) {
    bck_frame_index = cm->buffer_pool->frame_bufs[bck_idx].cur_frame_offset;
  }

  if (fwd_idx >= 0) {
    fwd_frame_index = cm->buffer_pool->frame_bufs[fwd_idx].cur_frame_offset;
  }

  int d0 = clamp(abs(fwd_frame_index - cur_frame_index), 0, MAX_FRAME_DISTANCE);
  int d1 = clamp(abs(cur_frame_index - bck_frame_index), 0, MAX_FRAME_DISTANCE);

  const int order = d0 <= d1;

  if (d0 == 0 || d1 == 0) {
    *fwd_offset = quant_dist_lookup_table[order_idx][3][order];
    *bck_offset = quant_dist_lookup_table[order_idx][3][1 - order];
    return;
  }

  int i;
  for (i = 0; i < 4; ++i) {
    int c0 = quant_dist_weight[i][order];
    int c1 = quant_dist_weight[i][!order];
    if (d0 * c0 < d1 * c1) break;
  }

  *fwd_offset = quant_dist_lookup_table[order_idx][i][order];
  *bck_offset = quant_dist_lookup_table[order_idx][i][1 - order];
}
#endif  // CONFIG_JNT_COMP

static INLINE void build_inter_predictors(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                          int plane, const MODE_INFO *mi,
                                          int build_for_obmc, int bw, int bh,
                                          int x, int y, int w, int h, int mi_x,
                                          int mi_y) {
  struct macroblockd_plane *const pd = &xd->plane[plane];
  int is_compound = has_second_ref(&mi->mbmi);
  int ref;
#if CONFIG_INTRABC
  const int is_intrabc = is_intrabc_block(&mi->mbmi);
  assert(IMPLIES(is_intrabc, !is_compound));
#endif  // CONFIG_INTRABC
  int is_global[2] = { 0, 0 };
  for (ref = 0; ref < 1 + is_compound; ++ref) {
    const WarpedMotionParams *const wm =
        &xd->global_motion[mi->mbmi.ref_frame[ref]];
    is_global[ref] = is_global_mv_block(mi, wm->wmtype);
  }

  (void)cm;

  const BLOCK_SIZE bsize = mi->mbmi.sb_type;
  const int ss_x = pd->subsampling_x;
  const int ss_y = pd->subsampling_y;
  int sub8x8_inter = (block_size_wide[bsize] < 8 && ss_x) ||
                     (block_size_high[bsize] < 8 && ss_y);

#if CONFIG_INTRABC
  if (is_intrabc) {
    sub8x8_inter = 0;
  }
#endif

  sub8x8_inter = sub8x8_inter && !build_for_obmc;
  const int row_start = (block_size_high[bsize] == 4) && ss_y ? -1 : 0;
  const int col_start = (block_size_wide[bsize] == 4) && ss_x ? -1 : 0;

  if (sub8x8_inter) {
    for (int row = row_start; row <= 0 && sub8x8_inter; ++row) {
      for (int col = col_start; col <= 0; ++col) {
        const MB_MODE_INFO *this_mbmi =
            &xd->mi[row * xd->mi_stride + col]->mbmi;
        if (!is_inter_block(this_mbmi)) sub8x8_inter = 0;
#if CONFIG_INTRABC
        if (is_intrabc_block(this_mbmi)) sub8x8_inter = 0;
#endif  // CONFIG_INTRABC
      }
    }
  }

  if (sub8x8_inter) {
    // block size
    const int b4_w = block_size_wide[bsize] >> ss_x;
    const int b4_h = block_size_high[bsize] >> ss_y;
    const BLOCK_SIZE plane_bsize = scale_chroma_bsize(bsize, ss_x, ss_y);
    const int b8_w = block_size_wide[plane_bsize] >> ss_x;
    const int b8_h = block_size_high[plane_bsize] >> ss_y;
    int idx, idy;

    const int x_base = x;
    const int y_base = y;

    const struct buf_2d orig_pred_buf[2] = { pd->pre[0], pd->pre[1] };

    int row = row_start;
    for (idy = 0; idy < b8_h; idy += b4_h) {
      int col = col_start;
      for (idx = 0; idx < b8_w; idx += b4_w) {
        MB_MODE_INFO *this_mbmi = &xd->mi[row * xd->mi_stride + col]->mbmi;
        is_compound = has_second_ref(this_mbmi);
        DECLARE_ALIGNED(16, int32_t, tmp_dst[8 * 8]);
        int tmp_dst_stride = 8;
        assert(w <= 8 && h <= 8);
        ConvolveParams conv_params = get_conv_params_no_round(
            0, 0, plane, tmp_dst, tmp_dst_stride, is_compound);
#if CONFIG_JNT_COMP
        conv_params.use_jnt_comp_avg = 0;
#endif  // CONFIG_JNT_COMP
        struct buf_2d *const dst_buf = &pd->dst;
        x = x_base + idx;
        y = y_base + idy;
        uint8_t *dst = dst_buf->buf + dst_buf->stride * y + x;

        // TODO(zoeliu): If single ref comp modes are considered here, a
        //               mismatch was caused. Need a further investigation.
        for (ref = 0; ref < 1 + is_compound; ++ref) {
          const RefBuffer *ref_buf =
              &cm->frame_refs[this_mbmi->ref_frame[ref] - LAST_FRAME];

          const int c_offset = (mi_x + MI_SIZE * col_start) >> ss_x;
          const int r_offset = (mi_y + MI_SIZE * row_start) >> ss_y;
          pd->pre[ref].buf0 =
              (plane == 1) ? ref_buf->buf->u_buffer : ref_buf->buf->v_buffer;
          pd->pre[ref].buf =
              pd->pre[ref].buf0 + scaled_buffer_offset(c_offset, r_offset,
                                                       ref_buf->buf->uv_stride,
                                                       &ref_buf->sf);
          pd->pre[ref].width = ref_buf->buf->uv_crop_width;
          pd->pre[ref].height = ref_buf->buf->uv_crop_height;
          pd->pre[ref].stride = ref_buf->buf->uv_stride;

#if CONFIG_INTRABC
          const struct scale_factors *const sf =
              is_intrabc ? &cm->sf_identity : &ref_buf->sf;
          struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
#else
          const struct scale_factors *const sf = &ref_buf->sf;
          struct buf_2d *const pre_buf = &pd->pre[ref];
#endif  // CONFIG_INTRABC

          const MV mv = this_mbmi->mv[ref].as_mv;

          uint8_t *pre;
          int xs, ys, subpel_x, subpel_y;
          const int is_scaled = av1_is_scaled(sf);
          WarpTypesAllowed warp_types;
          warp_types.global_warp_allowed = is_global[ref];
          warp_types.local_warp_allowed =
              this_mbmi->motion_mode == WARPED_CAUSAL;

          if (is_scaled) {
            int ssx = pd->subsampling_x;
            int ssy = pd->subsampling_y;
            int orig_pos_y = (mi_y << (SUBPEL_BITS - ssy)) + (y << SUBPEL_BITS);
            orig_pos_y += mv.row * (1 << (1 - ssy));
            int orig_pos_x = (mi_x << (SUBPEL_BITS - ssx)) + (x << SUBPEL_BITS);
            orig_pos_x += mv.col * (1 << (1 - ssx));
            int pos_y = sf->scale_value_y(orig_pos_y, sf);
            int pos_x = sf->scale_value_x(orig_pos_x, sf);
            pos_x += SCALE_EXTRA_OFF;
            pos_y += SCALE_EXTRA_OFF;

            const int top = -AOM_LEFT_TOP_MARGIN_SCALED(ssy);
            const int left = -AOM_LEFT_TOP_MARGIN_SCALED(ssx);
            const int bottom = (pre_buf->height + AOM_INTERP_EXTEND)
                               << SCALE_SUBPEL_BITS;
            const int right = (pre_buf->width + AOM_INTERP_EXTEND)
                              << SCALE_SUBPEL_BITS;
            pos_y = clamp(pos_y, top, bottom);
            pos_x = clamp(pos_x, left, right);

            pre = pre_buf->buf0 +
                  (pos_y >> SCALE_SUBPEL_BITS) * pre_buf->stride +
                  (pos_x >> SCALE_SUBPEL_BITS);
            subpel_x = pos_x & SCALE_SUBPEL_MASK;
            subpel_y = pos_y & SCALE_SUBPEL_MASK;
            xs = sf->x_step_q4;
            ys = sf->y_step_q4;
          } else {
            const MV mv_q4 = clamp_mv_to_umv_border_sb(
                xd, &mv, bw, bh, pd->subsampling_x, pd->subsampling_y);
            xs = ys = SCALE_SUBPEL_SHIFTS;
            subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
            subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;
            pre = pre_buf->buf +
                  (y + (mv_q4.row >> SUBPEL_BITS)) * pre_buf->stride +
                  (x + (mv_q4.col >> SUBPEL_BITS));
          }

          conv_params.ref = ref;
          conv_params.do_average = ref;
          if (is_masked_compound_type(mi->mbmi.interinter_compound_type)) {
            // masked compound type has its own average mechanism
            conv_params.do_average = 0;
          }
          if (ref && is_masked_compound_type(mi->mbmi.interinter_compound_type))
            av1_make_masked_inter_predictor(
                pre, pre_buf->stride, dst, dst_buf->stride, subpel_x, subpel_y,
                sf, b4_w, b4_h, &conv_params, mi->mbmi.interp_filters, xs, ys,
                plane, &warp_types, (mi_x >> pd->subsampling_x) + x,
                (mi_y >> pd->subsampling_y) + y, ref, xd);
          else
            av1_make_inter_predictor(
                pre, pre_buf->stride, dst, dst_buf->stride, subpel_x, subpel_y,
                sf, b4_w, b4_h, &conv_params, this_mbmi->interp_filters,
                &warp_types, (mi_x >> pd->subsampling_x) + x,
                (mi_y >> pd->subsampling_y) + y, plane, ref, mi, build_for_obmc,
                xs, ys, xd);
        }  // for (ref = 0; ref < 1 + is_compound; ++ref)
        if (conv_params.do_post_rounding) {
          if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
            av1_highbd_convolve_rounding(
                tmp_dst, tmp_dst_stride, dst, dst_buf->stride, b4_w, b4_h,
                FILTER_BITS * 2 + is_compound - conv_params.round_0 -
                    conv_params.round_1,
                xd->bd);
          else
            av1_convolve_rounding(
                tmp_dst, tmp_dst_stride, dst, dst_buf->stride, b4_w, b4_h,
                FILTER_BITS * 2 + is_compound - conv_params.round_0 -
                    conv_params.round_1);
        }
        ++col;
      }
      ++row;
    }

    for (ref = 0; ref < 2; ++ref) pd->pre[ref] = orig_pred_buf[ref];
    return;
  }

  {
    struct buf_2d *const dst_buf = &pd->dst;
    uint8_t *const dst = dst_buf->buf + dst_buf->stride * y + x;
    uint8_t *pre[2];
    SubpelParams subpel_params[2];
    DECLARE_ALIGNED(16, int32_t, tmp_dst[MAX_SB_SIZE * MAX_SB_SIZE]);

    for (ref = 0; ref < 1 + is_compound; ++ref) {
#if CONFIG_INTRABC
      const struct scale_factors *const sf =
          is_intrabc ? &cm->sf_identity : &xd->block_refs[ref]->sf;
      struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
#else
      const struct scale_factors *const sf = &xd->block_refs[ref]->sf;
      struct buf_2d *const pre_buf = &pd->pre[ref];
#endif  // CONFIG_INTRABC
      const MV mv = mi->mbmi.mv[ref].as_mv;

      const int is_scaled = av1_is_scaled(sf);
      if (is_scaled) {
        // Note: The various inputs here have different units:
        // * mi_x/mi_y are in units of luma pixels
        // * mv is in units of 1/8 luma pixels
        // * x/y are in units of pixels *in the current plane*
        // Here we unify these into a q4-format position within the current
        // plane, then project into the reference frame
        int ssx = pd->subsampling_x;
        int ssy = pd->subsampling_y;
        int orig_pos_y = (mi_y << (SUBPEL_BITS - ssy)) + (y << SUBPEL_BITS);
        orig_pos_y += mv.row * (1 << (1 - ssy));
        int orig_pos_x = (mi_x << (SUBPEL_BITS - ssx)) + (x << SUBPEL_BITS);
        orig_pos_x += mv.col * (1 << (1 - ssx));
        int pos_y = sf->scale_value_y(orig_pos_y, sf);
        int pos_x = sf->scale_value_x(orig_pos_x, sf);
        pos_x += SCALE_EXTRA_OFF;
        pos_y += SCALE_EXTRA_OFF;

        // Clamp against the reference frame borders, with enough extension
        // that we don't force the reference block to be partially onscreen.
        const int top = -AOM_LEFT_TOP_MARGIN_SCALED(ssy);
        const int left = -AOM_LEFT_TOP_MARGIN_SCALED(ssx);
        const int bottom = (pre_buf->height + AOM_INTERP_EXTEND)
                           << SCALE_SUBPEL_BITS;
        const int right = (pre_buf->width + AOM_INTERP_EXTEND)
                          << SCALE_SUBPEL_BITS;
        pos_y = clamp(pos_y, top, bottom);
        pos_x = clamp(pos_x, left, right);

        pre[ref] = pre_buf->buf0 +
                   (pos_y >> SCALE_SUBPEL_BITS) * pre_buf->stride +
                   (pos_x >> SCALE_SUBPEL_BITS);
        subpel_params[ref].subpel_x = pos_x & SCALE_SUBPEL_MASK;
        subpel_params[ref].subpel_y = pos_y & SCALE_SUBPEL_MASK;
        subpel_params[ref].xs = sf->x_step_q4;
        subpel_params[ref].ys = sf->y_step_q4;
      } else {
        const MV mv_q4 = clamp_mv_to_umv_border_sb(
            xd, &mv, bw, bh, pd->subsampling_x, pd->subsampling_y);
        subpel_params[ref].subpel_x = (mv_q4.col & SUBPEL_MASK)
                                      << SCALE_EXTRA_BITS;
        subpel_params[ref].subpel_y = (mv_q4.row & SUBPEL_MASK)
                                      << SCALE_EXTRA_BITS;
        subpel_params[ref].xs = SCALE_SUBPEL_SHIFTS;
        subpel_params[ref].ys = SCALE_SUBPEL_SHIFTS;
        pre[ref] = pre_buf->buf +
                   (y + (mv_q4.row >> SUBPEL_BITS)) * pre_buf->stride +
                   (x + (mv_q4.col >> SUBPEL_BITS));
      }
    }

    ConvolveParams conv_params = get_conv_params_no_round(
        ref, ref, plane, tmp_dst, MAX_SB_SIZE, is_compound);
#if CONFIG_JNT_COMP
    av1_jnt_comp_weight_assign(cm, &mi->mbmi, 0, &conv_params.fwd_offset,
                               &conv_params.bck_offset,
                               &conv_params.use_jnt_comp_avg, is_compound);
#endif  // CONFIG_JNT_COMP

    for (ref = 0; ref < 1 + is_compound; ++ref) {
#if CONFIG_INTRABC
      const struct scale_factors *const sf =
          is_intrabc ? &cm->sf_identity : &xd->block_refs[ref]->sf;
      struct buf_2d *const pre_buf = is_intrabc ? dst_buf : &pd->pre[ref];
#else
      const struct scale_factors *const sf = &xd->block_refs[ref]->sf;
      struct buf_2d *const pre_buf = &pd->pre[ref];
#endif  // CONFIG_INTRABC
      WarpTypesAllowed warp_types;
      warp_types.global_warp_allowed = is_global[ref];
      warp_types.local_warp_allowed = mi->mbmi.motion_mode == WARPED_CAUSAL;
      conv_params.ref = ref;
      conv_params.do_average = ref;
      if (is_masked_compound_type(mi->mbmi.interinter_compound_type)) {
        // masked compound type has its own average mechanism
        conv_params.do_average = 0;
      }

      if (ref && is_masked_compound_type(mi->mbmi.interinter_compound_type))
        av1_make_masked_inter_predictor(
            pre[ref], pre_buf->stride, dst, dst_buf->stride,
            subpel_params[ref].subpel_x, subpel_params[ref].subpel_y, sf, w, h,
            &conv_params, mi->mbmi.interp_filters, subpel_params[ref].xs,
            subpel_params[ref].ys, plane, &warp_types,
            (mi_x >> pd->subsampling_x) + x, (mi_y >> pd->subsampling_y) + y,
            ref, xd);
      else
        av1_make_inter_predictor(
            pre[ref], pre_buf->stride, dst, dst_buf->stride,
            subpel_params[ref].subpel_x, subpel_params[ref].subpel_y, sf, w, h,
            &conv_params, mi->mbmi.interp_filters, &warp_types,
            (mi_x >> pd->subsampling_x) + x, (mi_y >> pd->subsampling_y) + y,
            plane, ref, mi, build_for_obmc, subpel_params[ref].xs,
            subpel_params[ref].ys, xd);
    }

    // TODO(angiebird): This part needs optimization
    if (conv_params.do_post_rounding) {
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
        av1_highbd_convolve_rounding(
            tmp_dst, MAX_SB_SIZE, dst, dst_buf->stride, w, h,
            FILTER_BITS * 2 + is_compound - conv_params.round_0 -
                conv_params.round_1,
            xd->bd);
      else
        av1_convolve_rounding(tmp_dst, MAX_SB_SIZE, dst, dst_buf->stride, w, h,
                              FILTER_BITS * 2 + is_compound -
                                  conv_params.round_0 - conv_params.round_1);
    }
  }
}

static void build_inter_predictors_for_planes(const AV1_COMMON *cm,
                                              MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                              int mi_row, int mi_col,
                                              int plane_from, int plane_to) {
  int plane;
  const int mi_x = mi_col * MI_SIZE;
  const int mi_y = mi_row * MI_SIZE;
  for (plane = plane_from; plane <= plane_to; ++plane) {
    const struct macroblockd_plane *pd = &xd->plane[plane];
    const int bw = pd->width;
    const int bh = pd->height;

    if (!is_chroma_reference(mi_row, mi_col, bsize, pd->subsampling_x,
                             pd->subsampling_y))
      continue;

    build_inter_predictors(cm, xd, plane, xd->mi[0], 0, bw, bh, 0, 0, bw, bh,
                           mi_x, mi_y);
  }
}

void av1_build_inter_predictors_sby(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                    int mi_row, int mi_col, BUFFER_SET *ctx,
                                    BLOCK_SIZE bsize) {
  build_inter_predictors_for_planes(cm, xd, bsize, mi_row, mi_col, 0, 0);

  if (is_interintra_pred(&xd->mi[0]->mbmi)) {
    BUFFER_SET default_ctx = { { xd->plane[0].dst.buf, NULL, NULL },
                               { xd->plane[0].dst.stride, 0, 0 } };
    if (!ctx) ctx = &default_ctx;
    av1_build_interintra_predictors_sby(cm, xd, xd->plane[0].dst.buf,
                                        xd->plane[0].dst.stride, ctx, bsize);
  }
}

void av1_build_inter_predictors_sbuv(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                     int mi_row, int mi_col, BUFFER_SET *ctx,
                                     BLOCK_SIZE bsize) {
  build_inter_predictors_for_planes(cm, xd, bsize, mi_row, mi_col, 1,
                                    MAX_MB_PLANE - 1);

  if (is_interintra_pred(&xd->mi[0]->mbmi)) {
    BUFFER_SET default_ctx = {
      { NULL, xd->plane[1].dst.buf, xd->plane[2].dst.buf },
      { 0, xd->plane[1].dst.stride, xd->plane[2].dst.stride }
    };
    if (!ctx) ctx = &default_ctx;
    av1_build_interintra_predictors_sbuv(
        cm, xd, xd->plane[1].dst.buf, xd->plane[2].dst.buf,
        xd->plane[1].dst.stride, xd->plane[2].dst.stride, ctx, bsize);
  }
}

void av1_build_inter_predictors_sb(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                   int mi_row, int mi_col, BUFFER_SET *ctx,
                                   BLOCK_SIZE bsize) {
  av1_build_inter_predictors_sby(cm, xd, mi_row, mi_col, ctx, bsize);
  av1_build_inter_predictors_sbuv(cm, xd, mi_row, mi_col, ctx, bsize);
}

void av1_setup_dst_planes(struct macroblockd_plane *planes, BLOCK_SIZE bsize,
                          const YV12_BUFFER_CONFIG *src, int mi_row,
                          int mi_col) {
  const int widths[MAX_MB_PLANE] = { src->y_crop_width, src->uv_crop_width,
                                     src->uv_crop_width };
  const int heights[MAX_MB_PLANE] = { src->y_crop_height, src->uv_crop_height,
                                      src->uv_crop_height };
  const int strides[MAX_MB_PLANE] = { src->y_stride, src->uv_stride,
                                      src->uv_stride };
  int i;

  for (i = 0; i < MAX_MB_PLANE; ++i) {
    struct macroblockd_plane *const pd = &planes[i];
    setup_pred_plane(&pd->dst, bsize, src->buffers[i], widths[i], heights[i],
                     strides[i], mi_row, mi_col, NULL, pd->subsampling_x,
                     pd->subsampling_y);
  }
}

void av1_setup_pre_planes(MACROBLOCKD *xd, int idx,
                          const YV12_BUFFER_CONFIG *src, int mi_row, int mi_col,
                          const struct scale_factors *sf) {
  if (src != NULL) {
    int i;
    uint8_t *const buffers[MAX_MB_PLANE] = { src->y_buffer, src->u_buffer,
                                             src->v_buffer };
    const int widths[MAX_MB_PLANE] = { src->y_crop_width, src->uv_crop_width,
                                       src->uv_crop_width };
    const int heights[MAX_MB_PLANE] = { src->y_crop_height, src->uv_crop_height,
                                        src->uv_crop_height };
    const int strides[MAX_MB_PLANE] = { src->y_stride, src->uv_stride,
                                        src->uv_stride };
    for (i = 0; i < MAX_MB_PLANE; ++i) {
      struct macroblockd_plane *const pd = &xd->plane[i];
      setup_pred_plane(&pd->pre[idx], xd->mi[0]->mbmi.sb_type, buffers[i],
                       widths[i], heights[i], strides[i], mi_row, mi_col, sf,
                       pd->subsampling_x, pd->subsampling_y);
    }
  }
}

// obmc_mask_N[overlap_position]
static const uint8_t obmc_mask_1[1] = { 64 };

static const uint8_t obmc_mask_2[2] = { 45, 64 };

static const uint8_t obmc_mask_4[4] = { 39, 50, 59, 64 };

static const uint8_t obmc_mask_8[8] = { 36, 42, 48, 53, 57, 61, 64, 64 };

static const uint8_t obmc_mask_16[16] = { 34, 37, 40, 43, 46, 49, 52, 54,
                                          56, 58, 60, 61, 64, 64, 64, 64 };

static const uint8_t obmc_mask_32[32] = { 33, 35, 36, 38, 40, 41, 43, 44,
                                          45, 47, 48, 50, 51, 52, 53, 55,
                                          56, 57, 58, 59, 60, 60, 61, 62,
                                          64, 64, 64, 64, 64, 64, 64, 64 };

#if CONFIG_EXT_PARTITION
static const uint8_t obmc_mask_64[64] = {
  33, 34, 35, 35, 36, 37, 38, 39, 40, 40, 41, 42, 43, 44, 44, 44,
  45, 46, 47, 47, 48, 49, 50, 51, 51, 51, 52, 52, 53, 54, 55, 56,
  56, 56, 57, 57, 58, 58, 59, 60, 60, 60, 60, 60, 61, 62, 62, 62,
  62, 62, 63, 63, 63, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
};
#endif  // CONFIG_EXT_PARTITION

const uint8_t *av1_get_obmc_mask(int length) {
  switch (length) {
    case 1: return obmc_mask_1;
    case 2: return obmc_mask_2;
    case 4: return obmc_mask_4;
    case 8: return obmc_mask_8;
    case 16: return obmc_mask_16;
    case 32: return obmc_mask_32;
#if CONFIG_EXT_PARTITION
    case 64: return obmc_mask_64;
#endif  // CONFIG_EXT_PARTITION
    default: assert(0); return NULL;
  }
}

static INLINE void increment_int_ptr(MACROBLOCKD *xd, int rel_mi_rc,
                                     uint8_t mi_hw, MODE_INFO *mi,
                                     void *fun_ctxt) {
  (void)xd;
  (void)rel_mi_rc;
  (void)mi_hw;
  (void)mi;
  ++*(int *)fun_ctxt;
}

void av1_count_overlappable_neighbors(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                      int mi_row, int mi_col) {
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;

  mbmi->overlappable_neighbors[0] = 0;
  mbmi->overlappable_neighbors[1] = 0;

  if (!is_motion_variation_allowed_bsize(mbmi->sb_type)) return;

  foreach_overlappable_nb_above(cm, xd, mi_col, INT_MAX, increment_int_ptr,
                                &mbmi->overlappable_neighbors[0]);
  foreach_overlappable_nb_left(cm, xd, mi_row, INT_MAX, increment_int_ptr,
                               &mbmi->overlappable_neighbors[1]);
}

// HW does not support < 4x4 prediction. To limit the bandwidth requirement, if
// block-size of current plane is smaller than 8x8, always only blend with the
// left neighbor(s) (skip blending with the above side).
#define DISABLE_CHROMA_U8X8_OBMC 0  // 0: one-sided obmc; 1: disable

int skip_u4x4_pred_in_obmc(BLOCK_SIZE bsize, const struct macroblockd_plane *pd,
                           int dir) {
  assert(is_motion_variation_allowed_bsize(bsize));

  BLOCK_SIZE bsize_plane =
      ss_size_lookup[bsize][pd->subsampling_x][pd->subsampling_y];
  switch (bsize_plane) {
#if DISABLE_CHROMA_U8X8_OBMC
    case BLOCK_4X4:
    case BLOCK_8X4:
    case BLOCK_4X8: return 1; break;
#else
    case BLOCK_4X4:
    case BLOCK_8X4:
    case BLOCK_4X8: return dir == 0; break;
#endif
    default: return 0;
  }
}

struct obmc_inter_pred_ctxt {
  uint8_t **adjacent;
  int *adjacent_stride;
};

static INLINE void build_obmc_inter_pred_above(MACROBLOCKD *xd, int rel_mi_col,
                                               uint8_t above_mi_width,
                                               MODE_INFO *above_mi,
                                               void *fun_ctxt) {
  (void)above_mi;
  struct obmc_inter_pred_ctxt *ctxt = (struct obmc_inter_pred_ctxt *)fun_ctxt;
  const BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;
  const int is_hbd = (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) ? 1 : 0;
  const int overlap =
      AOMMIN(block_size_high[bsize], block_size_high[BLOCK_64X64]) >> 1;

  for (int plane = 0; plane < MAX_MB_PLANE; ++plane) {
    const struct macroblockd_plane *pd = &xd->plane[plane];
    const int bw = (above_mi_width * MI_SIZE) >> pd->subsampling_x;
    const int bh = overlap >> pd->subsampling_y;
    const int plane_col = (rel_mi_col * MI_SIZE) >> pd->subsampling_x;

    if (skip_u4x4_pred_in_obmc(bsize, pd, 0)) continue;

    const int dst_stride = pd->dst.stride;
    uint8_t *const dst = &pd->dst.buf[plane_col];
    const int tmp_stride = ctxt->adjacent_stride[plane];
    const uint8_t *const tmp = &ctxt->adjacent[plane][plane_col];
    const uint8_t *const mask = av1_get_obmc_mask(bh);

    if (is_hbd)
      aom_highbd_blend_a64_vmask(dst, dst_stride, dst, dst_stride, tmp,
                                 tmp_stride, mask, bh, bw, xd->bd);
    else
      aom_blend_a64_vmask(dst, dst_stride, dst, dst_stride, tmp, tmp_stride,
                          mask, bh, bw);
  }
}

static INLINE void build_obmc_inter_pred_left(MACROBLOCKD *xd, int rel_mi_row,
                                              uint8_t left_mi_height,
                                              MODE_INFO *left_mi,
                                              void *fun_ctxt) {
  (void)left_mi;
  struct obmc_inter_pred_ctxt *ctxt = (struct obmc_inter_pred_ctxt *)fun_ctxt;
  const BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;
  const int overlap =
      AOMMIN(block_size_wide[bsize], block_size_wide[BLOCK_64X64]) >> 1;
  const int is_hbd = (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) ? 1 : 0;

  for (int plane = 0; plane < MAX_MB_PLANE; ++plane) {
    const struct macroblockd_plane *pd = &xd->plane[plane];
    const int bw = overlap >> pd->subsampling_x;
    const int bh = (left_mi_height * MI_SIZE) >> pd->subsampling_y;
    const int plane_row = (rel_mi_row * MI_SIZE) >> pd->subsampling_y;

    if (skip_u4x4_pred_in_obmc(bsize, pd, 1)) continue;

    const int dst_stride = pd->dst.stride;
    uint8_t *const dst = &pd->dst.buf[plane_row * dst_stride];
    const int tmp_stride = ctxt->adjacent_stride[plane];
    const uint8_t *const tmp = &ctxt->adjacent[plane][plane_row * tmp_stride];
    const uint8_t *const mask = av1_get_obmc_mask(bw);

    if (is_hbd)
      aom_highbd_blend_a64_hmask(dst, dst_stride, dst, dst_stride, tmp,
                                 tmp_stride, mask, bh, bw, xd->bd);
    else
      aom_blend_a64_hmask(dst, dst_stride, dst, dst_stride, tmp, tmp_stride,
                          mask, bh, bw);
  }
}

// This function combines motion compensated predictions that are generated by
// top/left neighboring blocks' inter predictors with the regular inter
// prediction. We assume the original prediction (bmc) is stored in
// xd->plane[].dst.buf
void av1_build_obmc_inter_prediction(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                     int mi_row, int mi_col,
                                     uint8_t *above[MAX_MB_PLANE],
                                     int above_stride[MAX_MB_PLANE],
                                     uint8_t *left[MAX_MB_PLANE],
                                     int left_stride[MAX_MB_PLANE]) {
  const BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;

  // handle above row
  struct obmc_inter_pred_ctxt ctxt_above = { above, above_stride };
  foreach_overlappable_nb_above(cm, xd, mi_col,
                                max_neighbor_obmc[b_width_log2_lookup[bsize]],
                                build_obmc_inter_pred_above, &ctxt_above);

  // handle left column
  struct obmc_inter_pred_ctxt ctxt_left = { left, left_stride };
  foreach_overlappable_nb_left(cm, xd, mi_row,
                               max_neighbor_obmc[b_height_log2_lookup[bsize]],
                               build_obmc_inter_pred_left, &ctxt_left);
}

void modify_neighbor_predictor_for_obmc(MB_MODE_INFO *mbmi) {
  if (is_interintra_pred(mbmi)) {
    mbmi->ref_frame[1] = NONE_FRAME;
  } else if (has_second_ref(mbmi) &&
             is_masked_compound_type(mbmi->interinter_compound_type)) {
    mbmi->interinter_compound_type = COMPOUND_AVERAGE;
    mbmi->ref_frame[1] = NONE_FRAME;
  }
  if (has_second_ref(mbmi)) mbmi->ref_frame[1] = NONE_FRAME;
  return;
}

struct build_prediction_ctxt {
  const AV1_COMMON *cm;
  int mi_row;
  int mi_col;
  uint8_t **tmp_buf;
  int *tmp_width;
  int *tmp_height;
  int *tmp_stride;
  int mb_to_far_edge;
};

static INLINE void build_prediction_by_above_pred(MACROBLOCKD *xd,
                                                  int rel_mi_col,
                                                  uint8_t above_mi_width,
                                                  MODE_INFO *above_mi,
                                                  void *fun_ctxt) {
  MB_MODE_INFO *above_mbmi = &above_mi->mbmi;
  const BLOCK_SIZE a_bsize = AOMMAX(BLOCK_8X8, above_mbmi->sb_type);
  struct build_prediction_ctxt *ctxt = (struct build_prediction_ctxt *)fun_ctxt;
  const int above_mi_col = ctxt->mi_col + rel_mi_col;

  MB_MODE_INFO backup_mbmi = *above_mbmi;
  modify_neighbor_predictor_for_obmc(above_mbmi);

  for (int j = 0; j < MAX_MB_PLANE; ++j) {
    struct macroblockd_plane *const pd = &xd->plane[j];
    setup_pred_plane(&pd->dst, a_bsize, ctxt->tmp_buf[j], ctxt->tmp_width[j],
                     ctxt->tmp_height[j], ctxt->tmp_stride[j], 0, rel_mi_col,
                     NULL, pd->subsampling_x, pd->subsampling_y);
  }

  const int num_refs = 1 + has_second_ref(above_mbmi);

  for (int ref = 0; ref < num_refs; ++ref) {
    const MV_REFERENCE_FRAME frame = above_mbmi->ref_frame[ref];

    const RefBuffer *const ref_buf = &ctxt->cm->frame_refs[frame - LAST_FRAME];

    xd->block_refs[ref] = ref_buf;
    if ((!av1_is_valid_scale(&ref_buf->sf)))
      aom_internal_error(xd->error_info, AOM_CODEC_UNSUP_BITSTREAM,
                         "Reference frame has invalid dimensions");
    av1_setup_pre_planes(xd, ref, ref_buf->buf, ctxt->mi_row, above_mi_col,
                         &ref_buf->sf);
  }

  xd->mb_to_left_edge = 8 * MI_SIZE * (-above_mi_col);
  xd->mb_to_right_edge = ctxt->mb_to_far_edge +
                         (xd->n8_w - rel_mi_col - above_mi_width) * MI_SIZE * 8;

  int mi_x = above_mi_col << MI_SIZE_LOG2;
  int mi_y = ctxt->mi_row << MI_SIZE_LOG2;

  const BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;

  for (int j = 0; j < MAX_MB_PLANE; ++j) {
    const struct macroblockd_plane *pd = &xd->plane[j];
    int bw = (above_mi_width * MI_SIZE) >> pd->subsampling_x;
    int bh = clamp(block_size_high[bsize] >> (pd->subsampling_y + 1), 4,
                   block_size_high[BLOCK_64X64] >> (pd->subsampling_y + 1));

    if (skip_u4x4_pred_in_obmc(bsize, pd, 0)) continue;
    build_inter_predictors(ctxt->cm, xd, j, above_mi, 1, bw, bh, 0, 0, bw, bh,
                           mi_x, mi_y);
  }
  *above_mbmi = backup_mbmi;
}

void av1_build_prediction_by_above_preds(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                         int mi_row, int mi_col,
                                         uint8_t *tmp_buf[MAX_MB_PLANE],
                                         int tmp_width[MAX_MB_PLANE],
                                         int tmp_height[MAX_MB_PLANE],
                                         int tmp_stride[MAX_MB_PLANE]) {
  if (!xd->up_available) return;

  // Adjust mb_to_bottom_edge to have the correct value for the OBMC
  // prediction block. This is half the height of the original block,
  // except for 128-wide blocks, where we only use a height of 32.
  int this_height = xd->n8_h * MI_SIZE;
  int pred_height = AOMMIN(this_height / 2, 32);
  xd->mb_to_bottom_edge += (this_height - pred_height) * 8;

  struct build_prediction_ctxt ctxt = { cm,         mi_row,
                                        mi_col,     tmp_buf,
                                        tmp_width,  tmp_height,
                                        tmp_stride, xd->mb_to_right_edge };
  BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;
  foreach_overlappable_nb_above(cm, xd, mi_col,
                                max_neighbor_obmc[b_width_log2_lookup[bsize]],
                                build_prediction_by_above_pred, &ctxt);

  xd->mb_to_left_edge = -((mi_col * MI_SIZE) * 8);
  xd->mb_to_right_edge = ctxt.mb_to_far_edge;
  xd->mb_to_bottom_edge -= (this_height - pred_height) * 8;
}

static INLINE void build_prediction_by_left_pred(MACROBLOCKD *xd,
                                                 int rel_mi_row,
                                                 uint8_t left_mi_height,
                                                 MODE_INFO *left_mi,
                                                 void *fun_ctxt) {
  MB_MODE_INFO *left_mbmi = &left_mi->mbmi;
  const BLOCK_SIZE l_bsize = AOMMAX(BLOCK_8X8, left_mbmi->sb_type);
  struct build_prediction_ctxt *ctxt = (struct build_prediction_ctxt *)fun_ctxt;
  const int left_mi_row = ctxt->mi_row + rel_mi_row;

  MB_MODE_INFO backup_mbmi = *left_mbmi;
  modify_neighbor_predictor_for_obmc(left_mbmi);

  for (int j = 0; j < MAX_MB_PLANE; ++j) {
    struct macroblockd_plane *const pd = &xd->plane[j];
    setup_pred_plane(&pd->dst, l_bsize, ctxt->tmp_buf[j], ctxt->tmp_width[j],
                     ctxt->tmp_height[j], ctxt->tmp_stride[j], rel_mi_row, 0,
                     NULL, pd->subsampling_x, pd->subsampling_y);
  }

  const int num_refs = 1 + has_second_ref(left_mbmi);

  for (int ref = 0; ref < num_refs; ++ref) {
    const MV_REFERENCE_FRAME frame = left_mbmi->ref_frame[ref];

    const RefBuffer *const ref_buf = &ctxt->cm->frame_refs[frame - LAST_FRAME];

    xd->block_refs[ref] = ref_buf;
    if ((!av1_is_valid_scale(&ref_buf->sf)))
      aom_internal_error(xd->error_info, AOM_CODEC_UNSUP_BITSTREAM,
                         "Reference frame has invalid dimensions");
    av1_setup_pre_planes(xd, ref, ref_buf->buf, left_mi_row, ctxt->mi_col,
                         &ref_buf->sf);
  }

  xd->mb_to_top_edge = 8 * MI_SIZE * (-left_mi_row);
  xd->mb_to_bottom_edge =
      ctxt->mb_to_far_edge +
      (xd->n8_h - rel_mi_row - left_mi_height) * MI_SIZE * 8;

  int mi_x = ctxt->mi_col << MI_SIZE_LOG2;
  int mi_y = left_mi_row << MI_SIZE_LOG2;

  const BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;

  for (int j = 0; j < MAX_MB_PLANE; ++j) {
    const struct macroblockd_plane *pd = &xd->plane[j];
    int bw = clamp(block_size_wide[bsize] >> (pd->subsampling_x + 1), 4,
                   block_size_wide[BLOCK_64X64] >> (pd->subsampling_x + 1));
    int bh = (left_mi_height << MI_SIZE_LOG2) >> pd->subsampling_y;

    if (skip_u4x4_pred_in_obmc(bsize, pd, 1)) continue;
    build_inter_predictors(ctxt->cm, xd, j, left_mi, 1, bw, bh, 0, 0, bw, bh,
                           mi_x, mi_y);
  }
  *left_mbmi = backup_mbmi;
}

void av1_build_prediction_by_left_preds(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                        int mi_row, int mi_col,
                                        uint8_t *tmp_buf[MAX_MB_PLANE],
                                        int tmp_width[MAX_MB_PLANE],
                                        int tmp_height[MAX_MB_PLANE],
                                        int tmp_stride[MAX_MB_PLANE]) {
  if (!xd->left_available) return;

  // Adjust mb_to_right_edge to have the correct value for the OBMC
  // prediction block. This is half the width of the original block,
  // except for 128-wide blocks, where we only use a width of 32.
  int this_width = xd->n8_w * MI_SIZE;
  int pred_width = AOMMIN(this_width / 2, 32);
  xd->mb_to_right_edge += (this_width - pred_width) * 8;

  struct build_prediction_ctxt ctxt = { cm,         mi_row,
                                        mi_col,     tmp_buf,
                                        tmp_width,  tmp_height,
                                        tmp_stride, xd->mb_to_bottom_edge };
  BLOCK_SIZE bsize = xd->mi[0]->mbmi.sb_type;
  foreach_overlappable_nb_left(cm, xd, mi_row,
                               max_neighbor_obmc[b_height_log2_lookup[bsize]],
                               build_prediction_by_left_pred, &ctxt);

  xd->mb_to_top_edge = -((mi_row * MI_SIZE) * 8);
  xd->mb_to_right_edge -= (this_width - pred_width) * 8;
  xd->mb_to_bottom_edge = ctxt.mb_to_far_edge;
}

void av1_build_obmc_inter_predictors_sb(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                        int mi_row, int mi_col) {
  DECLARE_ALIGNED(16, uint8_t, tmp_buf1[2 * MAX_MB_PLANE * MAX_SB_SQUARE]);
  DECLARE_ALIGNED(16, uint8_t, tmp_buf2[2 * MAX_MB_PLANE * MAX_SB_SQUARE]);
  uint8_t *dst_buf1[MAX_MB_PLANE], *dst_buf2[MAX_MB_PLANE];
  int dst_stride1[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_stride2[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_width1[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_width2[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_height1[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };
  int dst_height2[MAX_MB_PLANE] = { MAX_SB_SIZE, MAX_SB_SIZE, MAX_SB_SIZE };

  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    int len = sizeof(uint16_t);
    dst_buf1[0] = CONVERT_TO_BYTEPTR(tmp_buf1);
    dst_buf1[1] = CONVERT_TO_BYTEPTR(tmp_buf1 + MAX_SB_SQUARE * len);
    dst_buf1[2] = CONVERT_TO_BYTEPTR(tmp_buf1 + MAX_SB_SQUARE * 2 * len);
    dst_buf2[0] = CONVERT_TO_BYTEPTR(tmp_buf2);
    dst_buf2[1] = CONVERT_TO_BYTEPTR(tmp_buf2 + MAX_SB_SQUARE * len);
    dst_buf2[2] = CONVERT_TO_BYTEPTR(tmp_buf2 + MAX_SB_SQUARE * 2 * len);
  } else {
    dst_buf1[0] = tmp_buf1;
    dst_buf1[1] = tmp_buf1 + MAX_SB_SQUARE;
    dst_buf1[2] = tmp_buf1 + MAX_SB_SQUARE * 2;
    dst_buf2[0] = tmp_buf2;
    dst_buf2[1] = tmp_buf2 + MAX_SB_SQUARE;
    dst_buf2[2] = tmp_buf2 + MAX_SB_SQUARE * 2;
  }
  av1_build_prediction_by_above_preds(cm, xd, mi_row, mi_col, dst_buf1,
                                      dst_width1, dst_height1, dst_stride1);
  av1_build_prediction_by_left_preds(cm, xd, mi_row, mi_col, dst_buf2,
                                     dst_width2, dst_height2, dst_stride2);
  av1_setup_dst_planes(xd->plane, xd->mi[0]->mbmi.sb_type,
                       get_frame_new_buffer(cm), mi_row, mi_col);
  av1_build_obmc_inter_prediction(cm, xd, mi_row, mi_col, dst_buf1, dst_stride1,
                                  dst_buf2, dst_stride2);
}

/* clang-format off */
#if CONFIG_EXT_PARTITION
static const uint8_t ii_weights1d[MAX_SB_SIZE] = {
  60, 58, 56, 54, 52, 50, 48, 47, 45, 44, 42, 41, 39, 38, 37, 35, 34, 33, 32,
  31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 22, 21, 20, 19, 19, 18, 18, 17, 16,
  16, 15, 15, 14, 14, 13, 13, 12, 12, 12, 11, 11, 10, 10, 10,  9,  9,  9,  8,
  8,  8,  8,  7,  7,  7,  7,  6,  6,  6,  6,  6,  5,  5,  5,  5,  5,  4,  4,
  4,  4,  4,  4,  4,  4,  3,  3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2,
  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1
};
static uint8_t ii_size_scales[BLOCK_SIZES_ALL] = {
    32, 16, 16, 16, 8, 8, 8, 4,
    4,  4,  2,  2,  2, 1, 1, 1,
    16, 16, 8, 8, 4, 4, 2, 2
};
#else
static const uint8_t ii_weights1d[MAX_SB_SIZE] = {
  60, 56, 52, 48, 45, 42, 39, 37, 34, 32, 30, 28, 26, 24, 22, 21,
  19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 10,  9,  8,  8,  7,  7,
  6,  6,  6,  5,  5,  4,  4,  4,  4,  3,  3,  3,  3,  3,  2,  2,
  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1
};
static uint8_t ii_size_scales[BLOCK_SIZES_ALL] = {
    16, 8, 8, 8, 4, 4, 4,
    2,  2, 2, 1, 1, 1,
    8, 8, 4, 4, 2, 2,
};
/* clang-format on */
#endif  // CONFIG_EXT_PARTITION

static void build_smooth_interintra_mask(uint8_t *mask, int stride,
                                         BLOCK_SIZE plane_bsize,
                                         INTERINTRA_MODE mode) {
  int i, j;
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];
  const int size_scale = ii_size_scales[plane_bsize];

  switch (mode) {
    case II_V_PRED:
      for (i = 0; i < bh; ++i) {
        memset(mask, ii_weights1d[i * size_scale], bw * sizeof(mask[0]));
        mask += stride;
      }
      break;

    case II_H_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j) mask[j] = ii_weights1d[j * size_scale];
        mask += stride;
      }
      break;

    case II_SMOOTH_PRED:
      for (i = 0; i < bh; ++i) {
        for (j = 0; j < bw; ++j)
          mask[j] = ii_weights1d[(i < j ? i : j) * size_scale];
        mask += stride;
      }
      break;

    case II_DC_PRED:
    default:
      for (i = 0; i < bh; ++i) {
        memset(mask, 32, bw * sizeof(mask[0]));
        mask += stride;
      }
      break;
  }
}

static void combine_interintra(INTERINTRA_MODE mode, int use_wedge_interintra,
                               int wedge_index, int wedge_sign,
                               BLOCK_SIZE bsize, BLOCK_SIZE plane_bsize,
                               uint8_t *comppred, int compstride,
                               const uint8_t *interpred, int interstride,
                               const uint8_t *intrapred, int intrastride) {
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];

  if (use_wedge_interintra) {
    if (is_interintra_wedge_used(bsize)) {
      const uint8_t *mask =
          av1_get_contiguous_soft_mask(wedge_index, wedge_sign, bsize);
      const int subw = 2 * num_4x4_blocks_wide_lookup[bsize] == bw;
      const int subh = 2 * num_4x4_blocks_high_lookup[bsize] == bh;
      aom_blend_a64_mask(comppred, compstride, intrapred, intrastride,
                         interpred, interstride, mask, block_size_wide[bsize],
                         bh, bw, subh, subw);
    }
    return;
  }

  uint8_t mask[MAX_SB_SQUARE];
  build_smooth_interintra_mask(mask, bw, plane_bsize, mode);
  aom_blend_a64_mask(comppred, compstride, intrapred, intrastride, interpred,
                     interstride, mask, bw, bh, bw, 0, 0);
}

static void combine_interintra_highbd(
    INTERINTRA_MODE mode, int use_wedge_interintra, int wedge_index,
    int wedge_sign, BLOCK_SIZE bsize, BLOCK_SIZE plane_bsize,
    uint8_t *comppred8, int compstride, const uint8_t *interpred8,
    int interstride, const uint8_t *intrapred8, int intrastride, int bd) {
  const int bw = block_size_wide[plane_bsize];
  const int bh = block_size_high[plane_bsize];

  if (use_wedge_interintra) {
    if (is_interintra_wedge_used(bsize)) {
      const uint8_t *mask =
          av1_get_contiguous_soft_mask(wedge_index, wedge_sign, bsize);
      const int subh = 2 * num_4x4_blocks_high_lookup[bsize] == bh;
      const int subw = 2 * num_4x4_blocks_wide_lookup[bsize] == bw;
      aom_highbd_blend_a64_mask(comppred8, compstride, intrapred8, intrastride,
                                interpred8, interstride, mask,
                                block_size_wide[bsize], bh, bw, subh, subw, bd);
    }
    return;
  }

  uint8_t mask[MAX_SB_SQUARE];
  build_smooth_interintra_mask(mask, bw, plane_bsize, mode);
  aom_highbd_blend_a64_mask(comppred8, compstride, intrapred8, intrastride,
                            interpred8, interstride, mask, bw, bh, bw, 0, 0,
                            bd);
}

void av1_build_intra_predictors_for_interintra(const AV1_COMMON *cm,
                                               MACROBLOCKD *xd,
                                               BLOCK_SIZE bsize, int plane,
                                               BUFFER_SET *ctx, uint8_t *dst,
                                               int dst_stride) {
  struct macroblockd_plane *const pd = &xd->plane[plane];
  BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, &xd->plane[plane]);
  PREDICTION_MODE mode =
      interintra_to_intra_mode[xd->mi[0]->mbmi.interintra_mode];
  xd->mi[0]->mbmi.angle_delta[0] = 0;
  xd->mi[0]->mbmi.angle_delta[1] = 0;
#if CONFIG_FILTER_INTRA
  xd->mi[0]->mbmi.filter_intra_mode_info.use_filter_intra = 0;
#endif  // CONFIG_FILTER_INTRA
#if CONFIG_INTRABC
  xd->mi[0]->mbmi.use_intrabc = 0;
#endif

  av1_predict_intra_block(
      cm, xd, pd->width, pd->height, get_max_rect_tx_size(plane_bsize, 0), mode,
      ctx->plane[plane], ctx->stride[plane], dst, dst_stride, 0, 0, plane);
}

void av1_combine_interintra(MACROBLOCKD *xd, BLOCK_SIZE bsize, int plane,
                            const uint8_t *inter_pred, int inter_stride,
                            const uint8_t *intra_pred, int intra_stride) {
  const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, &xd->plane[plane]);
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    combine_interintra_highbd(
        xd->mi[0]->mbmi.interintra_mode, xd->mi[0]->mbmi.use_wedge_interintra,
        xd->mi[0]->mbmi.interintra_wedge_index,
        xd->mi[0]->mbmi.interintra_wedge_sign, bsize, plane_bsize,
        xd->plane[plane].dst.buf, xd->plane[plane].dst.stride, inter_pred,
        inter_stride, intra_pred, intra_stride, xd->bd);
    return;
  }
  combine_interintra(xd->mi[0]->mbmi.interintra_mode,
                     xd->mi[0]->mbmi.use_wedge_interintra,
                     xd->mi[0]->mbmi.interintra_wedge_index,
                     xd->mi[0]->mbmi.interintra_wedge_sign, bsize, plane_bsize,
                     xd->plane[plane].dst.buf, xd->plane[plane].dst.stride,
                     inter_pred, inter_stride, intra_pred, intra_stride);
}

void av1_build_interintra_predictors_sby(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                         uint8_t *ypred, int ystride,
                                         BUFFER_SET *ctx, BLOCK_SIZE bsize) {
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    DECLARE_ALIGNED(16, uint16_t, intrapredictor[MAX_SB_SQUARE]);
    av1_build_intra_predictors_for_interintra(
        cm, xd, bsize, 0, ctx, CONVERT_TO_BYTEPTR(intrapredictor), MAX_SB_SIZE);
    av1_combine_interintra(xd, bsize, 0, ypred, ystride,
                           CONVERT_TO_BYTEPTR(intrapredictor), MAX_SB_SIZE);
    return;
  }
  {
    DECLARE_ALIGNED(16, uint8_t, intrapredictor[MAX_SB_SQUARE]);
    av1_build_intra_predictors_for_interintra(cm, xd, bsize, 0, ctx,
                                              intrapredictor, MAX_SB_SIZE);
    av1_combine_interintra(xd, bsize, 0, ypred, ystride, intrapredictor,
                           MAX_SB_SIZE);
  }
}

void av1_build_interintra_predictors_sbc(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                         uint8_t *upred, int ustride,
                                         BUFFER_SET *ctx, int plane,
                                         BLOCK_SIZE bsize) {
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    DECLARE_ALIGNED(16, uint16_t, uintrapredictor[MAX_SB_SQUARE]);
    av1_build_intra_predictors_for_interintra(
        cm, xd, bsize, plane, ctx, CONVERT_TO_BYTEPTR(uintrapredictor),
        MAX_SB_SIZE);
    av1_combine_interintra(xd, bsize, plane, upred, ustride,
                           CONVERT_TO_BYTEPTR(uintrapredictor), MAX_SB_SIZE);
  } else {
    DECLARE_ALIGNED(16, uint8_t, uintrapredictor[MAX_SB_SQUARE]);
    av1_build_intra_predictors_for_interintra(cm, xd, bsize, plane, ctx,
                                              uintrapredictor, MAX_SB_SIZE);
    av1_combine_interintra(xd, bsize, plane, upred, ustride, uintrapredictor,
                           MAX_SB_SIZE);
  }
}

void av1_build_interintra_predictors_sbuv(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                          uint8_t *upred, uint8_t *vpred,
                                          int ustride, int vstride,
                                          BUFFER_SET *ctx, BLOCK_SIZE bsize) {
  av1_build_interintra_predictors_sbc(cm, xd, upred, ustride, ctx, 1, bsize);
  av1_build_interintra_predictors_sbc(cm, xd, vpred, vstride, ctx, 2, bsize);
}

void av1_build_interintra_predictors(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                     uint8_t *ypred, uint8_t *upred,
                                     uint8_t *vpred, int ystride, int ustride,
                                     int vstride, BUFFER_SET *ctx,
                                     BLOCK_SIZE bsize) {
  av1_build_interintra_predictors_sby(cm, xd, ypred, ystride, ctx, bsize);
  av1_build_interintra_predictors_sbuv(cm, xd, upred, vpred, ustride, vstride,
                                       ctx, bsize);
}

// Builds the inter-predictor for the single ref case
// for use in the encoder to search the wedges efficiently.
static void build_inter_predictors_single_buf(MACROBLOCKD *xd, int plane,
                                              int bw, int bh, int x, int y,
                                              int w, int h, int mi_x, int mi_y,
                                              int ref, uint8_t *const ext_dst,
                                              int ext_dst_stride) {
  struct macroblockd_plane *const pd = &xd->plane[plane];
  const MODE_INFO *mi = xd->mi[0];

  const struct scale_factors *const sf = &xd->block_refs[ref]->sf;
  struct buf_2d *const pre_buf = &pd->pre[ref];
  uint8_t *const dst =
      (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH ? CONVERT_TO_BYTEPTR(ext_dst)
                                                   : ext_dst) +
      ext_dst_stride * y + x;
  const MV mv = mi->mbmi.mv[ref].as_mv;
  uint8_t *pre;
  int xs, ys, subpel_x, subpel_y;
  const int is_scaled = av1_is_scaled(sf);
  ConvolveParams conv_params = get_conv_params(ref, 0, plane);
  WarpTypesAllowed warp_types;
  const WarpedMotionParams *const wm =
      &xd->global_motion[mi->mbmi.ref_frame[ref]];
  warp_types.global_warp_allowed = is_global_mv_block(mi, wm->wmtype);
  warp_types.local_warp_allowed = mi->mbmi.motion_mode == WARPED_CAUSAL;

  if (is_scaled) {
    int ssx = pd->subsampling_x;
    int ssy = pd->subsampling_y;
    int orig_pos_y = (mi_y << (SUBPEL_BITS - ssy)) + (y << SUBPEL_BITS);
    orig_pos_y += mv.row * (1 << (1 - ssy));
    int orig_pos_x = (mi_x << (SUBPEL_BITS - ssx)) + (x << SUBPEL_BITS);
    orig_pos_x += mv.col * (1 << (1 - ssx));
    int pos_y = sf->scale_value_y(orig_pos_y, sf);
    int pos_x = sf->scale_value_x(orig_pos_x, sf);
    pos_x += SCALE_EXTRA_OFF;
    pos_y += SCALE_EXTRA_OFF;

    const int top = -AOM_LEFT_TOP_MARGIN_SCALED(ssy);
    const int left = -AOM_LEFT_TOP_MARGIN_SCALED(ssx);
    const int bottom = (pre_buf->height + AOM_INTERP_EXTEND)
                       << SCALE_SUBPEL_BITS;
    const int right = (pre_buf->width + AOM_INTERP_EXTEND) << SCALE_SUBPEL_BITS;
    pos_y = clamp(pos_y, top, bottom);
    pos_x = clamp(pos_x, left, right);

    pre = pre_buf->buf0 + (pos_y >> SCALE_SUBPEL_BITS) * pre_buf->stride +
          (pos_x >> SCALE_SUBPEL_BITS);
    subpel_x = pos_x & SCALE_SUBPEL_MASK;
    subpel_y = pos_y & SCALE_SUBPEL_MASK;
    xs = sf->x_step_q4;
    ys = sf->y_step_q4;
  } else {
    const MV mv_q4 = clamp_mv_to_umv_border_sb(
        xd, &mv, bw, bh, pd->subsampling_x, pd->subsampling_y);
    xs = ys = SCALE_SUBPEL_SHIFTS;
    subpel_x = (mv_q4.col & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    subpel_y = (mv_q4.row & SUBPEL_MASK) << SCALE_EXTRA_BITS;
    pre = pre_buf->buf + (y + (mv_q4.row >> SUBPEL_BITS)) * pre_buf->stride +
          (x + (mv_q4.col >> SUBPEL_BITS));
  }

  av1_make_inter_predictor(
      pre, pre_buf->stride, dst, ext_dst_stride, subpel_x, subpel_y, sf, w, h,
      &conv_params, mi->mbmi.interp_filters, &warp_types,
      (mi_x >> pd->subsampling_x) + x, (mi_y >> pd->subsampling_y) + y, plane,
      ref, mi, 0, xs, ys, xd);
}

void av1_build_inter_predictors_for_planes_single_buf(
    MACROBLOCKD *xd, BLOCK_SIZE bsize, int plane_from, int plane_to, int mi_row,
    int mi_col, int ref, uint8_t *ext_dst[3], int ext_dst_stride[3]) {
  int plane;
  const int mi_x = mi_col * MI_SIZE;
  const int mi_y = mi_row * MI_SIZE;
  for (plane = plane_from; plane <= plane_to; ++plane) {
    const BLOCK_SIZE plane_bsize =
        get_plane_block_size(bsize, &xd->plane[plane]);
    const int bw = block_size_wide[plane_bsize];
    const int bh = block_size_high[plane_bsize];
    build_inter_predictors_single_buf(xd, plane, bw, bh, 0, 0, bw, bh, mi_x,
                                      mi_y, ref, ext_dst[plane],
                                      ext_dst_stride[plane]);
  }
}

static void build_wedge_inter_predictor_from_buf(
    MACROBLOCKD *xd, int plane, int x, int y, int w, int h, uint8_t *ext_dst0,
    int ext_dst_stride0, uint8_t *ext_dst1, int ext_dst_stride1) {
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  const int is_compound = has_second_ref(mbmi);
  MACROBLOCKD_PLANE *const pd = &xd->plane[plane];
  struct buf_2d *const dst_buf = &pd->dst;
  uint8_t *const dst = dst_buf->buf + dst_buf->stride * y + x;
  const INTERINTER_COMPOUND_DATA comp_data = { mbmi->wedge_index,
                                               mbmi->wedge_sign,
                                               mbmi->mask_type, xd->seg_mask,
                                               mbmi->interinter_compound_type };

  if (is_compound && is_masked_compound_type(mbmi->interinter_compound_type)) {
    if (!plane && comp_data.interinter_compound_type == COMPOUND_SEG) {
      if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
        build_compound_seg_mask_highbd(
            comp_data.seg_mask, comp_data.mask_type,
            CONVERT_TO_BYTEPTR(ext_dst0), ext_dst_stride0,
            CONVERT_TO_BYTEPTR(ext_dst1), ext_dst_stride1, mbmi->sb_type, h, w,
            xd->bd);
      else
        build_compound_seg_mask(comp_data.seg_mask, comp_data.mask_type,
                                ext_dst0, ext_dst_stride0, ext_dst1,
                                ext_dst_stride1, mbmi->sb_type, h, w);
    }

    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
      build_masked_compound_highbd(
          dst, dst_buf->stride, CONVERT_TO_BYTEPTR(ext_dst0), ext_dst_stride0,
          CONVERT_TO_BYTEPTR(ext_dst1), ext_dst_stride1, &comp_data,
          mbmi->sb_type, h, w, xd->bd);
    else
      build_masked_compound(dst, dst_buf->stride, ext_dst0, ext_dst_stride0,
                            ext_dst1, ext_dst_stride1, &comp_data,
                            mbmi->sb_type, h, w);
  } else {
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH)
      aom_highbd_convolve_copy(CONVERT_TO_BYTEPTR(ext_dst0), ext_dst_stride0,
                               dst, dst_buf->stride, NULL, 0, NULL, 0, w, h,
                               xd->bd);
    else
      aom_convolve_copy(ext_dst0, ext_dst_stride0, dst, dst_buf->stride, NULL,
                        0, NULL, 0, w, h);
  }
}

void av1_build_wedge_inter_predictor_from_buf(MACROBLOCKD *xd, BLOCK_SIZE bsize,
                                              int plane_from, int plane_to,
                                              uint8_t *ext_dst0[3],
                                              int ext_dst_stride0[3],
                                              uint8_t *ext_dst1[3],
                                              int ext_dst_stride1[3]) {
  int plane;
  for (plane = plane_from; plane <= plane_to; ++plane) {
    const BLOCK_SIZE plane_bsize =
        get_plane_block_size(bsize, &xd->plane[plane]);
    const int bw = block_size_wide[plane_bsize];
    const int bh = block_size_high[plane_bsize];
    build_wedge_inter_predictor_from_buf(
        xd, plane, 0, 0, bw, bh, ext_dst0[plane], ext_dst_stride0[plane],
        ext_dst1[plane], ext_dst_stride1[plane]);
  }
}
