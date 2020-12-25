// Copyright (c) 2020 smarsufan. All Rights Reserved.

#include <iostream>

#include "version.h"

extern "C" {
#include "libavformat/avformat.h"
}

#if __ANDROID_API__ >= 8
#include <android/log.h>
#define fprintf __android_log_print
#define stderr \
  ANDROID_LOG_WARN, "audio_crash"
#endif

static uint8_t *buffer[64];

class MediaDuration {
 public:
  MediaDuration() = default;

  int64_t run(const char *input) {
    auto t1 = time();

    /// 1. Setup Input
    if (avformat_open_input(&av_format_ctx, input, nullptr, nullptr) < 0) {
      return -1000;
    }

    auto t2 = time();
    fprintf(stderr, "MediaDuration Time ... %f ms\n", t2 - t1);

    return av_format_ctx->duration;
  }

  ~MediaDuration() {
    if (av_format_ctx) avformat_close_input(&av_format_ctx);  // 30
  }

 private:
  AVFormatContext *av_format_ctx{nullptr};
};

extern "C" __attribute__((visibility("default"))) __attribute__((used))
int64_t media_duration(const char *input) {
  fprintf(stderr, "%s MediaDuration ... %s\n", VERSION, input);
  MediaDuration read;
  int64_t ok = read.run(input);
  fprintf(stderr, "MediaDuration ok ... %d\n", ok);
  return ok;
}

#ifdef WITH_MAIN
int main(int argv, char *args[]) {
  if (argv != 3) {
    fprintf(stderr, "Usage: %s <input> <mod>\n", args[0]);
    return -1;
  }

  const char *input = args[1];
  int mod = std::atoi(args[2]);

  int ok = media_duration(input);
  fprintf(stderr, "ok ... %d\n", ok);
  while (mod) {
    ok = media_duration(input);
    fprintf(stderr, "ok ... %d\n", ok);
  }
}
#endif  // WITH_MAIN
