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
  ANDROID_LOG_WARN, "audio_crash"
#endif

int cut(uint8_t **dst, uint8_t **src, int src_start, int src_end, int start_nframes, int end_nframes, AVSampleFormat sample_fmt, int nb_channels) {
  int nb_samples = std::min(end_nframes, src_end) - std::max(start_nframes, src_start);
  if (nb_samples <= 0) {
    return 0;
  }
  int nb_offset = std::max(start_nframes - src_start, 0);
  int nb_planes = nb_channels;
  int plane;
  
  if (!av_sample_fmt_is_planar(static_cast<AVSampleFormat>(sample_fmt))) {
    nb_samples *= nb_channels;
    nb_offset *= nb_channels;
    nb_planes = 1;
  }
  for (plane = 0; plane < nb_planes; ++plane) {
    dst[plane] = src[plane] + nb_offset * av_get_bytes_per_sample(sample_fmt);
  }
  return nb_samples;
}

static uint8_t *buffer[64];

class AudioCut {
 public:
  AudioCut() = default;

  int run(const char *input, const char *output, double start_ms, double end_ms) {
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

    int start_nframes = round(av_codec_ctx->sample_rate * start_ms / 1000);
    int end_nframes = round(av_codec_ctx->sample_rate * end_ms / 1000);

    fprintf(stderr, "start_nframes ... %d, end_nframes ... %d\n", start_nframes, end_nframes);

    AVSampleFormat output_av_sample_format = AV_SAMPLE_FMT_FLTP;
    uint64_t output_channel_layout = av_get_default_channel_layout(av_codec_ctx->channels);

    /// 2. Setup Swr
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
      return -5000;
    }
    av_opt_set_int(swr_ctx, "in_channel_count", av_codec_ctx->channels, 0);
    av_opt_set_int(swr_ctx, "out_channel_count", av_codec_ctx->channels, 0);
    av_opt_set_int(swr_ctx, "in_channel_layout", av_codec_ctx->channel_layout, 0);
    av_opt_set_int(swr_ctx, "out_channel_layout", output_channel_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", av_codec_ctx->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", av_codec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", av_codec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", output_av_sample_format, 0);  // FLTP
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

    output_av_codec_ctx->sample_fmt = output_av_sample_format;
    output_av_codec_ctx->sample_rate = av_codec_ctx->sample_rate;
    output_av_codec_ctx->channels = av_codec_ctx->channels;
    output_av_codec_ctx->channel_layout = output_channel_layout;

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

    handle = reinterpret_cast<uint8_t **>(malloc(sizeof(uint8_t *) * 64));

    /// 5. Decode
    bool drop = true;  // Drop the first pkt.
    int c1 = 0, nframes = 0, nsamples = 0, last_nframes = 0;
    while (true) {
      AVPacket input_packet;
      if (av_read_frame(av_format_ctx, &input_packet) < 0) {
        break;
      }

      if (input_packet.stream_index == stream_idx) {
        if (avcodec_send_packet(av_codec_ctx, &input_packet)) {
          // May send Bad packet.       
          av_packet_unref(&input_packet);
          continue;
        }
        while (avcodec_receive_frame(av_codec_ctx, decode_frame) == 0) {
          nframes += decode_frame->nb_samples;
          int nb_samples = cut(handle, decode_frame->data, last_nframes, nframes, start_nframes, end_nframes, av_codec_ctx->sample_fmt, output_av_codec_ctx->channels);
          last_nframes = nframes;
          if (swr_convert(swr_ctx, nullptr, 0, const_cast<const uint8_t **>(handle), nb_samples) != 0) {
            return -18000;
          }
        }

        while (swr_get_out_samples(swr_ctx, 0) > audio_output_frame_size) {
          int samples = swr_convert(swr_ctx, buffer, audio_output_frame_size, nullptr, 0);
          nsamples += samples;

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

        c1++;
      }

      av_packet_unref(&input_packet);
    }

    int samples = swr_convert(swr_ctx, buffer, audio_output_frame_size, nullptr, 0);
    nsamples += samples;
    
    if (samples > 0) {
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
    fprintf(stderr, "AudioCut Time ... %f ms\n", t2 - t1);

    return 0;
  }

  ~AudioCut() {
    if (encode_frame) av_frame_free(&encode_frame);  // 142
    if (decode_frame) av_frame_free(&decode_frame);  // 141
    if (is_av_alloc) av_freep(&buffer[0]);  // 130
    if (is_avio_open) avio_close(output_av_format_ctx->pb);  // 119
    if (output_av_codec_ctx) avcodec_free_context(&output_av_codec_ctx);  // 85
    if (output_av_format_ctx) avformat_free_context(output_av_format_ctx);  // 71
    if (swr_ctx) swr_free(&swr_ctx);  // 54
    if (av_codec_ctx) avcodec_free_context(&av_codec_ctx);  // 40
    if (av_format_ctx) avformat_close_input(&av_format_ctx);  // 30

    if (handle) free(handle);
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

  uint8_t **handle{nullptr};
};

extern "C" __attribute__((visibility("default"))) __attribute__((used))
int audio_cut(const char *input, const char *output, double start_ms, double end_ms) {
  fprintf(stderr, "%s AudioCut ... %s, %s, %f, %f\n", VERSION, input, output, start_ms, end_ms);
  AudioCut read;
  int ok = read.run(input, output, start_ms, end_ms);
  fprintf(stderr, "AudioCut ok ... %d\n", ok);
  return ok;
}

#ifdef WITH_MAIN
int main(int argv, char *args[]) {
  if (argv != 6) {
    fprintf(stderr, "Usage: %s <input> <output> <start_ms> <end_ms> <mod>\n", args[0]);
    return -1;
  }

  const char *input = args[1];
  const char *output = args[2];
  double start_ms = std::atof(args[3]);
  double end_ms = std::atof(args[4]);
  int mod = std::atoi(args[5]);

  int ok = audio_cut(input, output, start_ms, end_ms);
  fprintf(stderr, "ok ... %d\n", ok);
  while (mod) {
    ok = audio_cut(input, output, start_ms, end_ms);
    fprintf(stderr, "ok ... %d\n", ok);
  }
}
#endif  // WITH_MAIN
