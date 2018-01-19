/*
 * Copyright (c) 2017, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <immintrin.h>

#include "./av1_rtcd.h"

#include "av1/common/cfl.h"

/**
 * Subtracts avg_q3 from the active part of the CfL prediction buffer.
 *
 * The CfL prediction buffer is always of size CFL_BUF_SQUARE. However, the
 * active area is specified using width and height.
 *
 * Note: We don't need to worry about going over the active area, as long as we
 * stay inside the CfL prediction buffer.
 */
void av1_cfl_subtract_avx2(int16_t *pred_buf_q3, int width, int height,
                           int16_t avg_q3) {
  const __m256i avg_x16 = _mm256_set1_epi16(avg_q3);

  // Sixteen int16 values fit in one __m256i register. If this is enough to do
  // the entire row, we move to the next row (stride ==32), otherwise we move to
  // the next sixteen values.
  //   width   next
  //     4      32
  //     8      32
  //    16      32
  //    32      16
  const int stride = CFL_BUF_LINE >> (width == 32);

  const int16_t *end = pred_buf_q3 + height * CFL_BUF_LINE;
  do {
    __m256i val_x16 = _mm256_loadu_si256((__m256i *)pred_buf_q3);
    _mm256_storeu_si256((__m256i *)pred_buf_q3,
                        _mm256_sub_epi16(val_x16, avg_x16));
  } while ((pred_buf_q3 += stride) < end);
}

/**
 * Adds 4 pixels (in a 2x2 grid) and multiplies them by 2. Resulting in a more
 * precise version of a box filter 4:2:0 pixel subsampling in Q3.
 *
 * The CfL prediction buffer is always of size CFL_BUF_SQUARE. However, the
 * active area is specified using width and height.
 *
 * Note: We don't need to worry about going over the active area, as long as we
 * stay inside the CfL prediction buffer.
 *
 * Note: For 4:2:0 luma subsampling, the width will never be greater than 16.
 */
static void cfl_luma_subsampling_420_lbd_avx2(const uint8_t *input,
                                              int input_stride,
                                              int16_t *pred_buf_q3, int width,
                                              int height) {
  (void)width;  // Max chroma width is 16, so all widths fit in one __m256i

  const __m256i twos = _mm256_set1_epi8(2);  // Thirty two twos
  const int luma_stride = input_stride << 1;
  const int16_t *end = pred_buf_q3 + height * CFL_BUF_LINE;
  do {
    // Load 32 values for the top and bottom rows.
    // t_0, t_1, ... t_31
    __m256i top = _mm256_loadu_si256((__m256i *)(input));
    // b_0, b_1, ... b_31
    __m256i bot = _mm256_loadu_si256((__m256i *)(input + input_stride));

    // Horizontal add of the 32 values into 16 values that are multiplied by 2
    // (t_0 + t_1) * 2, (t_2 + t_3) * 2, ... (t_30 + t_31) *2
    top = _mm256_maddubs_epi16(top, twos);
    // (b_0 + b_1) * 2, (b_2 + b_3) * 2, ... (b_30 + b_31) *2
    bot = _mm256_maddubs_epi16(bot, twos);

    // Add the 16 values in top with the 16 values in bottom
    _mm256_storeu_si256((__m256i *)pred_buf_q3, _mm256_add_epi16(top, bot));

    input += luma_stride;
    pred_buf_q3 += CFL_BUF_LINE;
  } while (pred_buf_q3 < end);
}

cfl_subsample_lbd_fn get_subsample_lbd_fn_avx2(int sub_x, int sub_y) {
  static const cfl_subsample_lbd_fn subsample_lbd[2][2] = {
    //  (sub_y == 0, sub_x == 0)       (sub_y == 0, sub_x == 1)
    //  (sub_y == 1, sub_x == 0)       (sub_y == 1, sub_x == 1)
    { cfl_luma_subsampling_444_lbd, cfl_luma_subsampling_422_lbd },
    { cfl_luma_subsampling_440_lbd, cfl_luma_subsampling_420_lbd_avx2 },
  };
  // AND sub_x and sub_y with 1 to ensures that an attacker won't be able to
  // index the function pointer array out of bounds.
  return subsample_lbd[sub_y & 1][sub_x & 1];
}

static INLINE __m256i predict_unclipped(const __m256i *input, __m256i alpha_q12,
                                        __m256i alpha_sign, __m256i dc_q0) {
  __m256i ac_q3 = _mm256_loadu_si256(input);
  __m256i ac_sign = _mm256_sign_epi16(alpha_sign, ac_q3);
  __m256i scaled_luma_q0 =
      _mm256_mulhrs_epi16(_mm256_abs_epi16(ac_q3), alpha_q12);
  scaled_luma_q0 = _mm256_sign_epi16(scaled_luma_q0, ac_sign);
  return _mm256_add_epi16(scaled_luma_q0, dc_q0);
}

static INLINE void cfl_predict_lbd_x(const int16_t *pred_buf_q3, uint8_t *dst,
                                     int dst_stride, TX_SIZE tx_size,
                                     int alpha_q3, int width) {
  const int16_t *row_end = pred_buf_q3 + tx_size_high[tx_size] * CFL_BUF_LINE;
  const __m256i alpha_sign = _mm256_set1_epi16(alpha_q3);
  const __m256i alpha_q12 = _mm256_slli_epi16(_mm256_abs_epi16(alpha_sign), 9);
  const __m256i dc_q0 = _mm256_set1_epi16(*dst);
  do {
    __m256i res =
        predict_unclipped((__m256i *)pred_buf_q3, alpha_q12, alpha_sign, dc_q0);
    __m256i next = res;
    if (width == 32)
      next = predict_unclipped((__m256i *)(pred_buf_q3 + 16), alpha_q12,
                               alpha_sign, dc_q0);
    res = _mm256_packus_epi16(res, next);
    if (width == 4) {
      *(int32_t *)dst = _mm256_extract_epi32(res, 0);
    } else if (width == 8) {
#ifdef __x86_64__
      *(int64_t *)dst = _mm256_extract_epi64(res, 0);
#else
      _mm_storel_epi64((__m128i *)dst, _mm256_castsi256_si128(res));
#endif
    } else {
      res = _mm256_permute4x64_epi64(res, _MM_SHUFFLE(3, 1, 2, 0));
      if (width == 16)
        _mm_storeu_si128((__m128i *)dst, _mm256_castsi256_si128(res));
      else
        _mm256_storeu_si256((__m256i *)dst, res);
    }
    dst += dst_stride;
    pred_buf_q3 += CFL_BUF_LINE;
  } while (pred_buf_q3 < row_end);
}

static __m256i highbd_max_epi16(int bd) {
  const __m256i neg_one = _mm256_set1_epi16(-1);
  // (1 << bd) - 1 => -(-1 << bd) -1 => -1 - (-1 << bd) => -1 ^ (-1 << bd)
  return _mm256_xor_si256(_mm256_slli_epi16(neg_one, bd), neg_one);
}

static __m256i highbd_clamp_epi16(__m256i u, __m256i zero, __m256i max) {
  return _mm256_max_epi16(_mm256_min_epi16(u, max), zero);
}

static INLINE void cfl_predict_hbd_x(const int16_t *pred_buf_q3, uint16_t *dst,
                                     int dst_stride, TX_SIZE tx_size,
                                     int alpha_q3, int bd, int width) {
  const int16_t *row_end = pred_buf_q3 + tx_size_high[tx_size] * CFL_BUF_LINE;
  const __m256i alpha_sign = _mm256_set1_epi16(alpha_q3);
  const __m256i alpha_q12 = _mm256_slli_epi16(_mm256_abs_epi16(alpha_sign), 9);
  const __m256i dc_q0 = _mm256_loadu_si256((__m256i *)dst);
  const __m256i max = highbd_max_epi16(bd);
  const __m256i zero = _mm256_setzero_si256();
  do {
    __m256i res =
        predict_unclipped((__m256i *)pred_buf_q3, alpha_q12, alpha_sign, dc_q0);
    res = highbd_clamp_epi16(res, zero, max);
    if (width == 4)
#ifdef __x86_64__
      *(int64_t *)dst = _mm256_extract_epi64(res, 0);
#else
      _mm_storel_epi64((__m128i *)dst, _mm256_castsi256_si128(res));
#endif
    else if (width == 8)
      _mm_storeu_si128((__m128i *)dst, _mm256_castsi256_si128(res));
    else
      _mm256_storeu_si256((__m256i *)dst, res);
    if (width == 32) {
      res = predict_unclipped((__m256i *)(pred_buf_q3 + 16), alpha_q12,
                              alpha_sign, dc_q0);
      res = highbd_clamp_epi16(res, zero, max);
      _mm256_storeu_si256((__m256i *)(dst + 16), res);
    }
    dst += dst_stride;
    pred_buf_q3 += CFL_BUF_LINE;
  } while (pred_buf_q3 < row_end);
}

#define CFL_PREDICT_LBD_X(width)                                               \
  static void cfl_predict_lbd_##width(const int16_t *pred_buf_q3,              \
                                      uint8_t *dst, int dst_stride,            \
                                      TX_SIZE tx_size, int alpha_q3) {         \
    cfl_predict_lbd_x(pred_buf_q3, dst, dst_stride, tx_size, alpha_q3, width); \
  }

CFL_PREDICT_LBD_X(4)
CFL_PREDICT_LBD_X(8)
CFL_PREDICT_LBD_X(16)
CFL_PREDICT_LBD_X(32)

#define CFL_PREDICT_HBD_X(width)                                               \
  static void cfl_predict_hbd_##width(const int16_t *pred_buf_q3,              \
                                      uint16_t *dst, int dst_stride,           \
                                      TX_SIZE tx_size, int alpha_q3, int bd) { \
    cfl_predict_hbd_x(pred_buf_q3, dst, dst_stride, tx_size, alpha_q3, bd,     \
                      width);                                                  \
  }

CFL_PREDICT_HBD_X(4)
CFL_PREDICT_HBD_X(8)
CFL_PREDICT_HBD_X(16)
CFL_PREDICT_HBD_X(32)

cfl_predict_lbd_fn get_predict_lbd_fn_avx2(TX_SIZE tx_size) {
  static const cfl_predict_lbd_fn predict_lbd[4] = {
    cfl_predict_lbd_4, cfl_predict_lbd_8, cfl_predict_lbd_16, cfl_predict_lbd_32
  };
  return predict_lbd[(tx_size_wide_log2[tx_size] - tx_size_wide_log2[0]) & 3];
}

cfl_predict_hbd_fn get_predict_hbd_fn_avx2(TX_SIZE tx_size) {
  static const cfl_predict_hbd_fn predict_hbd[4] = {
    cfl_predict_hbd_4, cfl_predict_hbd_8, cfl_predict_hbd_16, cfl_predict_hbd_32
  };
  return predict_hbd[(tx_size_wide_log2[tx_size] - tx_size_wide_log2[0]) & 3];
}
