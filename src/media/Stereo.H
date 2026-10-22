// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <string>
#include <cstdint>
namespace nle {
class Timeline;
class LazyFrame;
// Source layout: -1 metadata/filename auto, 0 mono, 1 full SBS, 2 half SBS.
struct DecodeContext {
  static thread_local int eye,input,swap;
  int oldEye,oldInput,oldSwap;
  DecodeContext(int e,int i=-1,int s=-1);
  ~DecodeContext();
};
class StereoRender {
public:
  StereoRender(Timeline *source,int packedWidth,int height,double pixelAspect,int layout);
  ~StereoRender();
  LazyFrame *frame(int64_t position);
  Timeline *audioTimeline();
private:
  struct State;
  std::unique_ptr<State> state;
};
}
