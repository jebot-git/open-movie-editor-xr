// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IVideoFileWriter.H"
#include <memory>
#include <string>
namespace nle {
class EncodingPreset;
class FFmpegWriter : public IVideoFileWriter {
public:
  FFmpegWriter(EncodingPreset *preset,const char *filename);
  ~FFmpegWriter();
  void encodeVideoFrame(LazyFrame *frame);
  void encodeAudioFrame(float *buffer,int frames);
  video_format *format();
  int samplerate(){return 48000;}
  bool ok() const;
  const char *error() const;
  bool finish();
  void cancel();
  int stereoLayout() const;
private:
  struct State;
  std::unique_ptr<State> state;
};
}
