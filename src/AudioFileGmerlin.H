// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _AUDIO_FILE_GMERLIN_H_
#define _AUDIO_FILE_GMERLIN_H_
#include "IAudioFile.H"
#include <memory>
namespace nle {
class FFmpegInput;
class AudioFileGmerlin : public IAudioFile {
public:
  AudioFileGmerlin(std::string filename);
  ~AudioFileGmerlin();
  void seek(int64_t sample);
  int fillBuffer(float *output,unsigned long frames);
private:
  struct State;
  std::unique_ptr<FFmpegInput> input;
  std::unique_ptr<State> state;
};
}
#endif
