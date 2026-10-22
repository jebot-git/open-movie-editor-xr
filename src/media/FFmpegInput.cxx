// SPDX-License-Identifier: GPL-2.0-or-later
#include "FFmpegInput.H"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <thread>
extern "C" {
#include <libavutil/error.h>
}
namespace nle {
std::string ffmpeg_error(int code) { char text[AV_ERROR_MAX_STRING_SIZE]; av_strerror(code,text,sizeof(text));return text; }
const char *qsv_decoder(AVCodecID id) {
  switch(id) {
  case AV_CODEC_ID_H264:return "h264_qsv"; case AV_CODEC_ID_HEVC:return "hevc_qsv";
  case AV_CODEC_ID_AV1:return "av1_qsv"; case AV_CODEC_ID_VP9:return "vp9_qsv";
  case AV_CODEC_ID_MPEG2VIDEO:return "mpeg2_qsv"; default:return nullptr;
  }
}
static AVPixelFormat hardware_format(AVCodecContext *,const AVPixelFormat *formats) {
  for(auto p=formats;*p!=AV_PIX_FMT_NONE;++p) if(*p==AV_PIX_FMT_QSV)return *p;
  return AV_PIX_FMT_NONE;
}
FFmpegInput::FFmpegInput(const std::string &path, AVMediaType type) {
  int status=avformat_open_input(&file,path.c_str(),nullptr,nullptr);
  if(status<0){error=ffmpeg_error(status);return;}
  status=avformat_find_stream_info(file,nullptr);
  if(status<0){error=ffmpeg_error(status);return;}
  stream=av_find_best_stream(file,type,-1,-1,nullptr,0);
  if(stream<0){error=ffmpeg_error(stream);return;}
  bool use_hw=type==AVMEDIA_TYPE_VIDEO && qsv_decoder(file->streams[stream]->codecpar->codec_id);
  const char *mode=std::getenv("OME_HW_DECODE");
  if(mode && std::string(mode)=="off")use_hw=false;
  if(type==AVMEDIA_TYPE_VIDEO && mode && std::string(mode)=="required" && !use_hw){error="This codec has no QSV decoder";return;}
  if(!openCodec(use_hw)) {
    if(mode && std::string(mode)=="required"){error="QSV decoder unavailable";return;}
    if(!openCodec(false)){error="Cannot open media decoder";return;}
  }
  frame=av_frame_alloc();host=av_frame_alloc();packet=av_packet_alloc();
  if(!frame||!host||!packet)error="Cannot allocate decoder buffers";
}
bool FFmpegInput::openCodec(bool qsv) {
  avcodec_free_context(&codec);av_buffer_unref(&device);av_buffer_unref(&vaapi);
  auto params=file->streams[stream]->codecpar;
  const AVCodec *decoder=qsv?avcodec_find_decoder_by_name(qsv_decoder(params->codec_id)):avcodec_find_decoder(params->codec_id);
  if(!decoder)return false;
  codec=avcodec_alloc_context3(decoder);if(!codec)return false;
  if(avcodec_parameters_to_context(codec,params)<0)return false;
  codec->pkt_timebase=file->streams[stream]->time_base;
  codec->thread_count=std::min(8u,std::max(1u,std::thread::hardware_concurrency()));
  if(qsv) {
    const char *node=std::getenv("OME_QSV_DEVICE");
    if(!node)node="/dev/dri/by-path/pci-0000:03:00.0-render";
    if(av_hwdevice_ctx_create(&vaapi,AV_HWDEVICE_TYPE_VAAPI,node,nullptr,0)<0)return false;
    if(av_hwdevice_ctx_create_derived(&device,AV_HWDEVICE_TYPE_QSV,vaapi,0)<0)return false;
    codec->hw_device_ctx=av_buffer_ref(device);codec->get_format=hardware_format;
    codec->extra_hw_frames=16;
  }
  if(avcodec_open2(codec,decoder,nullptr)<0)return false;
  hardware=qsv;
  return true;
}
FFmpegInput::~FFmpegInput() {
  av_packet_free(&packet);av_frame_free(&frame);av_frame_free(&host);avcodec_free_context(&codec);
  avformat_close_input(&file);av_buffer_unref(&device);av_buffer_unref(&vaapi);
}
bool FFmpegInput::next() {
  if(!ok())return false;
  for(;;) {
    int status=avcodec_receive_frame(codec,frame);
    if(status==0)return true;
    if(status==AVERROR_EOF)return false;
    if(status!=AVERROR(EAGAIN)){error=ffmpeg_error(status);return false;}
    if(eof) {
      if(draining)return false;
      draining=true;
      status=avcodec_send_packet(codec,nullptr);
      if(status<0 && status!=AVERROR_EOF){error=ffmpeg_error(status);return false;}
      continue;
    }
    for(;;) {
      status=av_read_frame(file,packet);if(status<0){eof=true;break;}
      if(packet->stream_index==stream)break;
      av_packet_unref(packet);
    }
    if(eof)continue;
    status=avcodec_send_packet(codec,packet);av_packet_unref(packet);
    if(status<0){error=ffmpeg_error(status);return false;}
  }
}
AVFrame *FFmpegInput::cpu() {
  if(frame->format!=AV_PIX_FMT_QSV)return frame;
  av_frame_unref(host);
  int status=av_hwframe_transfer_data(host,frame,0);
  if(status<0){error=ffmpeg_error(status);return nullptr;}
  av_frame_copy_props(host,frame);return host;
}
bool FFmpegInput::seek(double seconds) {
  auto s=file->streams[stream];
  int64_t target=static_cast<int64_t>(seconds/av_q2d(s->time_base))+(s->start_time==AV_NOPTS_VALUE?0:s->start_time);
  int status=av_seek_frame(file,stream,target,AVSEEK_FLAG_BACKWARD);
  if(status<0)return false;
  avcodec_flush_buffers(codec);av_frame_unref(frame);eof=draining=false;return true;
}
double FFmpegInput::seconds() const {
  int64_t pts=frame->best_effort_timestamp;
  if(pts==AV_NOPTS_VALUE)return -1;
  auto s=file->streams[stream];return (pts-(s->start_time==AV_NOPTS_VALUE?0:s->start_time))*av_q2d(s->time_base);
}
double FFmpegInput::duration() const {
  auto s=file->streams[stream];
  if(s->duration!=AV_NOPTS_VALUE)return s->duration*av_q2d(s->time_base);
  auto entry=av_dict_get(s->metadata,"DURATION",nullptr,0);
  if(entry){int h=0,m=0;double sec=0;if(std::sscanf(entry->value,"%d:%d:%lf",&h,&m,&sec)==3)return h*3600+m*60+sec;}
  return file->duration==AV_NOPTS_VALUE?0:double(file->duration)/AV_TIME_BASE;
}
const char *FFmpegInput::name() const {return hardware?"FFmpeg / Intel QSV":"FFmpeg";}
}
