// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
namespace nle {
// Each clip owns its demuxer and decoder. No process-global FFmpeg seek state.
class FFmpegInput {
public:
  AVFormatContext *file = nullptr;
  AVCodecContext *codec = nullptr;
  AVFrame *frame = nullptr, *host = nullptr;
  AVPacket *packet = nullptr;
  AVBufferRef *device = nullptr, *vaapi = nullptr;
  int stream = -1;
  bool eof = false, draining = false, hardware = false;
  std::string error;
  FFmpegInput(const std::string &path, AVMediaType type);
  ~FFmpegInput();
  bool ok() const { return codec != nullptr && error.empty(); }
  bool next();
  AVFrame *cpu();
  bool seek(double seconds);
  double seconds() const;
  double duration() const;
  const char *name() const;
private:
  bool openCodec(bool qsv);
};
const char *qsv_decoder(AVCodecID id);
std::string ffmpeg_error(int code);
}
