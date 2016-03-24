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
#ifndef TEST_WEBM_VIDEO_SOURCE_H_
#define TEST_WEBM_VIDEO_SOURCE_H_
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include "../tools_common.h"
#include "../webmdec.h"
#include "test/video_source.h"

namespace libaom_test {

// This class extends VideoSource to allow parsing of WebM files,
// so that we can do actual file decodes.
class WebMVideoSource : public CompressedVideoSource {
 public:
  explicit WebMVideoSource(const std::string &file_name)
      : file_name_(file_name), vpx_ctx_(new VpxInputContext()),
        webm_ctx_(new WebmInputContext()), buf_(NULL), buf_sz_(0), frame_(0),
        end_of_file_(false) {}

  virtual ~WebMVideoSource() {
    if (vpx_ctx_->file != NULL) fclose(vpx_ctx_->file);
    webm_free(webm_ctx_);
    delete vpx_ctx_;
    delete webm_ctx_;
  }

  virtual void Init() {}

  virtual void Begin() {
    vpx_ctx_->file = OpenTestDataFile(file_name_);
    ASSERT_TRUE(vpx_ctx_->file != NULL) << "Input file open failed. Filename: "
                                        << file_name_;

    ASSERT_EQ(file_is_webm(webm_ctx_, vpx_ctx_), 1) << "file is not WebM";

    FillFrame();
  }

  virtual void Next() {
    ++frame_;
    FillFrame();
  }

  void FillFrame() {
    ASSERT_TRUE(vpx_ctx_->file != NULL);
    const int status = webm_read_frame(webm_ctx_, &buf_, &buf_sz_, &buf_sz_);
    ASSERT_GE(status, 0) << "webm_read_frame failed";
    if (status == 1) {
      end_of_file_ = true;
    }
  }

  void SeekToNextKeyFrame() {
    ASSERT_TRUE(vpx_ctx_->file != NULL);
    do {
      const int status = webm_read_frame(webm_ctx_, &buf_, &buf_sz_, &buf_sz_);
      ASSERT_GE(status, 0) << "webm_read_frame failed";
      ++frame_;
      if (status == 1) {
        end_of_file_ = true;
      }
    } while (!webm_ctx_->is_key_frame && !end_of_file_);
  }

  virtual const uint8_t *cxdata() const { return end_of_file_ ? NULL : buf_; }
  virtual size_t frame_size() const { return buf_sz_; }
  virtual unsigned int frame_number() const { return frame_; }

 protected:
  std::string file_name_;
  VpxInputContext *vpx_ctx_;
  WebmInputContext *webm_ctx_;
  uint8_t *buf_;
  size_t buf_sz_;
  unsigned int frame_;
  bool end_of_file_;
};

}  // namespace libaom_test

#endif  // TEST_WEBM_VIDEO_SOURCE_H_
