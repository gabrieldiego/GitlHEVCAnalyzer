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

#ifndef VP10_ENCODER_ETHREAD_H_
#define VP10_ENCODER_ETHREAD_H_

#ifdef __cplusplus
extern "C" {
#endif

struct VP10_COMP;
struct ThreadData;

typedef struct EncWorkerData {
  struct VP10_COMP *cpi;
  struct ThreadData *td;
  int start;
} EncWorkerData;

void vp10_encode_tiles_mt(struct VP10_COMP *cpi);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VP10_ENCODER_ETHREAD_H_
