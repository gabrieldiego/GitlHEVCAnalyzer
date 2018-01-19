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

#ifndef AV1_COMMON_QUANT_COMMON_H_
#define AV1_COMMON_QUANT_COMMON_H_

#include "aom/aom_codec.h"
#include "av1/common/seg_common.h"
#include "av1/common/enums.h"
#include "av1/common/entropy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MINQ 0
#define MAXQ 255
#define QINDEX_RANGE (MAXQ - MINQ + 1)
#define QINDEX_BITS 8
#if CONFIG_AOM_QM
// Total number of QM sets stored
#define QM_LEVEL_BITS 4
#define NUM_QM_LEVELS (1 << QM_LEVEL_BITS)
/* Range of QMS is between first and last value, with offset applied to inter
 * blocks*/
#define DEFAULT_QM_FIRST 5
#define DEFAULT_QM_LAST 9
#endif

struct AV1Common;

int16_t av1_dc_quant_Q3(int qindex, int delta, aom_bit_depth_t bit_depth);
int16_t av1_ac_quant_Q3(int qindex, int delta, aom_bit_depth_t bit_depth);
int16_t av1_dc_quant_QTX(int qindex, int delta, aom_bit_depth_t bit_depth);
int16_t av1_ac_quant_QTX(int qindex, int delta, aom_bit_depth_t bit_depth);
int16_t av1_qindex_from_ac_Q3(int ac_Q3, aom_bit_depth_t bit_depth);

int av1_get_qindex(const struct segmentation *seg, int segment_id,
                   int base_qindex);
#if CONFIG_AOM_QM
// Reduce the large number of quantizers to a smaller number of levels for which
// different matrices may be defined
static INLINE int aom_get_qmlevel(int qindex, int first, int last) {
  return first + (qindex * (last + 1 - first)) / QINDEX_RANGE;
}
void aom_qm_init(struct AV1Common *cm);
const qm_val_t *aom_iqmatrix(struct AV1Common *cm, int qindex, int comp,
                             TX_SIZE tx_size);
const qm_val_t *aom_qmatrix(struct AV1Common *cm, int qindex, int comp,
                            TX_SIZE tx_size);
#endif  // CONFIG_AOM_QM

#if CONFIG_NEW_QUANT

#define QUANT_PROFILES ((DQ_TYPES - 1) * 8 + 1)
#define QUANT_RANGES 2
#define NUQ_KNOTS 1

// Encoder only
#define X0_PROFILES (2 * 8 + 1)

// dequant_val_type_nuq needs space for the 3 possible shift values
// for different tx sizes
typedef tran_low_t dequant_val_type_nuq[NUQ_KNOTS * 3];
typedef tran_low_t cuml_bins_type_nuq[NUQ_KNOTS];
void av1_get_dequant_val_nuq(int q, int is_ac_coeff, tran_low_t *dq,
                             int dq_off_index);
void av1_get_cuml_bins_nuq(int q, int is_ac_coeff, tran_low_t *cuml_bins,
                           int q_profile);
tran_low_t av1_dequant_abscoeff_nuq(int v, int q, const tran_low_t *dq,
                                    int shift);
tran_low_t av1_dequant_coeff_nuq(int v, int q, const tran_low_t *dq, int shift);

static INLINE int qindex_to_qrange(int qindex) {
  return (qindex < 140 ? 1 : 0);
}

static INLINE int get_dq_profile(DqType dqtype, int qindex, int is_inter,
                                 PLANE_TYPE plane_type) {
  // intra/inter, Y/UV, ctx, qrange
  static const int
      dq_profile_lookup[DQ_TYPES][REF_TYPES][PLANE_TYPES][QUANT_RANGES] = {
        {
            { { 0, 0 }, { 0, 0 } },  // intra: Y, UV
            { { 0, 0 }, { 0, 0 } },  // inter: Y, UV
        },
        {
            { { 1, 2 }, { 3, 4 } },  // intra: Y, UV
            { { 5, 6 }, { 7, 8 } },  // inter: Y, UV
        },
        {
            { { 9, 10 }, { 11, 12 } },   // intra: Y, UV
            { { 13, 14 }, { 15, 16 } },  // inter: Y, UV
        },
        {
            { { 17, 18 }, { 19, 20 } },  // intra: Y, UV
            { { 21, 22 }, { 23, 24 } },  // inter: Y, UV
        },
      };
  if (!qindex) return 0;  // lossless
  if (!dqtype) return 0;  // DQ_MULT
  return dq_profile_lookup[dqtype][is_inter][plane_type]
                          [qindex_to_qrange(qindex)];
}

// Encoder only
static INLINE int get_x0_profile(int optimize, int qindex, int is_inter,
                                 PLANE_TYPE plane_type) {
  // intra/inter, Y/UV, ctx, qrange
  static const int x0_profile_lookup[2][REF_TYPES][PLANE_TYPES][QUANT_RANGES] =
      {
        {
            { { 1, 2 }, { 3, 4 } },  // intra: Y, UV
            { { 5, 6 }, { 7, 8 } },  // inter: Y, UV
        },
        {
            { { 9, 10 }, { 11, 12 } },   // intra: Y, UV
            { { 13, 14 }, { 15, 16 } },  // inter: Y, UV
        },
      };
  if (!qindex) return 0;  // lossless
  return x0_profile_lookup[!optimize][is_inter][plane_type]
                          [qindex_to_qrange(qindex)];
}
#endif  // CONFIG_NEW_QUANT

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV1_COMMON_QUANT_COMMON_H_
