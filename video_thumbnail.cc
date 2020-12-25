// Copyright (c) 2020 smarsufan. All Rights Reserved.

// ./video_thumbnail IMG_1892.MOV image/0.jpg image/1.jpg image/2.jpg image/3.jpg image/4.jpg image/5.jpg image/6.jpg image/7.jpg 0 0 1000 -10 60000 0 700000 6000000 600000
// ./video_thumbnail 16Min.MP4 image/0.jpg image/1.jpg image/2.jpg image/3.jpg image/4.jpg image/5.jpg image/6.jpg image/7.jpg 0 0 300 600 900 1200 1500 1800 2100

#include <iostream>
#include <cstring>
#include <sys/time.h>
#include <vector>
#include <fstream>
#include <thread>

extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
// #include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include <libavutil/fifo.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

}  // extern "C"

#if __ANDROID_API__ >= 8
#include <android/log.h>
#define fprintf __android_log_print
#define stderr \
  ANDROID_LOG_WARN, "audio_convert"
#endif

#define av_free_packet av_packet_unref

inline double timems() {
  struct timeval tv;    
  gettimeofday(&tv,nullptr);
  return tv.tv_sec * 1000. + tv.tv_usec / 1000.; 
}

#ifdef __APPLE__
#include "TargetConditionals.h"
#ifdef TARGET_OS_IPHONE
// iOS
#elif TARGET_OS_MAC
// Mac OS
#else
// Unsupported platform
#endif
#include <mach/machine.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#else  // Linux or Android
#include <omp.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif


inline int get_cpu_num() {
#ifdef __APPLE__
  int count = 0;
  size_t len = sizeof(count);
  sysctlbyname("hw.ncpu", &count, &len, NULL, 0);
  if (count < 1) {
    count = 1;
  }
  return count;
#else  // Linux or Android
  // get cpu num from /sys/devices/system/cpu/cpunum/uevent
  int max_cpu_num = 20;
  int count = 0;
  for (int i = 0; i < max_cpu_num; i++) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/uevent", i);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
      break;
    }
    count++;
    fclose(fp);
  }
  if (count < 1) {
    count = 1;
  }
  return count;
#endif
}

template <typename T>
inline int minof(T *data, int size) {
  T d = data[0];
  for (int idx = 1; idx < size; ++idx) {
    if (data[idx] < d) {
      d = data[idx];
    }
  }
  return d;
}

class VideoThubmnail {
 public:
  VideoThubmnail() = default;

  int find_rotate(AVStream *av_stream) {
    auto entry = av_dict_get(av_stream->metadata, "rotate", nullptr, 0);
    if (!entry) {
      return 0;
    }
    else {
      int rotate = atoi(entry->value);
      return rotate % 360;
    }
  }

  int copy(const char *dst, const char *src) {
    std::ifstream sfb(src);
    sfb.seekg(0, sfb.end);
    size_t size = sfb.tellg();
    sfb.seekg(0, sfb.beg);

    std::vector<char> c(size);
    sfb.read(c.data(), size);

    std::ofstream dfb(dst);
    dfb.write(c.data(), size);

    sfb.close();
    dfb.close();
    return 0;
  }

  int write(const char *path, AVFrame *frame) {
    int width = frame->width;
    int height = frame->height;

    AVFormatContext *output_av_format_ctx = nullptr;
    avformat_alloc_output_context2(&output_av_format_ctx, nullptr, "singlejpeg", path);

    if (avio_open(&output_av_format_ctx->pb, path, AVIO_FLAG_READ_WRITE) < 0) {
      return -10100;
    }

    AVStream *output_av_stream = avformat_new_stream(output_av_format_ctx, nullptr);
    if (output_av_stream == nullptr) {
      return -10200;
    }

    AVCodecContext *output_av_codec_ctx = output_av_stream->codec;
    output_av_codec_ctx->codec_id = output_av_format_ctx->oformat->video_codec;
    output_av_codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    output_av_codec_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    output_av_codec_ctx->height = height;
    output_av_codec_ctx->width = width;
    output_av_codec_ctx->time_base.num = 1;
    output_av_codec_ctx->time_base.den = 25;

    AVCodec *output_av_codec = avcodec_find_encoder(output_av_codec_ctx->codec_id);
    if (!output_av_codec) {
      return -10300;
    }

    if (avcodec_open2(output_av_codec_ctx, output_av_codec, nullptr) < 0) {
      return -10400;
    }
    avcodec_parameters_from_context(output_av_stream->codecpar, output_av_codec_ctx);

    if (avformat_write_header(output_av_format_ctx, nullptr) < 0) {
      return -10410;
    }
    int size = output_av_codec_ctx->width * output_av_codec_ctx->height;

    AVPacket packet;
    av_new_packet(&packet, size * 3);

    int got_picture = 0;
    int result = avcodec_encode_video2(output_av_codec_ctx, &packet, frame, &got_picture);
    if (result < 0 || !got_picture) {
      return -10500;
    }

    av_write_frame(output_av_format_ctx, &packet);
    av_write_trailer(output_av_format_ctx);

    av_free_packet(&packet);
    avio_close(output_av_format_ctx->pb);
    avformat_free_context(output_av_format_ctx);
    return 0;
  }

  bool make_dump_flt(int rotate, int width, int height) {
    if (rotate == 0 && width <= 0) {
      return false;
    }

    std::string desc = "[in] ";
    switch (rotate) {      
      case 90:
        desc += "transpose=1";
        break;
      
      case 180:
        desc += "rotate=3.14159265358979323846264338327950288";
        break;

      case 270:
        desc += "transpose=2";
        break;

      default:
        desc += "rotate=0";
        break;
    }
    if (width > 0) {
      desc += " [inter]; [inter] ";
      desc += " scale=" + std::to_string(width) + ":" + std::to_string(width) + "/a";
    }
    desc += " [out]";

    make_flt(desc.c_str(), &resize_graph, &resize_buffersrc_ctx, &resize_buffersink_ctx);
    return true;
  }

  bool make_resize_flt(int width, int height) {
    if (width <= 0) {
      return false;
    }
    else {
      std::string desc = "scale=" + std::to_string(width) + ":" + std::to_string(width) + "/a";
      make_flt(desc.c_str(), &resize_graph, &resize_buffersrc_ctx, &resize_buffersink_ctx);
      return true;
    }
  }

  bool make_rotate_flt(int rotate) {
    if (rotate == 0) {
      return false;
    }
    else {
      const char *desc = nullptr;
      switch (rotate) {
        case 0:
          desc = "rotate=0";
          break;
        
        case 90:
          desc = "transpose=1";
          break;
        
        case 180:
          desc = "rotate=3.14159265358979323846264338327950288";
          break;

        case 270:
          desc = "transpose=2";
          break;

        default:
          desc = "rotate=0";
          break;
      }
      make_flt(desc, &rotate_graph, &rotate_buffersrc_ctx, &rotate_buffersink_ctx);
      return true;
    }
  }

  int make_flt(const char *desc, AVFilterGraph **graph, AVFilterContext **buffersrc_ctx, AVFilterContext **buffersink_ctx) {
    fprintf(stderr, "desc ... %s\n", desc);

    char args[512];
    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    AVBufferSinkParams *buffersink_params;

    snprintf(args, sizeof(args),
      "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
      av_codec_ctx->width, av_codec_ctx->height, av_codec_ctx->pix_fmt,
      av_codec_ctx->time_base.num, av_codec_ctx->time_base.den,
      av_codec_ctx->sample_aspect_ratio.num, av_codec_ctx->sample_aspect_ratio.den);

    *graph = avfilter_graph_alloc();

    if (avfilter_graph_create_filter(buffersrc_ctx, buffersrc, "in", args, nullptr, *graph) < 0) {
      return -20100;
    }

    buffersink_params = av_buffersink_params_alloc();
    buffersink_params->pixel_fmts = pix_fmts;
    if (avfilter_graph_create_filter(buffersink_ctx, buffersink, "out", nullptr, buffersink_params, *graph) < 0) {
      return -20200;
    }
    av_free(buffersink_params);

    outputs->name = av_strdup("in");
    outputs->filter_ctx = *buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = *buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    if (avfilter_graph_parse_ptr(*graph, desc, &inputs, &outputs, nullptr) < 0) {
      return -20300;
    }
    if (avfilter_graph_config(*graph, nullptr) < 0) {
        return -20400;
    }
    return 0;
  }

  int run(const char *input_path, const char **output_paths, double *tms, int tms_len, int width, int height, double threshold) {
    auto t1 = timems();

    if (avformat_open_input(&av_format_ctx, input_path, nullptr, nullptr) < 0) {
      return -100;
    }
    if (avformat_find_stream_info(av_format_ctx, nullptr) < 0) {
      return -200;
    }
    stream_idx = av_find_best_stream(av_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &av_codec, 0);
    if (stream_idx < 0 || !av_codec) {
      return -300;
    }
    av_codec_ctx = av_format_ctx->streams[stream_idx]->codec;
    if (avcodec_open2(av_codec_ctx, av_codec, nullptr) < 0) {
      return -400;
    }

    auto t_rotate_s = timems();
    int rotate = find_rotate(av_format_ctx->streams[stream_idx]);
    // bool need_rotate = make_rotate_flt(rotate);
    bool need_rotate = false;

    // bool need_resize = make_resize_flt(width, height);
    bool need_resize = make_dump_flt(rotate, width, height);
    auto t_rotate_e = timems();
    fprintf(stderr, "resize rotate time ... %f, need_rotate ... %d, need_resize ... %d\n", t_rotate_e - t_rotate_s, need_rotate, need_resize);

    decode_frame = av_frame_alloc();
    rotate_frame = av_frame_alloc();
    resize_frame = av_frame_alloc();
    ref_frame = av_frame_alloc();
    if (!decode_frame || !rotate_frame || !resize_frame || !ref_frame) {
      return -500;
    }

    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = nullptr;
    avpkt.size = 0;

    /// 1. Just Key Frame
    // // Got Same Picture If Is The same Seek Frame
    // auto t_find_s = timems();
    // std::vector<uint64_t> pts_list(tms_len);
    // for (int idx = 0; idx < tms_len; ++idx) {
    //   if (av_seek_frame(av_format_ctx, stream_idx, av_format_ctx->streams[stream_idx]->duration * tms[idx] * 1000 / av_format_ctx->duration, AVSEEK_FLAG_BACKWARD) < 0) {
    //     return -510;
    //   }
    //   while (av_read_frame(av_format_ctx, &avpkt) >= 0) {
    //     if (avpkt.stream_index == stream_idx) {
    //       break;
    //     }
    //     av_packet_unref(&avpkt);
    //   }
    //   pts_list[idx] = avpkt.pts;

    //   av_packet_unref(&avpkt);
    // }
    // auto t_find_e = timems();
    // fprintf(stderr, "find time ... %f\n", t_find_e - t_find_s);

    // std::vector<bool> need_write(tms_len, true);

    // // Seek Take                0.105ms ... 0.028ms
    // // Seek & Read Take         46ms    ... 681ms
    // // Seek & Read & Write Take 155ms   ... 1753.340088ms
    // auto t_seek_s = timems();
    // for (int idx = 0; idx < tms_len; ++idx) {
    //   if (!need_write[idx]) {
    //     continue;
    //   }

    //   if (av_seek_frame(av_format_ctx, stream_idx, av_format_ctx->streams[stream_idx]->duration * tms[idx] * 1000 / av_format_ctx->duration, AVSEEK_FLAG_BACKWARD) < 0) {
    //     return -510;
    //   }

    //   while (av_read_frame(av_format_ctx, &avpkt) >= 0) {
    //     if (avpkt.stream_index == stream_idx) {
    //       if (avcodec_send_packet(av_codec_ctx, &avpkt) < 0) {
    //         return -600;
    //       }
    //       while (avcodec_receive_frame(av_codec_ctx, decode_frame) == 0) {
    //         encode_frame = decode_frame;
    //         if (need_rotate) {
    //           if (av_buffersrc_add_frame(rotate_buffersrc_ctx, encode_frame) < 0) {
    //             return -520;
    //           }
    //           if (av_buffersink_get_frame(rotate_buffersink_ctx, rotate_frame) < 0) {
    //             return -530;
    //           }
    //           encode_frame = rotate_frame;
    //         }
    //         if (need_resize) {
    //           if (av_buffersrc_add_frame(resize_buffersrc_ctx, encode_frame) < 0) {
    //             return -540;
    //           }
    //           if (av_buffersink_get_frame(resize_buffersink_ctx, resize_frame) < 0) {
    //             return -550;
    //           }
    //           encode_frame = resize_frame;
    //         }
    //         write(output_paths[idx], encode_frame);
    //         fprintf(stderr, "Suc Write ... %f\n", tms[idx]);
    //         need_write[idx] = false;
    //         for (int ii = 0; ii < tms_len; ++ii) {
    //           if (need_write[ii] && pts_list[idx] == pts_list[ii]) {
    //             copy(output_paths[ii], output_paths[idx]);
    //             fprintf(stderr, "Suc Copy ... %f\n", tms[ii]);
    //             need_write[ii] = false;
    //           }
    //         }
    //         av_packet_unref(&avpkt);
    //         goto end;
    //       }
    //       av_packet_unref(&avpkt);
    //     }
    //   }

    //   end:
    //     avcodec_flush_buffers(av_codec_ctx);
    //     continue;
    // }
    // auto t_seek_e = timems();
    // fprintf(stderr, "seek time ... %f\n", t_seek_e - t_seek_s);


    // /// 2. Perfect Frame
    // auto t_seek_s = timems();
    // for (int idx = 0; idx < tms_len; ++idx) {
    //   if (av_seek_frame(av_format_ctx, stream_idx, av_format_ctx->streams[stream_idx]->duration * tms[idx] * 1000 / av_format_ctx->duration, AVSEEK_FLAG_BACKWARD) < 0) {
    //     return -510;
    //   }

    //   while (av_read_frame(av_format_ctx, &avpkt) >= 0) {
    //     if (avpkt.stream_index == stream_idx) {
    //       fprintf(stderr, "%lld ... %f ... %f\n", avpkt.pts, (double) avpkt.pts / av_format_ctx->streams[stream_idx]->duration, tms[idx] * 1000 / av_format_ctx->duration);
    //       if (avcodec_send_packet(av_codec_ctx, &avpkt) < 0) {
    //         return -600;
    //       }
    //       while (avcodec_receive_frame(av_codec_ctx, decode_frame) == 0) {
    //         av_frame_unref(ref_frame);
    //         av_frame_ref(ref_frame, decode_frame);
    //         if ((double) avpkt.pts / av_format_ctx->streams[stream_idx]->duration >= tms[idx] * 1000 / av_format_ctx->duration) {
    //           av_packet_unref(&avpkt);
    //           goto end;
    //         }
    //       }
    //       av_packet_unref(&avpkt);
    //     }
    //   }

    //   end:
    //     encode_frame = ref_frame;
    //     if (need_rotate) {
    //       if (av_buffersrc_add_frame(rotate_buffersrc_ctx, encode_frame) < 0) {
    //         return -520;
    //       }
    //       if (av_buffersink_get_frame(rotate_buffersink_ctx, rotate_frame) < 0) {
    //         return -530;
    //       }
    //       encode_frame = rotate_frame;
    //     }
    //     if (need_resize) {
    //       if (av_buffersrc_add_frame(resize_buffersrc_ctx, encode_frame) < 0) {
    //         return -540;
    //       }
    //       if (av_buffersink_get_frame(resize_buffersink_ctx, resize_frame) < 0) {
    //         return -550;
    //       }
    //       encode_frame = resize_frame;
    //     }
    //     write(output_paths[idx], encode_frame);
    //     fprintf(stderr, "Suc Write ... %f\n", tms[idx]);

    //     avcodec_flush_buffers(av_codec_ctx);
    //     continue;
    // }
    // av_frame_unref(ref_frame);
    // auto t_seek_e = timems();
    // fprintf(stderr, "seek time ... %f\n", t_seek_e - t_seek_s);

    /// 3. Decode A Stream
    // Got Same Picture If Is The same Seek Frame
    auto t_find_s = timems();
    double minoftms = minof(tms, tms_len);
    auto t_find_e = timems();
    fprintf(stderr, "find time ... %f\n", t_find_e - t_find_s);

    std::vector<bool> need_write(tms_len, true);
    int have_write = 0;

    auto t_seek_s = timems();
    if (av_seek_frame(av_format_ctx, stream_idx, av_format_ctx->streams[stream_idx]->duration * minoftms * 1000 / av_format_ctx->duration, AVSEEK_FLAG_BACKWARD) < 0) {
      return -510;
    }

    while (have_write < tms_len && av_read_frame(av_format_ctx, &avpkt) >= 0) {
      if (avpkt.stream_index == stream_idx) {
        fprintf(stderr, "%lld ... %f ... %f\n", avpkt.pts, (double) avpkt.pts / av_format_ctx->streams[stream_idx]->duration, minoftms * 1000 / av_format_ctx->duration);
        if (avcodec_send_packet(av_codec_ctx, &avpkt) < 0) {
          return -600;
        }
        while (avcodec_receive_frame(av_codec_ctx, decode_frame) == 0) {
          av_frame_unref(ref_frame);
          av_frame_ref(ref_frame, decode_frame);
          bool process = false;
          for (int idx = 0; idx < tms_len; ++idx) {
            if (need_write[idx] && (double) avpkt.pts / av_format_ctx->streams[stream_idx]->duration >= tms[idx] * 1000 / av_format_ctx->duration) {
              if (!process) {
                encode_frame = ref_frame;
                if (need_rotate) {
                  if (av_buffersrc_add_frame(rotate_buffersrc_ctx, encode_frame) < 0) {
                    return -520;
                  }
                  if (av_buffersink_get_frame(rotate_buffersink_ctx, rotate_frame) < 0) {
                    return -530;
                  }
                  encode_frame = rotate_frame;
                }
                if (need_resize) {
                  if (av_buffersrc_add_frame(resize_buffersrc_ctx, encode_frame) < 0) {
                    return -540;
                  }
                  if (av_buffersink_get_frame(resize_buffersink_ctx, resize_frame) < 0) {
                    return -550;
                  }
                  encode_frame = resize_frame;
                }

                process = true;
              }
              
              write(output_paths[idx], encode_frame);
              fprintf(stderr, "Suc Write ... %f\n", tms[idx]);

              av_frame_unref(rotate_frame);
              av_frame_unref(resize_frame);

              need_write[idx] = false;
              ++have_write;
            }
          }
        }
      }
      av_packet_unref(&avpkt);
    }

    if (have_write < tms_len) {
      encode_frame = ref_frame;
      if (need_rotate) {
        if (av_buffersrc_add_frame(rotate_buffersrc_ctx, encode_frame) < 0) {
          return -520;
        }
        if (av_buffersink_get_frame(rotate_buffersink_ctx, rotate_frame) < 0) {
          return -530;
        }
        encode_frame = rotate_frame;
      }
      if (need_resize) {
        if (av_buffersrc_add_frame(resize_buffersrc_ctx, encode_frame) < 0) {
          return -540;
        }
        if (av_buffersink_get_frame(resize_buffersink_ctx, resize_frame) < 0) {
          return -550;
        }
        encode_frame = resize_frame;
      }

      for (int idx = 0; idx < tms_len; ++idx) {
        if (need_write[idx]) {
          write(output_paths[idx], encode_frame);
          fprintf(stderr, "Try Write ... %f\n", tms[idx]);

          need_write[idx] = false;
          ++have_write;
        }
      }

      av_frame_unref(rotate_frame);
      av_frame_unref(resize_frame);
    }

    av_frame_unref(ref_frame);
    auto t_seek_e = timems();
    fprintf(stderr, "seek time ... %f\n", t_seek_e - t_seek_s);

    // // Read Take 357ms ... 17ms
    // auto t_read_s = timems();
    // while (av_read_frame(av_format_ctx, &avpkt) >= 0) {
    //   av_packet_unref(&avpkt);
    // }
    // auto t_read_e = timems();
    // fprintf(stderr, "read time ... %f\n", t_read_e - t_read_s);

    auto t2 = timems();
    fprintf(stderr, "video thumb time ... %fms\n", t2 - t1);

    return 0;
  }

  ~VideoThubmnail() {
    avformat_close_input(&av_format_ctx);
    av_frame_free(&decode_frame);
    av_frame_free(&ref_frame);
    // av_frame_free(&encode_frame);
    av_frame_free(&rotate_frame);
    av_frame_free(&resize_frame);
    if (rotate_graph) avfilter_graph_free(&rotate_graph);
    if (resize_graph) avfilter_graph_free(&resize_graph);
  }

 private:
  AVFormatContext *av_format_ctx{nullptr};
  AVCodec *av_codec{nullptr};
  int stream_idx{-1};
  AVCodecContext *av_codec_ctx{nullptr};
  AVFilterContext *rotate_buffersink_ctx{nullptr};
  AVFilterContext *rotate_buffersrc_ctx{nullptr};
  AVFilterGraph *rotate_graph{nullptr};
  AVFilterContext *resize_buffersink_ctx{nullptr};
  AVFilterContext *resize_buffersrc_ctx{nullptr};
  AVFilterGraph *resize_graph{nullptr};
  AVFrame *decode_frame{nullptr};
  AVFrame *ref_frame{nullptr};
  AVFrame *encode_frame{nullptr};
  AVFrame *rotate_frame{nullptr};
  AVFrame *resize_frame{nullptr};
};

extern "C" __attribute__((visibility("default"))) __attribute__((used))
int to_thumbnail(const char *input_path, const char **output_paths, double *tms, int tms_len, int width, int height, double threshold) {
  fprintf(stderr, "input ... %s, output ... %s\n", input_path, output_paths[0]);
#ifndef WITH_OMP
  VideoThubmnail video_thumbnail;
  return video_thumbnail.run(input_path, output_paths, tms, tms_len, width, height, threshold);
#else 
  // int threads = get_cpu_num();
  // fprintf(stderr, "threads ... %d, tms_len ... %d\n", threads, tms_len);
  // #pragma omp parallel for num_threads(tms_len)
  // for (int idx = 0; idx < tms_len; ++idx) {
  //   fprintf(stderr, "omp for ... %d\n", idx);
  //   VideoThubmnail video_thumbnail;
  //   video_thumbnail.run(input_path, output_paths + idx, tms + idx, 1, width, height, threshold);    
  // }
  // return 0;
  std::vector<VideoThubmnail *> video_thumbnails;
  std::vector<std::thread *> threads;
  for (int idx = 0; idx < tms_len; ++idx) {
    fprintf(stderr, "Threads Idx ... %d\n", idx);
    video_thumbnails.push_back(new VideoThubmnail());
    threads.push_back(new std::thread(&VideoThubmnail::run, video_thumbnails[idx], input_path, output_paths + idx, tms + idx, 1, width, height, threshold));
  }
  for (int idx = 0; idx < tms_len; ++idx) {
    threads[idx]->join();
  }
  // TODO: Release
  for (int idx = 0; idx < tms_len; ++idx) {
    delete video_thumbnails[idx];
    delete threads[idx];
  }
  return 0;
#endif  // WITH_OMP
}

#ifdef WITH_MAIN
int main(int argv, char *args[]) {
  if (argv < 4) {
    fprintf(stderr, "Usage: %s input_path output_path mod\n", args[0]);
    return -1;
  }

  int n = (argv - 3) / 2;
  fprintf(stderr, "n ... %d\n", n);

  const char *input_path = args[1];
  std::vector<const char *> output_paths;
  for (int i = 0; i < n; ++i) {
    output_paths.push_back(args[i + 2]);
  }
  int mod = std::atoi(args[3 + n - 1]);  // If no-end loop.

  std::vector<double> tms;
  for (int i = 0; i < n; ++i) {
    tms.push_back(std::atof(args[i + 4 + n - 1]));
  }

  double threshold = 0;

  int r = to_thumbnail(input_path, output_paths.data(), tms.data(), tms.size(), 0, 300, threshold);
  fprintf(stderr, "r ... %d\n", r);
  while (mod) {
    int r = to_thumbnail(input_path, output_paths.data(), tms.data(), tms.size(), 300, 300, threshold);
    fprintf(stderr, "r ... %d\n", r);
  }

  return 0;
}
#endif  // WITH_MAIN
