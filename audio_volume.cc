// Copyright (c) 2020 smarsufan. All Rights Reserved.

#include <iostream>

#include "version.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/opt.h"
}

#if __ANDROID_API__ >= 8
#include <android/log.h>
#define fprintf __android_log_print
#define stderr \
  ANDROID_LOG_WARN, "audio_volume"
#endif

void filter_frame_f(uint64_t *histogram, AVFrame *samples) {
  int nb_samples = samples->nb_samples;
  int nb_channels = samples->channels;
  int nb_planes = nb_channels;
  int plane, i;
  float *pcm;

  if (!av_sample_fmt_is_planar(static_cast<AVSampleFormat>(samples->format))) {
    nb_samples *= nb_channels;
    nb_planes = 1;
  }
  for (plane = 0; plane < nb_planes; plane++) {
    pcm = (float *)samples->extended_data[plane];
    for (i = 0; i < nb_samples; i++) {
      int his = static_cast<int>(pcm[i] * 0x8000);
      his = std::max(std::min(his, 0x8000), -0x8000);
      histogram[his + 0x8000]++;
    }
  }
}

#define MAX_DB 91

inline double logdb(uint64_t v) {
  double d = v / (double)(0x8000 * 0x8000);
  if (!v)
    return MAX_DB;
  return -log10(d) * 10;
}

double print_stats(const uint64_t *histogram) {
  int i, max_volume, shift;
  uint64_t nb_samples = 0, power = 0, nb_samples_shift = 0, sum = 0;

  for (i = 0; i < 0x10000; i++)
    nb_samples += histogram[i];
  if (!nb_samples)
    return 0;

  /* If nb_samples > 1<<34, there is a risk of overflow in the
      multiplication or the sum: shift all histogram values to avoid that.
      The total number of samples must be recomputed to avoid rounding
      errors. */
  shift = av_log2(nb_samples >> 33);
  for (i = 0; i < 0x10000; i++) {
    nb_samples_shift += histogram[i] >> shift;
    power += (i - 0x8000) * (i - 0x8000) * (histogram[i] >> shift);
  }
  if (!nb_samples_shift)
    return 0;
  power = (power + nb_samples_shift / 2) / nb_samples_shift;
  if (power > 0x8000 * 0x8000) {
    return 0;
  }
  return -logdb(power);
}

void tune(uint8_t **data, int nb_samples, int nb_channels, AVSampleFormat format, float db) {
  int nb_planes = nb_channels;
  int plane, i;
  float *pcm;

  if (!av_sample_fmt_is_planar(format)) {
    nb_samples *= nb_channels;
    nb_planes = 1;
  }
  for (plane = 0; plane < nb_planes; plane++) {
    pcm = (float *)data[plane];
    for (i = 0; i < nb_samples; i++) {
      pcm[i] = std::min(std::max(pcm[i] * pow(10.f, db / 20.f), -1.f), 1.f);
    }
  }
}

static uint8_t *buffer[64];

class AudioVolume {
 public:
  AudioVolume() = default;

  int run(const char *input, const char *output, double volume) {
    auto t1 = time();

    /// 1. Setup Input
    if (avformat_open_input(&av_format_ctx, input, nullptr, nullptr) < 0) {
      return -1000;
    }
    if (avformat_find_stream_info(av_format_ctx, nullptr) < 0) {
      return -2000;
    }
    stream_idx = av_find_best_stream(av_format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &av_codec, 0);
    if (stream_idx < 0 || !av_codec) {
      return -3000;
    }
    av_codec_ctx = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context(av_codec_ctx, av_format_ctx->streams[stream_idx]->codecpar);

    if (avcodec_open2(av_codec_ctx, av_codec, nullptr) < 0) {
      return -4000;
    }

    fprintf(stderr, "channels ... %d, channel_layout ... %llu, sample_rate ... %d, sample_fmt ... %d, frame_size .. %d\n", 
        av_codec_ctx->channels, av_codec_ctx->channel_layout, av_codec_ctx->sample_rate, av_codec_ctx->sample_fmt, av_codec_ctx->frame_size);

    /// 2. Setup Swr
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
      return -5000;
    }
    av_opt_set_int(swr_ctx, "in_channel_count", av_codec_ctx->channels, 0);
    av_opt_set_int(swr_ctx, "out_channel_count", av_codec_ctx->channels, 0);
    av_opt_set_int(swr_ctx, "in_channel_layout", av_codec_ctx->channel_layout, 0);
    av_opt_set_int(swr_ctx, "out_channel_layout", av_codec_ctx->channel_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", av_codec_ctx->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", av_codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", av_codec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", av_codec_ctx->sample_fmt, 0);
    if (swr_init(swr_ctx) < 0) {
      return -6000;
    };

    /// 3. Setup Encoder
    if (avformat_alloc_output_context2(&output_av_format_ctx, nullptr, "mp4", output) < 0) {
      return -7000;
    }
    if (output_av_format_ctx->oformat->audio_codec == AV_CODEC_ID_NONE) {
      return -8000;
    }
    output_av_codec = avcodec_find_encoder(output_av_format_ctx->oformat->audio_codec);
    if (!output_av_codec) {
      return -9000;
    }
    output_av_stream = avformat_new_stream(output_av_format_ctx, output_av_codec);
    if (!output_av_stream) {
      return -10000;
    }
    output_av_codec_ctx = avcodec_alloc_context3(output_av_codec);
    if (!output_av_codec_ctx) {
      return -11000;
    }

    output_av_codec_ctx->sample_fmt = av_codec_ctx->sample_fmt;
    output_av_codec_ctx->sample_rate = av_codec_ctx->sample_rate;
    output_av_codec_ctx->channels = av_codec_ctx->channels;
    output_av_codec_ctx->channel_layout = av_codec_ctx->channel_layout;

    if (output_av_format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
      output_av_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(output_av_codec_ctx, output_av_codec, nullptr) < 0) {
      return -11000;
    }

    if (avcodec_parameters_from_context(output_av_stream->codecpar, output_av_codec_ctx) < 0) {
      return -12000;
    }

    if (output_av_codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) {
      audio_output_frame_size = 1024;
    }
    else {
      audio_output_frame_size = output_av_codec_ctx->frame_size;
    }
    fprintf(stderr, "audio_output_frame_size ... %d\n", audio_output_frame_size);

    if (output_av_format_ctx->flags & AVFMT_NOFILE) {
      return -13000;
    }

    if (avio_open(&output_av_format_ctx->pb, output, AVIO_FLAG_WRITE) < 0) {
      return -14000;
    }
    is_avio_open = true;

    if (avformat_write_header(output_av_format_ctx, nullptr) < 0) {
      return -15000;
    }

    /// 4. Alloc
    size_t size = sizeof(uint8_t *) * av_sample_fmt_is_planar(av_codec_ctx->sample_fmt) ? av_codec_ctx->channels : 1;
    memset(buffer, 0, size);
    if (av_samples_alloc(buffer, nullptr, output_av_codec_ctx->channels, audio_output_frame_size, output_av_codec_ctx->sample_fmt, 0) < 0) {
      return -16000;
    }
    is_av_alloc = true;
    size_t buffer_size = av_samples_get_buffer_size(nullptr, output_av_codec_ctx->channels, audio_output_frame_size, output_av_codec_ctx->sample_fmt, 0);

    decode_frame = av_frame_alloc();
    encode_frame = av_frame_alloc();
    encode_frame->nb_samples = audio_output_frame_size;
    encode_frame->format = output_av_codec_ctx->sample_fmt;
    encode_frame->channel_layout = output_av_codec_ctx->channel_layout;
    encode_frame->channels = output_av_codec_ctx->channels;

    histogram = reinterpret_cast<uint64_t *>(malloc(sizeof(uint64_t) * 0x10001));
    memset(histogram, 0, sizeof(uint64_t) * 0x10001);

    /// 5. Decode
    bool drop = true;  // Drop the first pkt.
    int c1 = 0, nframes = 0, nsamples = 0;
    while (true) {
      AVPacket input_packet;
      if (av_read_frame(av_format_ctx, &input_packet) < 0) {
        break;
      }

      if (input_packet.stream_index == stream_idx) {
        if (avcodec_send_packet(av_codec_ctx, &input_packet)) {
          av_packet_unref(&input_packet);
          continue;
        }
        while (avcodec_receive_frame(av_codec_ctx, decode_frame) == 0) {
          filter_frame_f(histogram, decode_frame);

          nframes += decode_frame->nb_samples;
          if (swr_convert(swr_ctx, nullptr, 0, const_cast<const uint8_t **>(decode_frame->data), decode_frame->nb_samples) != 0) {
            return -18000;
          }
        }

        c1++;
      }

      av_packet_unref(&input_packet);
    }

    double mean_d = print_stats(histogram);
    double to_d = volume - mean_d;
    fprintf(stderr, "volume ... %f, mean_d ... %f, to_d ... %f\n", volume, mean_d, to_d);

    while (swr_get_out_samples(swr_ctx, 0) > audio_output_frame_size) {
      int samples = swr_convert(swr_ctx, buffer, audio_output_frame_size, nullptr, 0);
      nsamples += samples;

      tune(buffer, samples, output_av_codec_ctx->channels, output_av_codec_ctx->sample_fmt, to_d);

      encode_frame->nb_samples = samples;
      if (avcodec_fill_audio_frame(encode_frame, output_av_codec_ctx->channels, output_av_codec_ctx->sample_fmt, buffer[0], buffer_size, 0) < 0) {
        return -19000;
      }

      AVPacket pkt;
      av_init_packet(&pkt);

      if (avcodec_send_frame(output_av_codec_ctx, encode_frame) < 0) {
        return -20000;
      }
      while (avcodec_receive_packet(output_av_codec_ctx, &pkt) == 0) {
        pkt.stream_index = output_av_stream->index;
        av_packet_rescale_ts(&pkt, output_av_codec_ctx->time_base, output_av_stream->time_base); 

        if (!drop) {
          if (av_interleaved_write_frame(output_av_format_ctx, &pkt) < 0) {
            return -21000;
          }
        }
        else {
          fprintf(stderr, "pkt size ... %d\n", pkt.size);
          drop = false;
        }

        av_packet_unref(&pkt);
      }
    }

    int samples = swr_convert(swr_ctx, buffer, audio_output_frame_size, nullptr, 0);
    nsamples += samples;
    
    if (samples > 0) {
      tune(buffer, samples, output_av_codec_ctx->channels, output_av_codec_ctx->sample_fmt, to_d);

      encode_frame->nb_samples = samples;
      if (avcodec_fill_audio_frame(encode_frame, output_av_codec_ctx->channels, output_av_codec_ctx->sample_fmt, buffer[0], buffer_size, 0) < 0) {
        return -22000;
      }

      if (avcodec_send_frame(output_av_codec_ctx, encode_frame) < 0) {
        return -23000;
      }
    }

    AVPacket pkt;
    av_init_packet(&pkt);

    if (avcodec_send_frame(output_av_codec_ctx, nullptr) < 0) {
      return -24000;
    }
    while (avcodec_receive_packet(output_av_codec_ctx, &pkt) == 0) {
      pkt.stream_index = output_av_stream->index;
      av_packet_rescale_ts(&pkt, output_av_codec_ctx->time_base, output_av_stream->time_base);
      
      if (!drop) {
        if (av_interleaved_write_frame(output_av_format_ctx, &pkt) < 0) {
          return -25000;
        }
      }
      else {
        fprintf(stderr, "pkt size ... %d\n", pkt.size);
        drop = false;
      }

      av_packet_unref(&pkt);
    }

    fprintf(stderr, "c1 ... %d, nframes ... %d, nsamples ... %d\n", c1, nframes, nsamples);

    if (av_write_trailer(output_av_format_ctx) < 0) {
      return -26000;
    }

    auto t2 = time();
    fprintf(stderr, "AudioVolume Time ... %f ms\n", t2 - t1);

    return 0;
  }

  ~AudioVolume() {
    if (encode_frame) av_frame_free(&encode_frame);  // 142
    if (decode_frame) av_frame_free(&decode_frame);  // 141
    if (is_av_alloc) av_freep(&buffer[0]);  // 130
    if (is_avio_open) avio_close(output_av_format_ctx->pb);  // 119
    if (output_av_codec_ctx) avcodec_free_context(&output_av_codec_ctx);  // 85
    if (output_av_format_ctx) avformat_free_context(output_av_format_ctx);  // 71
    if (swr_ctx) swr_free(&swr_ctx);  // 54
    if (av_codec_ctx) avcodec_free_context(&av_codec_ctx);  // 40
    if (av_format_ctx) avformat_close_input(&av_format_ctx);  // 30

    if (histogram) free(histogram);
  }

 private:
  AVFormatContext *av_format_ctx{nullptr};
  AVCodec *av_codec{nullptr};
  int stream_idx{-1};
  AVCodecContext *av_codec_ctx{nullptr};
  AVFrame *decode_frame{nullptr};
  AVFrame *encode_frame{nullptr};
  SwrContext *swr_ctx{nullptr};
  AVFormatContext *output_av_format_ctx{nullptr};
  AVCodec *output_av_codec{nullptr};
  AVStream *output_av_stream{nullptr};
  AVCodecContext *output_av_codec_ctx{nullptr};
  int audio_output_frame_size{0};
  bool is_avio_open{false};
  bool is_av_alloc{false};
  uint64_t *histogram{nullptr};
};

extern "C" __attribute__((visibility("default"))) __attribute__((used))
int audio_volume(const char *input, const char *output, double volume) {
  // volume: [-0, -91]
  // ffmpeg -i <input> -filter_complex volumedetect -c:v copy -f null /dev/null 
  fprintf(stderr, "%s AudioVolume ... %s, %s, %f\n", VERSION, input, output, volume);
  AudioVolume read;
  int ok = read.run(input, output, volume);
  return ok;
}

#ifdef WITH_MAIN
int main(int argv, char *args[]) {
  if (argv != 5) {
    fprintf(stderr, "Usage: %s <input> <output> <volume> <mod>\n", args[0]);
    return -1;
  }

  const char *input = args[1];
  const char *output = args[2];
  double volume = std::atof(args[3]);
  int mod = std::atoi(args[4]);

  int ok = audio_volume(input, output, volume);
  fprintf(stderr, "ok ... %d\n", ok);
  while (mod) {
    ok = audio_volume(input, output, volume);
    fprintf(stderr, "ok ... %d\n", ok);
  }
}
#endif  // WITH_MAIN
