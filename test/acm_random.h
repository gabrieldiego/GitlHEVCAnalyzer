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

#ifndef TEST_ACM_RANDOM_H_
#define TEST_ACM_RANDOM_H_

#include "third_party/googletest/src/include/gtest/gtest.h"

#include "aom/vpx_integer.h"

namespace libaom_test {

class ACMRandom {
 public:
  ACMRandom() : random_(DeterministicSeed()) {}

  explicit ACMRandom(int seed) : random_(seed) {}

  void Reset(int seed) { random_.Reseed(seed); }

  uint32_t Rand31(void) {
    return random_.Generate(testing::internal::Random::kMaxRange);
  }

  uint16_t Rand16(void) {
    const uint32_t value =
        random_.Generate(testing::internal::Random::kMaxRange);
    return (value >> 15) & 0xffff;
  }

  uint8_t Rand8(void) {
    const uint32_t value =
        random_.Generate(testing::internal::Random::kMaxRange);
    // There's a bit more entropy in the upper bits of this implementation.
    return (value >> 23) & 0xff;
  }

  uint8_t Rand8Extremes(void) {
    // Returns a random value near 0 or near 255, to better exercise
    // saturation behavior.
    const uint8_t r = Rand8();
    return r < 128 ? r << 4 : r >> 4;
  }

  int PseudoUniform(int range) { return random_.Generate(range); }

  int operator()(int n) { return PseudoUniform(n); }

  static int DeterministicSeed(void) { return 0xbaba; }

 private:
  testing::internal::Random random_;
};

}  // namespace libaom_test

#endif  // TEST_ACM_RANDOM_H_
