// SPDX-License-Identifier: GPL-2.0-or-later
#include "Stereo.H"
#include "Timeline.H"
#include "timeline/Track.H"
#include "DummyClip.H"
#include "FilterClip.H"
#include "MissingEffect.H"
#include "LazyFrame.H"
#include "globals.H"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
extern "C" {
#include <libavutil/rational.h>
}
namespace nle {
thread_local int DecodeContext::eye=-1;
thread_local int DecodeContext::input=-1;
thread_local int DecodeContext::swap=-1;
DecodeContext::DecodeContext(int e,int i,int s):oldEye(eye),oldInput(input),oldSwap(s){eye=e;input=i;swap=s;}
DecodeContext::~DecodeContext(){eye=oldEye;input=oldInput;swap=oldSwap;}
struct StereoRender::State {
  std::unique_ptr<Timeline> eyes[2];
  gavl_video_format_t eyeFormat{},packedFormat{};
  gavl_video_frame_t *pixels=nullptr;
  std::unique_ptr<LazyFrame> packed;
  int count=1;
  ~State(){auto current=g_timeline;eyes[0].reset();eyes[1].reset();g_timeline=current;if(pixels)gavl_video_frame_destroy(pixels);}
};
StereoRender::StereoRender(Timeline *source,int width,int height,double par,int layout):state(new State) {
  auto &s=*state;s.count=layout?2:1;auto original=g_timeline;
  auto snapshot=source->snapshot();
  auto ratio=av_d2q(par,100000);
  s.packedFormat.frame_width=s.packedFormat.image_width=width;
  s.packedFormat.frame_height=s.packedFormat.image_height=height;
  s.packedFormat.pixel_width=ratio.num;s.packedFormat.pixel_height=ratio.den;
  s.packedFormat.pixelformat=GAVL_RGBA_32;s.packedFormat.interlace_mode=GAVL_INTERLACE_NONE;
  s.eyeFormat=s.packedFormat;s.eyeFormat.frame_width=s.eyeFormat.image_width=width/s.count;
  // The same per-eye physical geometry applies before all effect instances.
  try {
    for(int eye=0;eye<s.count;++eye) {
      DecodeContext context(eye,source->stereoInput,source->stereoSwap);
      s.eyes[eye].reset(new Timeline);s.eyes[eye]->render_mode(true);
      if(!s.eyes[eye]->restore(snapshot))throw std::runtime_error("Cannot clone the project for rendering");
      for(auto t=s.eyes[eye]->getTracks();t;t=t->next)for(auto c=t->track->getClips();c;c=c->next){
        if(dynamic_cast<DummyClip*>(c->clip))throw std::runtime_error("Missing or unsupported media: "+c->clip->filename());
        if(auto clip=dynamic_cast<FilterClip*>(c->clip))for(auto f=clip->getFilters();f;f=f->next)if(dynamic_cast<MissingEffect*>(f->filter))throw std::runtime_error(f->filter->name());
      }
      s.eyes[eye]->sort();s.eyes[eye]->prepareFormat(&s.eyeFormat);
    }
    g_timeline=original;
    s.pixels=gavl_video_frame_create(&s.packedFormat);if(!s.pixels)throw std::bad_alloc();
    s.packed.reset(new LazyFrame(&s.packedFormat));s.packed->put_data(s.pixels);s.packed->alpha(1);
  }catch(...){g_timeline=original;throw;}
}
StereoRender::~StereoRender()=default;
Timeline *StereoRender::audioTimeline(){return state->eyes[0].get();}
LazyFrame *StereoRender::frame(int64_t position) {
  auto &s=*state;gavl_video_frame_clear(s.pixels,&s.packedFormat);
  // Render each eye through an independent copy of the ORIGINAL timeline.
  // Temporal effects and node state are never shared between eyes.
  for(int eye=0;eye<s.count;++eye) {
    auto f=s.eyes[eye]->getBlendedFrame(position);if(!f)continue;
    f->set_target(&s.eyeFormat);auto pixels=f->target();
    if(std::getenv("OME_TRACE_STEREO")){auto p=pixels->planes[0]+(s.eyeFormat.image_height/2)*pixels->strides[0]+(s.eyeFormat.image_width/2)*4;std::fprintf(stderr,"Stereo eye %d output %d,%d,%d (%dx%d)\n",eye,p[0],p[1],p[2],s.eyeFormat.image_width,s.eyeFormat.image_height);}
    for(int y=0;y<s.eyeFormat.image_height;++y)
      std::memcpy(s.pixels->planes[0]+y*s.pixels->strides[0]+eye*s.eyeFormat.image_width*4,
                  pixels->planes[0]+y*pixels->strides[0],s.eyeFormat.image_width*4);
  }
  s.packed->dirty(true);return s.packed.get();
}
}
