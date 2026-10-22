// SPDX-License-Identifier: GPL-2.0-or-later
// Historical public reader interface retained; decoding now uses FFmpeg/QSV.
#ifndef _VIDEO_FILE_GMERLIN_H_
#define _VIDEO_FILE_GMERLIN_H_
#include "IVideoFile.H"
#include <memory>
namespace nle {
class FFmpegInput;
class LazyFrame;
class IProgressListener;
class VideoFileGmerlin : public IVideoFile {
public:
  VideoFileGmerlin(std::string filename,IProgressListener *progress=0);
  ~VideoFileGmerlin();
  bool ok();
  void configureStereo(int layout,int swap);
  int stereoOverride() const;
  int sourceStereoLayout() const;
  int eyeOrderOverride() const;
  int64_t length();
  LazyFrame *read();
  void seek(int64_t position);
  const char *decoder();
  IProgressListener *m_progress;
  int64_t ticksPerFrame();
protected:
  void seekToFrame(int64_t frame);
private:
  std::unique_ptr<FFmpegInput> input;
  struct State;
  std::unique_ptr<State> state;
};
}
#endif
