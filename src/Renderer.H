// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _RENDERER_H_
#define _RENDERER_H_
#include "IVideoFileWriter.H"
#include "fps_definitions.H"
#include <memory>
#include <string>
namespace nle {
class IProgressListener;
class StereoRender;
class Renderer {
public:
  Renderer(IVideoFileWriter *writer);
  ~Renderer();
  void go(IProgressListener *listener=0);
  bool ok();
  const char *error() const;
private:
  std::unique_ptr<IVideoFileWriter> writer;
  std::unique_ptr<StereoRender> render;
  std::string problem;
};
}
#endif
