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

#ifndef VIDEO_READER_H_
#define VIDEO_READER_H_

#include "./video_common.h"

// The following code is work in progress. It is going to  support transparent
// reading of input files. Right now only IVF format is supported for
// simplicity. The main goal the API is to be simple and easy to use in example
// code and in vpxenc/vpxdec later. All low-level details like memory
// buffer management are hidden from API users.
struct VpxVideoReaderStruct;
typedef struct VpxVideoReaderStruct VpxVideoReader;

#ifdef __cplusplus
extern "C" {
#endif

// Opens the input file for reading and inspects it to determine file type.
// Returns an opaque VpxVideoReader* upon success, or NULL upon failure.
// Right now only IVF format is supported.
VpxVideoReader *vpx_video_reader_open(const char *filename);

// Frees all resources associated with VpxVideoReader* returned from
// vpx_video_reader_open() call.
void vpx_video_reader_close(VpxVideoReader *reader);

// Reads frame from the file and stores it in internal buffer.
int vpx_video_reader_read_frame(VpxVideoReader *reader);

// Returns the pointer to memory buffer with frame data read by last call to
// vpx_video_reader_read_frame().
const uint8_t *vpx_video_reader_get_frame(VpxVideoReader *reader, size_t *size);

// Fills VpxVideoInfo with information from opened video file.
const VpxVideoInfo *vpx_video_reader_get_info(VpxVideoReader *reader);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // VIDEO_READER_H_
