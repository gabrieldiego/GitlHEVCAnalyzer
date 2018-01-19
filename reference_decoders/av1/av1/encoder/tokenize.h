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

#ifndef AV1_ENCODER_TOKENIZE_H_
#define AV1_ENCODER_TOKENIZE_H_

#include "av1/common/entropy.h"
#include "av1/encoder/block.h"
#include "aom_dsp/bitwriter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EOSB_TOKEN 127  // Not signalled, encoder only

typedef int32_t EXTRABIT;

typedef struct {
  int16_t token;
  EXTRABIT extra;
} TOKENVALUE;

typedef struct {
  aom_cdf_prob (*tail_cdf)[CDF_SIZE(ENTROPY_TOKENS)];
  aom_cdf_prob (*head_cdf)[CDF_SIZE(ENTROPY_TOKENS)];
  aom_cdf_prob *color_map_cdf;
  // TODO(yaowu: use packed enum type if appropriate)
  int8_t eob_val;
  int8_t first_val;
  const aom_prob *context_tree;
  EXTRABIT extra;
  uint8_t token;
} TOKENEXTRA;

int av1_is_skippable_in_plane(MACROBLOCK *x, BLOCK_SIZE bsize, int plane);

struct AV1_COMP;
struct ThreadData;

struct tokenize_b_args {
  const struct AV1_COMP *cpi;
  struct ThreadData *td;
  TOKENEXTRA **tp;
  int this_rate;
  uint8_t allow_update_cdf;
};

typedef enum {
  OUTPUT_ENABLED = 0,
  DRY_RUN_NORMAL,
  DRY_RUN_COSTCOEFFS,
} RUN_TYPE;

// Note in all the tokenize functions rate if non NULL is incremented
// with the coefficient token cost only if dry_run = DRY_RUN_COSTCOEFS,
// otherwise rate is not incremented.
void av1_tokenize_sb_vartx(const struct AV1_COMP *cpi, struct ThreadData *td,
                           TOKENEXTRA **t, RUN_TYPE dry_run, int mi_row,
                           int mi_col, BLOCK_SIZE bsize, int *rate,
                           uint8_t allow_update_cdf);

int av1_cost_color_map(const MACROBLOCK *const x, int plane, BLOCK_SIZE bsize,
                       TX_SIZE tx_size, COLOR_MAP_TYPE type);

void av1_tokenize_color_map(const MACROBLOCK *const x, int plane,
                            TOKENEXTRA **t, BLOCK_SIZE bsize, TX_SIZE tx_size,
                            COLOR_MAP_TYPE type);

void av1_tokenize_sb(const struct AV1_COMP *cpi, struct ThreadData *td,
                     TOKENEXTRA **t, RUN_TYPE dry_run, BLOCK_SIZE bsize,
                     int *rate, const int mi_row, const int mi_col,
                     uint8_t allow_update_cdf);

extern const int16_t *av1_dct_value_cost_ptr;
/* TODO: The Token field should be broken out into a separate char array to
 *  improve cache locality, since it's needed for costing when the rest of the
 *  fields are not.
 */
extern const TOKENVALUE *av1_dct_value_tokens_ptr;
extern const TOKENVALUE *av1_dct_cat_lt_10_value_tokens;
extern const int *av1_dct_cat_lt_10_value_cost;
extern const int16_t av1_cat6_low_cost[256];
#define CAT6_HIGH_COST_ENTRIES 1024
extern const int av1_cat6_high_cost[CAT6_HIGH_COST_ENTRIES];
extern const uint8_t av1_cat6_skipped_bits_discount[8];

static INLINE void av1_get_token_extra(int v, int16_t *token, EXTRABIT *extra) {
  if (v >= CAT6_MIN_VAL || v <= -CAT6_MIN_VAL) {
    *token = CATEGORY6_TOKEN;
    if (v >= CAT6_MIN_VAL)
      *extra = 2 * v - 2 * CAT6_MIN_VAL;
    else
      *extra = -2 * v - 2 * CAT6_MIN_VAL + 1;
    return;
  }
  *token = av1_dct_cat_lt_10_value_tokens[v].token;
  *extra = av1_dct_cat_lt_10_value_tokens[v].extra;
}
static INLINE int16_t av1_get_token(int v) {
  if (v >= CAT6_MIN_VAL || v <= -CAT6_MIN_VAL) return 10;
  return av1_dct_cat_lt_10_value_tokens[v].token;
}

static INLINE int av1_get_token_cost(int v, int16_t *token, int cat6_bits) {
  if (v >= CAT6_MIN_VAL || v <= -CAT6_MIN_VAL) {
    EXTRABIT extrabits;
    *token = CATEGORY6_TOKEN;
    extrabits = abs(v) - CAT6_MIN_VAL;
    return av1_cat6_low_cost[extrabits & 0xff] +
           av1_cat6_high_cost[extrabits >> 8] -
           av1_cat6_skipped_bits_discount[18 - cat6_bits];
  }
  *token = av1_dct_cat_lt_10_value_tokens[v].token;
  return av1_dct_cat_lt_10_value_cost[v];
}

static INLINE int av1_get_tx_eob(const struct segmentation *seg, int segment_id,
                                 TX_SIZE tx_size) {
  const int eob_max = av1_get_max_eob(tx_size);
  return segfeature_active(seg, segment_id, SEG_LVL_SKIP) ? 0 : eob_max;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AV1_ENCODER_TOKENIZE_H_
