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

#ifndef AV1_COMMON_RECONINTRA_H_
#define AV1_COMMON_RECONINTRA_H_

#include "aom/aom_integer.h"
#include "av1/common/blockd.h"
#include "av1/common/onyxc_int.h"

#ifdef __cplusplus
extern "C" {
#endif

void av1_init_intra_predictors(void);
void av1_predict_intra_block_facade(const AV1_COMMON *cm, MACROBLOCKD *xd,
                                    int plane, int blk_col, int blk_row,
                                    TX_SIZE tx_size);
void av1_predict_intra_block(const AV1_COMMON *cm, const MACROBLOCKD *xd,
                             int bw, int bh, TX_SIZE tx_size,
                             PREDICTION_MODE mode, const uint8_t *ref,
                             int ref_stride, uint8_t *dst, int dst_stride,
                             int aoff, int loff, int plane);

// Mapping of interintra to intra mode for use in the intra component
static const PREDICTION_MODE interintra_to_intra_mode[INTERINTRA_MODES] = {
  DC_PRED, V_PRED, H_PRED, SMOOTH_PRED
};

// Mapping of intra mode to the interintra mode
static const INTERINTRA_MODE intra_to_interintra_mode[INTRA_MODES] = {
  II_DC_PRED, II_V_PRED, II_H_PRED, II_V_PRED,      II_SMOOTH_PRED, II_V_PRED,
  II_H_PRED,  II_H_PRED, II_V_PRED, II_SMOOTH_PRED, II_SMOOTH_PRED
};

#if CONFIG_FILTER_INTRA
#define FILTER_INTRA_SCALE_BITS 4
#endif  // CONFIG_FILTER_INTRA

#define CONFIG_USE_ANGLE_DELTA_SUB8X8 0

static INLINE int av1_is_directional_mode(PREDICTION_MODE mode,
                                          BLOCK_SIZE bsize) {
#if CONFIG_INTRA_EDGE
  (void)bsize;
  return mode >= V_PRED && mode <= D63_PRED;
#else
  return mode >= V_PRED && mode <= D63_PRED && bsize >= BLOCK_8X8;
#endif
}

static INLINE int av1_use_angle_delta(BLOCK_SIZE bsize) {
  (void)bsize;
#if CONFIG_USE_ANGLE_DELTA_SUB8X8
  return 1;
#else
  return bsize >= BLOCK_8X8;
#endif
}

#if CONFIG_INTRABC
static INLINE int av1_allow_intrabc(const AV1_COMMON *const cm) {
  return cm->allow_screen_content_tools && cm->allow_intrabc;
}
#endif  // CONFIG_INTRABC

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // AV1_COMMON_RECONINTRA_H_
