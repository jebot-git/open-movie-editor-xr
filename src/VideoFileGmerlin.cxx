// SPDX-License-Identifier: GPL-2.0-or-later
#include "VideoFileGmerlin.H"
#include "media/FFmpegInput.H"
#include "LazyFrame.H"
#include "ErrorDialog/IErrorHandler.H"
#include <cmath>
#include <algorithm>
#include <regex>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include "media/Stereo.H"
extern "C" {
#include <libavutil/stereo3d.h>
}
namespace nle {
struct VideoFileGmerlin::State {
  gavl_video_format_t format{};
  gavl_video_frame_t *frame=nullptr;
  LazyFrame *lazy=nullptr;
  SwsContext *scale=nullptr;
  int64_t ticks=1,length=0,target=0;
  double fps=25;
  bool have=false;
  int eye=-1,overrideLayout=-1,overrideSwap=-1,defaultLayout=-1,defaultSwap=-1,detectedLayout=0,detectedSwap=0,layout=0,swap=0;
  std::vector<uint8_t> rgba;
  ~State(){delete lazy;if(frame)gavl_video_frame_destroy(frame);sws_freeContext(scale);}
};
VideoFileGmerlin::VideoFileGmerlin(std::string filename,IProgressListener *progress)
 :m_progress(progress),input(new FFmpegInput(filename,AVMEDIA_TYPE_VIDEO)),state(new State) {
  m_filename=filename;m_width=m_height=0;
  if(!input->ok())return;
  auto stream=input->file->streams[input->stream];
  auto rate=av_guess_frame_rate(input->file,stream,nullptr);
  state->fps=rate.num&&rate.den?av_q2d(rate):25.;
  if(!std::isfinite(state->fps)||state->fps<=0)return;
  state->ticks=std::max<int64_t>(1,std::llround(NLE_TIME_BASE/state->fps));
  state->length=std::llround(input->duration()*NLE_TIME_BASE);
  m_width=input->codec->width;m_height=input->codec->height;
  state->eye=DecodeContext::eye;state->defaultLayout=DecodeContext::input;state->defaultSwap=DecodeContext::swap;
  std::string stem=filename;std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return std::toupper(c);});
  auto token=[&](const char *pattern){return std::regex_search(stem,std::regex(pattern));};
  auto mode=av_dict_get(stream->metadata,"stereo_mode",nullptr,0);
  auto own=av_dict_get(stream->metadata,"OME_SBS_LAYOUT",nullptr,0);
  bool tagged=false;
  if(mode){std::string value=mode->value;tagged=true;
    if(value=="mono"||value=="0"||value=="2d")state->detectedLayout=0;
    else if(value=="left_right"||value=="right_left"||value=="sbs2l"||value=="sbs2r"||value=="1"||value=="11") {state->detectedLayout=1;state->detectedSwap=value=="right_left"||value=="sbs2r"||value=="11";}
    else state->detectedLayout=-2;
  } else if(auto side=av_packet_side_data_get(stream->codecpar->coded_side_data,stream->codecpar->nb_coded_side_data,AV_PKT_DATA_STEREO3D)) {
    auto stereo=reinterpret_cast<const AVStereo3D*>(side->data);tagged=true;
    state->detectedLayout=stereo->type==AV_STEREO3D_SIDEBYSIDE?1:(stereo->type==AV_STEREO3D_2D?0:-2);
    state->detectedSwap=(stereo->flags&AV_STEREO3D_FLAG_INVERT)!=0;
  }
  if(own&&std::string(own->value)=="half-sbs")state->detectedLayout=2;
  else if((!tagged||state->detectedLayout==1)&&token("(^|[^A-Z0-9])(H[-_. ]?SBS|HALF[-_. ]?SBS)([^A-Z0-9]|$)"))state->detectedLayout=2;
  else if(!tagged&&token("(^|[^A-Z0-9])(F[-_. ]?SBS|FULL[-_. ]?SBS|SBS)([^A-Z0-9]|$)"))state->detectedLayout=1;
  if(!tagged&&token("(^|[^A-Z0-9])(RL|CROSS|CROSS[-_. ]EYED)([^A-Z0-9]|$)"))state->detectedSwap=1;
  auto &f=state->format;f.frame_width=f.image_width=m_width;f.frame_height=f.image_height=m_height;
  f.pixel_width=stream->sample_aspect_ratio.num?stream->sample_aspect_ratio.num:1;
  f.pixel_height=stream->sample_aspect_ratio.den?stream->sample_aspect_ratio.den:1;
  f.pixelformat=GAVL_RGBA_32;f.interlace_mode=GAVL_INTERLACE_NONE;
  f.timescale=rate.num?rate.num:25;f.frame_duration=rate.den?rate.den:1;f.framerate_mode=GAVL_FRAMERATE_CONSTANT;
  configureStereo(-1,-1);
}
VideoFileGmerlin::~VideoFileGmerlin()=default;
bool VideoFileGmerlin::ok(){return input->ok() && state->frame && state->length>0;}
int64_t VideoFileGmerlin::length(){return state->length;}
int64_t VideoFileGmerlin::ticksPerFrame(){return state->ticks;}
const char *VideoFileGmerlin::decoder(){return input->name();}
void VideoFileGmerlin::seek(int64_t position){seekToFrame(position/state->ticks);}
void VideoFileGmerlin::seekToFrame(int64_t frame){state->target=std::max<int64_t>(0,frame);input->seek(state->target/state->fps);}
LazyFrame *VideoFileGmerlin::read(){
  if(!ok())return nullptr;
  const double target=state->target/state->fps;
  while(input->next()) {
    if(input->seconds()>=0 && input->seconds()+.5/state->fps<target)continue;
    AVFrame *src=input->cpu();if(!src)return nullptr;
    state->scale=sws_getCachedContext(state->scale,src->width,src->height,(AVPixelFormat)src->format,
                                      src->width,src->height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
    if(!state->scale)return nullptr;
    bool extract=state->eye>=0&&state->layout>0;
    if(extract)state->rgba.resize(size_t(src->width)*src->height*4);
    uint8_t *planes[4]={extract?state->rgba.data():state->frame->planes[0],nullptr,nullptr,nullptr};
    int strides[4]={extract?src->width*4:state->frame->strides[0],0,0,0};
    sws_scale(state->scale,src->data,src->linesize,0,src->height,planes,strides);
    if(extract)for(int y=0;y<m_height;++y)std::memcpy(state->frame->planes[0]+y*state->frame->strides[0],
      state->rgba.data()+size_t(y)*src->width*4+(state->eye^state->swap)*m_width*4,m_width*4);
    state->have=true;state->target++;state->lazy->dirty(true);state->lazy->alpha(1);
    return state->lazy;
  }
  return state->have?state->lazy:nullptr;
}
int VideoFileGmerlin::sourceStereoLayout() const{return state->layout;}
int VideoFileGmerlin::stereoOverride() const{return state->overrideLayout;}
int VideoFileGmerlin::eyeOrderOverride() const{return state->overrideSwap;}
void VideoFileGmerlin::configureStereo(int layout,int swap) {
  if(!input->ok())return;
  auto &s=*state;s.overrideLayout=layout;s.overrideSwap=swap;
  s.layout=layout>=0?layout:(s.defaultLayout>=0?s.defaultLayout:s.detectedLayout);
  s.swap=swap>=0?swap:(s.defaultSwap>=0?s.defaultSwap:s.detectedSwap);
  if(s.eye>=0 && s.layout<0)throw std::runtime_error("Unsupported stereo metadata in "+m_filename+"; choose a clip input layout explicitly");
  auto stream=input->file->streams[input->stream];
  AVRational par=av_guess_sample_aspect_ratio(input->file,stream,nullptr);
  if(par.num<=0||par.den<=0)par={1,1};
  m_width=input->codec->width;m_height=input->codec->height;
  if(s.eye>=0&&s.layout>0){
    if(m_width%4)throw std::runtime_error("SBS source width must be divisible by four");
    m_width/=2;
    if(s.layout==2&&par.num==par.den)par.num*=2;
  }
  auto &f=s.format;f.frame_width=f.image_width=m_width;f.frame_height=f.image_height=m_height;
  f.pixel_width=par.num;f.pixel_height=par.den;
  delete s.lazy;s.lazy=nullptr;if(s.frame)gavl_video_frame_destroy(s.frame);
  s.frame=gavl_video_frame_create(&f);s.lazy=new LazyFrame(&f);s.lazy->put_data(s.frame);s.lazy->alpha(1);s.have=false;
  if(std::getenv("OME_TRACE_STEREO"))std::fprintf(stderr,"Source eye=%d layout=%d swap=%d width=%d %s\n",s.eye,s.layout,s.swap,m_width,m_filename.c_str());
  m_lastFramePosition=-1;
}

}
