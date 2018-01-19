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
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

#include "aom_ports/aom_timer.h"
#include "./av1_rtcd.h"
#include "test/util.h"
#include "test/acm_random.h"

using std::tr1::make_tuple;

using libaom_test::ACMRandom;

#define NUM_ITERATIONS (10)
#define NUM_ITERATIONS_SPEED (INT16_MAX)

#define ALL_CFL_SIZES(function)                                     \
  make_tuple(4, 4, &function), make_tuple(8, 4, &function),         \
      make_tuple(4, 8, &function), make_tuple(8, 8, &function),     \
      make_tuple(16, 8, &function), make_tuple(8, 16, &function),   \
      make_tuple(16, 16, &function), make_tuple(32, 16, &function), \
      make_tuple(16, 32, &function), make_tuple(32, 32, &function)

#define CHROMA_420_CFL_SIZES(function)                            \
  make_tuple(4, 4, &function), make_tuple(8, 4, &function),       \
      make_tuple(4, 8, &function), make_tuple(8, 8, &function),   \
      make_tuple(16, 8, &function), make_tuple(8, 16, &function), \
      make_tuple(16, 16, &function)

#define ALL_CFL_TX_SIZES(function)                                      \
  make_tuple(TX_4X4, &function), make_tuple(TX_4X8, &function),         \
      make_tuple(TX_8X4, &function), make_tuple(TX_8X8, &function),     \
      make_tuple(TX_8X16, &function), make_tuple(TX_16X8, &function),   \
      make_tuple(TX_16X16, &function), make_tuple(TX_16X32, &function), \
      make_tuple(TX_32X16, &function), make_tuple(TX_32X32, &function)

namespace {
typedef void (*subtract_fn)(int16_t *pred_buf_q3, int width, int height,
                            int16_t avg_q3);

typedef cfl_subsample_lbd_fn (*get_subsample_fn)(int width, int height);

typedef cfl_predict_lbd_fn (*get_predict_fn)(TX_SIZE tx_size);

typedef cfl_predict_hbd_fn (*get_predict_fn_hbd)(TX_SIZE tx_size);

typedef std::tr1::tuple<int, int, subtract_fn> subtract_param;

typedef std::tr1::tuple<int, int, get_subsample_fn> subsample_param;

typedef std::tr1::tuple<TX_SIZE, get_predict_fn> predict_param;

typedef std::tr1::tuple<TX_SIZE, get_predict_fn_hbd> predict_param_hbd;

static void assertFaster(int ref_elapsed_time, int elapsed_time) {
  EXPECT_GT(ref_elapsed_time, elapsed_time)
      << "Error: CFLSubtractSpeedTest, SIMD slower than C." << std::endl
      << "C time: " << ref_elapsed_time << " us" << std::endl
      << "SIMD time: " << elapsed_time << " us" << std::endl;
}

static void printSpeed(int ref_elapsed_time, int elapsed_time, int width,
                       int height) {
  std::cout.precision(2);
  std::cout << "[          ] " << width << "x" << height
            << ": C time = " << ref_elapsed_time
            << " us, SIMD time = " << elapsed_time << " us"
            << " (~" << ref_elapsed_time / (double)elapsed_time << "x) "
            << std::endl;
}

class CFLSubtractTest : public ::testing::TestWithParam<subtract_param> {
 public:
  virtual ~CFLSubtractTest() {}
  virtual void SetUp() { subtract = GET_PARAM(2); }

 protected:
  int Width() const { return GET_PARAM(0); }
  int Height() const { return GET_PARAM(1); }
  int16_t pred_buf_data[CFL_BUF_SQUARE];
  int16_t pred_buf_data_ref[CFL_BUF_SQUARE];
  subtract_fn subtract;
  void init(int width, int height) {
    int k = 0;
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        pred_buf_data[j * CFL_BUF_LINE + i] = k;
        pred_buf_data_ref[j * CFL_BUF_LINE + i] = k++;
      }
    }
  }
};

class CFLSubsampleTest : public ::testing::TestWithParam<subsample_param> {
 public:
  virtual ~CFLSubsampleTest() {}
  virtual void SetUp() { subsample = GET_PARAM(2); }

 protected:
  int Width() const { return GET_PARAM(0); }
  int Height() const { return GET_PARAM(1); }
  get_subsample_fn subsample;
  uint8_t luma_pels[CFL_BUF_SQUARE];
  uint8_t luma_pels_ref[CFL_BUF_SQUARE];
  int16_t sub_luma_pels[CFL_BUF_SQUARE];
  int16_t sub_luma_pels_ref[CFL_BUF_SQUARE];
  void init(int width, int height) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    for (int j = 0; j < height * 2; j++) {
      for (int i = 0; i < width * 2; i++) {
        const int val = rnd.Rand8();
        luma_pels[j * CFL_BUF_LINE + i] = val;
        luma_pels_ref[j * CFL_BUF_LINE + i] = val;
      }
    }
  }
};

class CFLPredictTest : public ::testing::TestWithParam<predict_param> {
 public:
  virtual ~CFLPredictTest() {}
  virtual void SetUp() {
    predict = GET_PARAM(1);
    chroma_pels_ref = reinterpret_cast<uint8_t *>(
        aom_memalign(32, sizeof(uint8_t) * CFL_BUF_SQUARE));
    sub_luma_pels_ref = reinterpret_cast<int16_t *>(
        aom_memalign(32, sizeof(int16_t) * CFL_BUF_SQUARE));
    chroma_pels = reinterpret_cast<uint8_t *>(
        aom_memalign(32, sizeof(uint8_t) * CFL_BUF_SQUARE));
    sub_luma_pels = reinterpret_cast<int16_t *>(
        aom_memalign(32, sizeof(int16_t) * CFL_BUF_SQUARE));
  }

  virtual void TearDown() {
    aom_free(chroma_pels_ref);
    aom_free(sub_luma_pels_ref);
    aom_free(chroma_pels);
    aom_free(sub_luma_pels);
  }

 protected:
  int Width() const { return tx_size_wide[GET_PARAM(0)]; }
  int Height() const { return tx_size_high[GET_PARAM(0)]; }
  TX_SIZE Tx_size() const { return GET_PARAM(0); }
  uint8_t *chroma_pels_ref;
  int16_t *sub_luma_pels_ref;
  uint8_t *chroma_pels;
  int16_t *sub_luma_pels;
  get_predict_fn predict;
  int alpha_q3;
  uint8_t dc;
  void init(int width, int height) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    alpha_q3 = rnd(33) - 16;
    dc = rnd.Rand8();
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        chroma_pels[j * CFL_BUF_LINE + i] = dc;
        chroma_pels_ref[j * CFL_BUF_LINE + i] = dc;
        sub_luma_pels_ref[j * CFL_BUF_LINE + i] =
            sub_luma_pels[j * CFL_BUF_LINE + i] = rnd.Rand8() - 128;
      }
    }
  }
};

class CFLPredictHBDTest : public ::testing::TestWithParam<predict_param_hbd> {
 public:
  virtual ~CFLPredictHBDTest() {}
  virtual void SetUp() {
    predict = GET_PARAM(1);
    chroma_pels_ref = reinterpret_cast<uint16_t *>(
        aom_memalign(32, sizeof(uint16_t) * CFL_BUF_SQUARE));
    sub_luma_pels_ref = reinterpret_cast<int16_t *>(
        aom_memalign(32, sizeof(int16_t) * CFL_BUF_SQUARE));
    chroma_pels = reinterpret_cast<uint16_t *>(
        aom_memalign(32, sizeof(uint16_t) * CFL_BUF_SQUARE));
    sub_luma_pels = reinterpret_cast<int16_t *>(
        aom_memalign(32, sizeof(int16_t) * CFL_BUF_SQUARE));
  }

  virtual void TearDown() {
    aom_free(chroma_pels_ref);
    aom_free(sub_luma_pels_ref);
    aom_free(chroma_pels);
    aom_free(sub_luma_pels);
  }

 protected:
  int Width() const { return tx_size_wide[GET_PARAM(0)]; }
  int Height() const { return tx_size_high[GET_PARAM(0)]; }
  TX_SIZE Tx_size() const { return GET_PARAM(0); }
  uint16_t *chroma_pels_ref;
  int16_t *sub_luma_pels_ref;
  uint16_t *chroma_pels;
  int16_t *sub_luma_pels;
  get_predict_fn_hbd predict;
  int bd;
  int alpha_q3;
  uint8_t dc;
  void init(int width, int height) {
    ACMRandom rnd(ACMRandom::DeterministicSeed());
    bd = 12;
    alpha_q3 = rnd(33) - 16;
    dc = rnd(1 << bd);
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        chroma_pels[j * CFL_BUF_LINE + i] = dc;
        chroma_pels_ref[j * CFL_BUF_LINE + i] = dc;
        sub_luma_pels_ref[j * CFL_BUF_LINE + i] =
            sub_luma_pels[j * CFL_BUF_LINE + i] =
                rnd(1 << bd) - (1 << (bd - 1));
      }
    }
  }
};

TEST_P(CFLSubtractTest, SubtractTest) {
  const int width = Width();
  const int height = Height();

  ACMRandom rnd(ACMRandom::DeterministicSeed());

  for (int it = 0; it < NUM_ITERATIONS; it++) {
    init(width, height);
    int16_t k = rnd.Rand15Signed();
    subtract(pred_buf_data, width, height, k);
    av1_cfl_subtract_c(pred_buf_data_ref, width, height, k);
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        ASSERT_EQ(pred_buf_data[j * CFL_BUF_LINE + i],
                  pred_buf_data_ref[j * CFL_BUF_LINE + i]);
        ASSERT_EQ(pred_buf_data[j * CFL_BUF_LINE + i], -k);
        k--;
      }
    }
  }
}

TEST_P(CFLSubtractTest, DISABLED_SubtractSpeedTest) {
  const int width = Width();
  const int height = Height();

  aom_usec_timer ref_timer;
  aom_usec_timer timer;

  init(width, height);
  aom_usec_timer_start(&ref_timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    av1_cfl_subtract_c(pred_buf_data_ref, width, height, k);
  }
  aom_usec_timer_mark(&ref_timer);
  const int ref_elapsed_time = (int)aom_usec_timer_elapsed(&ref_timer);

  aom_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    subtract(pred_buf_data, width, height, k);
  }
  aom_usec_timer_mark(&timer);
  const int elapsed_time = (int)aom_usec_timer_elapsed(&timer);

  printSpeed(ref_elapsed_time, elapsed_time, width, height);
  assertFaster(ref_elapsed_time, elapsed_time);
}

TEST_P(CFLSubsampleTest, SubsampleTest) {
  const int width = Width();
  const int height = Height();

  for (int it = 0; it < NUM_ITERATIONS; it++) {
    init(width, height);
    subsample(1, 1)(luma_pels, CFL_BUF_LINE, sub_luma_pels, width, height);
    get_subsample_lbd_fn_c(1, 1)(luma_pels_ref, CFL_BUF_LINE, sub_luma_pels_ref,
                                 width, height);
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        ASSERT_EQ(sub_luma_pels_ref[j * CFL_BUF_LINE + i],
                  sub_luma_pels[j * CFL_BUF_LINE + i]);
      }
    }
  }
}

TEST_P(CFLSubsampleTest, DISABLED_SubsampleSpeedTest) {
  const int width = Width();
  const int height = Height();

  aom_usec_timer ref_timer;
  aom_usec_timer timer;

  init(width, height);
  aom_usec_timer_start(&ref_timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    get_subsample_lbd_fn_c(1, 1)(luma_pels, CFL_BUF_LINE, sub_luma_pels, width,
                                 height);
  }
  aom_usec_timer_mark(&ref_timer);
  int ref_elapsed_time = (int)aom_usec_timer_elapsed(&ref_timer);

  aom_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    subsample(1, 1)(luma_pels_ref, CFL_BUF_LINE, sub_luma_pels_ref, width,
                    height);
  }
  aom_usec_timer_mark(&timer);
  int elapsed_time = (int)aom_usec_timer_elapsed(&timer);

  printSpeed(ref_elapsed_time, elapsed_time, width, height);
  assertFaster(ref_elapsed_time, elapsed_time);
}

TEST_P(CFLPredictTest, PredictTest) {
  const int width = Width();
  const int height = Height();
  const TX_SIZE tx_size = Tx_size();

  for (int it = 0; it < NUM_ITERATIONS; it++) {
    init(width, height);
    predict(tx_size)(sub_luma_pels, chroma_pels, CFL_BUF_LINE, tx_size,
                     alpha_q3);
    get_predict_lbd_fn_c(tx_size)(sub_luma_pels_ref, chroma_pels_ref,
                                  CFL_BUF_LINE, tx_size, alpha_q3);
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        ASSERT_EQ(chroma_pels_ref[j * CFL_BUF_LINE + i],
                  chroma_pels[j * CFL_BUF_LINE + i]);
      }
    }
  }
}

TEST_P(CFLPredictTest, DISABLED_PredictSpeedTest) {
  const int width = Width();
  const int height = Height();
  const TX_SIZE tx_size = Tx_size();

  aom_usec_timer ref_timer;
  aom_usec_timer timer;

  init(width, height);
  cfl_predict_lbd_fn predict_impl = get_predict_lbd_fn_c(tx_size);
  aom_usec_timer_start(&ref_timer);

  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    predict_impl(sub_luma_pels_ref, chroma_pels_ref, CFL_BUF_LINE, tx_size,
                 alpha_q3);
  }
  aom_usec_timer_mark(&ref_timer);
  int ref_elapsed_time = (int)aom_usec_timer_elapsed(&ref_timer);

  predict_impl = predict(tx_size);
  aom_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    predict_impl(sub_luma_pels, chroma_pels, CFL_BUF_LINE, tx_size, alpha_q3);
  }
  aom_usec_timer_mark(&timer);
  int elapsed_time = (int)aom_usec_timer_elapsed(&timer);

  printSpeed(ref_elapsed_time, elapsed_time, width, height);
  assertFaster(ref_elapsed_time, elapsed_time);
}

TEST_P(CFLPredictHBDTest, PredictHBDTest) {
  const int width = Width();
  const int height = Height();
  const TX_SIZE tx_size = Tx_size();

  for (int it = 0; it < NUM_ITERATIONS; it++) {
    init(width, height);
    predict(tx_size)(sub_luma_pels, chroma_pels, CFL_BUF_LINE, tx_size,
                     alpha_q3, bd);
    get_predict_hbd_fn_c(tx_size)(sub_luma_pels_ref, chroma_pels_ref,
                                  CFL_BUF_LINE, tx_size, alpha_q3, bd);
    for (int j = 0; j < height; j++) {
      for (int i = 0; i < width; i++) {
        ASSERT_EQ(chroma_pels_ref[j * CFL_BUF_LINE + i],
                  chroma_pels[j * CFL_BUF_LINE + i]);
      }
    }
  }
}

TEST_P(CFLPredictHBDTest, DISABLED_PredictHBDSpeedTest) {
  const int width = Width();
  const int height = Height();
  const TX_SIZE tx_size = Tx_size();

  aom_usec_timer ref_timer;
  aom_usec_timer timer;

  init(width, height);
  cfl_predict_hbd_fn predict_impl = get_predict_hbd_fn_c(tx_size);
  aom_usec_timer_start(&ref_timer);

  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    predict_impl(sub_luma_pels_ref, chroma_pels_ref, CFL_BUF_LINE, tx_size,
                 alpha_q3, bd);
  }
  aom_usec_timer_mark(&ref_timer);
  int ref_elapsed_time = (int)aom_usec_timer_elapsed(&ref_timer);

  predict_impl = predict(tx_size);
  aom_usec_timer_start(&timer);
  for (int k = 0; k < NUM_ITERATIONS_SPEED; k++) {
    predict_impl(sub_luma_pels, chroma_pels, CFL_BUF_LINE, tx_size, alpha_q3,
                 bd);
  }
  aom_usec_timer_mark(&timer);
  int elapsed_time = (int)aom_usec_timer_elapsed(&timer);

  printSpeed(ref_elapsed_time, elapsed_time, width, height);
  assertFaster(ref_elapsed_time, elapsed_time);
}

#if HAVE_SSE2
const subtract_param subtract_sizes_sse2[] = { ALL_CFL_SIZES(
    av1_cfl_subtract_sse2) };

INSTANTIATE_TEST_CASE_P(SSE2, CFLSubtractTest,
                        ::testing::ValuesIn(subtract_sizes_sse2));

#endif

#if HAVE_SSSE3
const subsample_param subsample_sizes_ssse3[] = { CHROMA_420_CFL_SIZES(
    get_subsample_lbd_fn_ssse3) };

const predict_param predict_sizes_ssse3[] = { ALL_CFL_TX_SIZES(
    get_predict_lbd_fn_ssse3) };

const predict_param_hbd predict_sizes_hbd_ssse3[] = { ALL_CFL_TX_SIZES(
    get_predict_hbd_fn_ssse3) };

INSTANTIATE_TEST_CASE_P(SSSE3, CFLSubsampleTest,
                        ::testing::ValuesIn(subsample_sizes_ssse3));

INSTANTIATE_TEST_CASE_P(SSSE3, CFLPredictTest,
                        ::testing::ValuesIn(predict_sizes_ssse3));

INSTANTIATE_TEST_CASE_P(SSSE3, CFLPredictHBDTest,
                        ::testing::ValuesIn(predict_sizes_hbd_ssse3));
#endif

#if HAVE_AVX2
const subtract_param subtract_sizes_avx2[] = { ALL_CFL_SIZES(
    av1_cfl_subtract_avx2) };

const subsample_param subsample_sizes_avx2[] = { CHROMA_420_CFL_SIZES(
    get_subsample_lbd_fn_avx2) };

const predict_param predict_sizes_avx2[] = { ALL_CFL_TX_SIZES(
    get_predict_lbd_fn_avx2) };

const predict_param_hbd predict_sizes_hbd_avx2[] = { ALL_CFL_TX_SIZES(
    get_predict_hbd_fn_avx2) };

INSTANTIATE_TEST_CASE_P(AVX2, CFLSubtractTest,
                        ::testing::ValuesIn(subtract_sizes_avx2));

INSTANTIATE_TEST_CASE_P(AVX2, CFLSubsampleTest,
                        ::testing::ValuesIn(subsample_sizes_avx2));

INSTANTIATE_TEST_CASE_P(AVX2, CFLPredictTest,
                        ::testing::ValuesIn(predict_sizes_avx2));

INSTANTIATE_TEST_CASE_P(AVX2, CFLPredictHBDTest,
                        ::testing::ValuesIn(predict_sizes_hbd_avx2));
#endif
}  // namespace
