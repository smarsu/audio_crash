// Copyright (c) 2021 smarsufan. All Rights Reserved.

#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>

extern "C" {
#include "libavutil/samplefmt.h"
} 

/// Audio Resample
/// It's only support the change of sample fmt.
class AResample {
 public:
  AResample(int nb_channels, AVSampleFormat sample_fmt)
      : bytes_(av_get_bytes_per_sample(sample_fmt)),
        is_planar_(av_sample_fmt_is_planar(sample_fmt)) {
    nb_planar_ = is_planar_ ? nb_channels : 1;
    nb_channels_ = is_planar_ ? 1 : nb_channels;
    buffer_.resize(nb_planar_);
  }

  ~AResample() {

  }

  int convert(uint8_t **to, int to_size, uint8_t **from, int from_size) {
    // fprintf(stderr, "size: %lu %lu\n", buffer_.size(), buffer_[0].size());
    int size1 = nbsample_to_size(from_size);
    // fprintf(stderr, "size1: %d\n", size1);
    if (size1 > 0) {
      extend_buffer(size1);
      for (int idx = 0; idx < buffer_.size(); ++idx) {
        memcpy(buffer_[idx].data() + buffer_[idx].size() - size1, from[idx], size1);
      }
    }

    int size2 = nbsample_to_size(to_size);
    // fprintf(stderr, "size2: %d\n", size2);
    if (size2 > 0) {
      for (int idx = 0; idx < buffer_.size(); ++idx) {
        memcpy(to[idx], buffer_[idx].data(), size2);
      }
      shrink_buffer(size2);
    }

    // fprintf(stderr, "\n");
    return to_size;
  }

  int get_out_samples(int insamples) {
    return size_to_nbsample(buffer_[0].size() + nbsample_to_size(insamples));
  }

 private:
  void extend_buffer(int size) {
    size += buffer_[0].size();
    for (auto &buffer : buffer_) buffer.resize(size);
  }
  
  void shrink_buffer(int size) {
    int cpy_size = buffer_[0].size() - size;
    // fprintf(stderr, "cpy_size: %d\n", cpy_size);
    if (cpy_size > 0) {
      for (auto &buffer : buffer_) {
        // memmove(buffer.data(), buffer.data() + size, cpy_size);  // 
        memcpy(buffer.data(), buffer.data() + size, cpy_size);  // 
      }
    }
    for (auto &buffer : buffer_) {
      buffer.resize(std::max(cpy_size, 0));
    }
  }

  int nbsample_to_size(int nbsamples) {
    return bytes_ * nb_channels_ * nbsamples;
  }

  int size_to_nbsample(int size) {
    return size / bytes_ / nb_channels_;
  }
 
 private:
  std::vector<std::vector<uint8_t>> buffer_;
  int nb_channels_{0};
  // AVSampleFormat sample_fmt_{AV_SAMPLE_FMT_NONE};
  int bytes_{0};
  int is_planar_{-1};
  int nb_planar_{0};
};
