// SPDX-License-Identifier: GPL-2.0-or-later
#include "Renderer.H"
#include "Timeline.H"
#include "timebase.H"
#include "media/Stereo.H"
#include "ProgressDialog/IProgressListener.H"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
extern "C" {
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
}
namespace nle {
Renderer::Renderer(IVideoFileWriter *file):writer(file) {
  if(!writer){problem="No output writer";return;}
  if(!writer->ok()){problem=writer->error();return;}
  try {
    auto format=writer->format();
    double aspect=format->pixel_aspect_ratio*(writer->stereoLayout()==2?2:1);
    render.reset(new StereoRender(g_timeline,format->w,format->h,aspect,writer->stereoLayout()));
  }catch(const std::exception &e){problem=e.what();}
}
Renderer::~Renderer()=default;
bool Renderer::ok(){return problem.empty()&&writer&&writer->ok()&&render;}
const char *Renderer::error() const {return !problem.empty()?problem.c_str():(writer?writer->error():"No output writer");}
void Renderer::go(IProgressListener *listener) {
  if(!ok())return;
  SwrContext *resample=nullptr;
  if(listener)listener->start();
  try {
    auto format=writer->format();auto timeline=render->audioTimeline();
    auto fps=AVRational{format->framerate.frame_duration,format->framerate.timescale};
    int64_t duration=timeline->durationTicks();
    int64_t frames=av_rescale_q_rnd(duration,{1,NLE_TIME_BASE},fps,AV_ROUND_UP);
    if(frames<=0)throw std::runtime_error("The project is empty");
    // The timeline mixer is always 48 kHz. Legacy writers may request another
    // rate; resample once after mixing, preserving audio automation timing.
    if(writer->samplerate()!=48000) {
      AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
      if(swr_alloc_set_opts2(&resample,&stereo,AV_SAMPLE_FMT_FLT,writer->samplerate(),&stereo,AV_SAMPLE_FMT_FLT,48000,0,nullptr)<0||swr_init(resample)<0)
        throw std::runtime_error("Cannot initialize legacy output audio resampling");
    }
    timeline->sampleseek(1,0);timeline->sort();
    int64_t samples=0,totalSamples=av_rescale_q(duration,{1,NLE_TIME_BASE},{1,48000});
    float audio[32000*2];
    for(int64_t index=0;index<frames;++index) {
      int64_t target=std::min(totalSamples,av_rescale_q(index+1,fps,{1,48000}));
      while(samples<target) {
        int count=std::min<int64_t>(32000,target-samples);std::memset(audio,0,count*2*sizeof(float));
        timeline->fillBuffer(audio,count);timeline->sampleseek(0,count);samples+=count;
        if(resample){int capacity=swr_get_out_samples(resample,count);std::vector<float> converted(capacity*2);uint8_t *out=reinterpret_cast<uint8_t*>(converted.data());const uint8_t *in=reinterpret_cast<uint8_t*>(audio);int n=swr_convert(resample,&out,capacity,&in,count);if(n<0)throw std::runtime_error("Output audio resampling failed");writer->encodeAudioFrame(converted.data(),n);}
        else writer->encodeAudioFrame(audio,count);
      }
      int64_t position=av_rescale_q(index,fps,{1,NLE_TIME_BASE});
      writer->encodeVideoFrame(render->frame(position));
      if(!writer->ok())throw std::runtime_error(writer->error());
      if(listener&&listener->progress(double(index+1)/frames)){writer->cancel();problem="Rendering cancelled";break;}
    }
    if(problem.empty()) {
      if(resample){float converted[4096*2];uint8_t *out=reinterpret_cast<uint8_t*>(converted);int n=swr_convert(resample,&out,4096,nullptr,0);if(n>0)writer->encodeAudioFrame(converted,n);}
      if(!writer->finish())problem=writer->error();
    }
  } catch(const std::exception &e){problem=e.what();writer->cancel();}
  swr_free(&resample);if(listener)listener->end();
}
}
