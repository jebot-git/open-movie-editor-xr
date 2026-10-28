// SPDX-License-Identifier: GPL-2.0-or-later
#include "FFmpegWriter.H"
#include "FFmpegInput.H"
#include "EncoderRegistry.H"
#include "RenderDialog/EncodingPreset.H"
#include "LazyFrame.H"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <unistd.h>
extern "C" {
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libavutil/stereo3d.h>
}
namespace nle {
struct FFmpegWriter::State {
  video_format format{};
  int stereo=0;
  std::string destination,temporary,problem;
  AVFormatContext *mux=nullptr;
  AVCodecContext *video=nullptr,*audio=nullptr;
  AVStream *vs=nullptr,*as=nullptr;
  AVFrame *pixels=nullptr;
  AVPacket *packet=nullptr;
  AVBufferRef *device=nullptr,*vaapi=nullptr,*pool=nullptr;
  SwsContext *scale=nullptr;
  SwrContext *resample=nullptr;
  AVAudioFifo *fifo=nullptr;
  int64_t videoPts=0,audioPts=0;
  bool hardware=false,committed=false,header=false;
  gavl_video_format_t rgba{};
  ~State(){
    if(mux&&mux->pb)avio_closep(&mux->pb);
    avformat_free_context(mux);avcodec_free_context(&video);avcodec_free_context(&audio);
    av_frame_free(&pixels);av_packet_free(&packet);av_buffer_unref(&pool);av_buffer_unref(&device);av_buffer_unref(&vaapi);
    sws_freeContext(scale);swr_free(&resample);if(fifo)av_audio_fifo_free(fifo);
    if(!temporary.empty())std::remove(temporary.c_str());
  }
  void check(int result,const char *operation){if(result<0)throw std::runtime_error(std::string(operation)+": "+ffmpeg_error(result));}
  void packets(AVCodecContext *codec,AVStream *stream,AVFrame *frame) {
    check(avcodec_send_frame(codec,frame),"Send encoder frame");
    for(;;){int result=avcodec_receive_packet(codec,packet);if(result==AVERROR(EAGAIN)||result==AVERROR_EOF)break;
      check(result,"Receive encoded packet");if(codec->codec_type==AVMEDIA_TYPE_VIDEO&&packet->duration<=0)packet->duration=1;av_packet_rescale_ts(packet,codec->time_base,stream->time_base);packet->stream_index=stream->index;
      result=av_interleaved_write_frame(mux,packet);av_packet_unref(packet);check(result,"Mux encoded packet");}
  }
  void audioFrames(bool flush) {
    int block=audio->frame_size?audio->frame_size:1024;
    while(av_audio_fifo_size(fifo)>=block || (flush&&av_audio_fifo_size(fifo)>0)) {
      int count=std::min(block,av_audio_fifo_size(fifo));
      AVFrame *frame=av_frame_alloc();if(!frame)throw std::bad_alloc();
      frame->format=audio->sample_fmt;frame->sample_rate=audio->sample_rate;
      av_channel_layout_copy(&frame->ch_layout,&audio->ch_layout);
      frame->nb_samples=(audio->codec->capabilities&AV_CODEC_CAP_SMALL_LAST_FRAME)?count:block;
      int result=av_frame_get_buffer(frame,0);
      if(result>=0) {
        av_samples_set_silence(frame->extended_data,0,frame->nb_samples,2,audio->sample_fmt);
        av_audio_fifo_read(fifo,reinterpret_cast<void**>(frame->extended_data),count);
        frame->pts=audioPts;audioPts+=frame->nb_samples;
        try{packets(audio,as,frame);}catch(...){av_frame_free(&frame);throw;}
      }
      av_frame_free(&frame);check(result,"Allocate audio frame");
    }
  }
  void convertedAudio(const float *data,int count) {
    int size=swr_get_out_samples(resample,count);size=std::max(32,size);
    uint8_t **converted=nullptr;int linesize=0;
    check(av_samples_alloc_array_and_samples(&converted,&linesize,2,size,audio->sample_fmt,0),"Allocate audio conversion");
    const uint8_t *source=reinterpret_cast<const uint8_t*>(data);
    int got=swr_convert(resample,converted,size,data?&source:nullptr,count);
    if(got>0) {
      int result=av_audio_fifo_realloc(fifo,av_audio_fifo_size(fifo)+got);
      if(result<0){av_freep(&converted[0]);av_freep(&converted);check(result,"Grow audio FIFO");}
      av_audio_fifo_write(fifo,reinterpret_cast<void**>(converted),got);
    }
    av_freep(&converted[0]);av_freep(&converted);check(got,"Resample audio");audioFrames(false);
  }
};
FFmpegWriter::FFmpegWriter(EncodingPreset *preset,const char *filename):state(new State) {
  auto &s=*state;
  try {
    preset->getFormat(&s.format);s.stereo=preset->stereoLayout();s.destination=filename;
    if(s.stereo==1)s.format.w*=2;
    if(s.format.w<=0||s.format.h<=0||s.format.w%2||s.format.h%2||s.format.framerate.timescale<=0||s.format.framerate.frame_duration<=0)
      throw std::runtime_error("Even positive output dimensions and a positive frame rate are required");
    s.check(avformat_alloc_output_context2(&s.mux,nullptr,preset->container().empty()?nullptr:preset->container().c_str(),filename),"Choose output container");
    if(!s.mux)throw std::runtime_error("Unsupported output container");
    std::string pattern=s.destination+".ome-XXXXXX";std::vector<char> temp(pattern.begin(),pattern.end());temp.push_back(0);
    int fd=mkstemp(temp.data());if(fd<0)throw std::runtime_error("Cannot create temporary output");close(fd);s.temporary=temp.data();
    auto encoder=video_encoder_name(s.format.video_codec);const AVCodec *codec=avcodec_find_encoder_by_name(encoder.c_str());
    if(!codec)throw std::runtime_error("Video encoder unavailable: "+encoder);
    s.hardware=encoder.size()>4&&encoder.substr(encoder.size()-4)=="_qsv";
    s.video=avcodec_alloc_context3(codec);s.vs=avformat_new_stream(s.mux,nullptr);s.packet=av_packet_alloc();
    if(!s.video||!s.vs||!s.packet)throw std::bad_alloc();
    s.video->width=s.format.w;s.video->height=s.format.h;
    s.video->time_base={s.format.framerate.frame_duration,s.format.framerate.timescale};
    s.video->framerate=av_inv_q(s.video->time_base);
    s.video->sample_aspect_ratio=av_d2q(s.format.pixel_aspect_ratio*(s.stereo==2?2:1),100000);
    s.video->gop_size=preset->videoInt("ome_gop",120);
    int quality=preset->videoInt("ome_quality",23);
    if(s.mux->oformat->flags&AVFMT_GLOBALHEADER)s.video->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    if(s.hardware) {
      std::string node=preset->videoString("ome_qsv_device","/dev/dri/by-path/pci-0000:03:00.0-render");
      if(auto env=std::getenv("OME_QSV_DEVICE"))node=env;
      s.check(av_hwdevice_ctx_create(&s.vaapi,AV_HWDEVICE_TYPE_VAAPI,node.c_str(),nullptr,0),"Open Intel GPU");
      s.check(av_hwdevice_ctx_create_derived(&s.device,AV_HWDEVICE_TYPE_QSV,s.vaapi,0),"Create QSV device");
      s.pool=av_hwframe_ctx_alloc(s.device);if(!s.pool)throw std::bad_alloc();
      auto frames=reinterpret_cast<AVHWFramesContext*>(s.pool->data);
      frames->format=AV_PIX_FMT_QSV;frames->sw_format=AV_PIX_FMT_NV12;frames->width=s.video->width;frames->height=s.video->height;frames->initial_pool_size=32;
      s.check(av_hwframe_ctx_init(s.pool),"Create QSV frame pool");
      s.video->hw_frames_ctx=av_buffer_ref(s.pool);s.video->hw_device_ctx=av_buffer_ref(s.device);s.video->pix_fmt=AV_PIX_FMT_QSV;
      if(encoder=="vp9_qsv") {s.video->bit_rate=preset->videoInt("ff_bit_rate_video",8000000);av_opt_set(s.video->priv_data,"low_power","1",0);}
      else s.video->global_quality=quality;
    } else {
      const AVPixelFormat *formats=nullptr;int count=0;
      avcodec_get_supported_config(nullptr,codec,AV_CODEC_CONFIG_PIX_FORMAT,0,reinterpret_cast<const void**>(&formats),&count);
      s.video->pix_fmt=formats?formats[0]:AV_PIX_FMT_YUV420P;
      for(int i=0;i<count;++i)if(formats[i]==AV_PIX_FMT_YUV420P){s.video->pix_fmt=AV_PIX_FMT_YUV420P;break;}
      if(encoder=="libx264"){av_opt_set(s.video->priv_data,"preset","fast",0);av_opt_set_int(s.video->priv_data,"crf",quality,0);}
      else s.video->bit_rate=preset->videoInt("ff_bit_rate_video",8000000);
    }
    s.check(avcodec_open2(s.video,codec,nullptr),"Open video encoder");
    s.check(avcodec_parameters_from_context(s.vs->codecpar,s.video),"Set video stream");s.vs->time_base=s.video->time_base;s.vs->sample_aspect_ratio=s.video->sample_aspect_ratio;
    if(s.stereo) {
      av_dict_set(&s.vs->metadata,"stereo_mode","left_right",0);
      av_dict_set(&s.vs->metadata,"OME_SBS_LAYOUT",s.stereo==1?"full-sbs":"half-sbs",0);
      auto data=av_stereo3d_alloc();if(!data)throw std::bad_alloc();data->type=AV_STEREO3D_SIDEBYSIDE;
      av_packet_side_data_add(&s.vs->codecpar->coded_side_data,&s.vs->codecpar->nb_coded_side_data,AV_PKT_DATA_STEREO3D,reinterpret_cast<uint8_t*>(data),sizeof(*data),0);
    }
    if(std::strcmp(s.format.audio_codec,"none")!=0) {
    auto audioName=audio_encoder_name(s.format.audio_codec);const AVCodec *ac=avcodec_find_encoder_by_name(audioName.c_str());
    if(!ac)throw std::runtime_error("Audio encoder unavailable: "+audioName);
    s.audio=avcodec_alloc_context3(ac);s.as=avformat_new_stream(s.mux,nullptr);if(!s.audio||!s.as)throw std::bad_alloc();
    s.audio->sample_rate=s.format.samplerate;s.audio->time_base={1,s.audio->sample_rate};
    av_channel_layout_default(&s.audio->ch_layout,2);
    const AVSampleFormat *formats=nullptr;int count=0;avcodec_get_supported_config(nullptr,ac,AV_CODEC_CONFIG_SAMPLE_FORMAT,0,reinterpret_cast<const void**>(&formats),&count);
    s.audio->sample_fmt=formats?formats[0]:AV_SAMPLE_FMT_FLTP;s.audio->bit_rate=preset->audioInt("bit_rate_audio",192000);
    if(s.mux->oformat->flags&AVFMT_GLOBALHEADER)s.audio->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    s.check(avcodec_open2(s.audio,ac,nullptr),"Open audio encoder");
    s.check(avcodec_parameters_from_context(s.as->codecpar,s.audio),"Set audio stream");s.as->time_base=s.audio->time_base;
    AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
    s.check(swr_alloc_set_opts2(&s.resample,&s.audio->ch_layout,s.audio->sample_fmt,s.audio->sample_rate,&stereo,AV_SAMPLE_FMT_FLT,48000,0,nullptr),"Create audio resampler");
    s.check(swr_init(s.resample),"Initialize audio resampler");
    s.fifo=av_audio_fifo_alloc(s.audio->sample_fmt,2,1024);if(!s.fifo)throw std::bad_alloc();
    }
    s.pixels=av_frame_alloc();if(!s.pixels)throw std::bad_alloc();
    s.pixels->format=s.hardware?AV_PIX_FMT_NV12:s.video->pix_fmt;s.pixels->width=s.video->width;s.pixels->height=s.video->height;
    s.check(av_frame_get_buffer(s.pixels,32),"Allocate video conversion");
    s.scale=sws_getContext(s.format.w,s.format.h,AV_PIX_FMT_RGBA,s.format.w,s.format.h,static_cast<AVPixelFormat>(s.pixels->format),SWS_BILINEAR,nullptr,nullptr,nullptr);
    if(!s.scale)throw std::runtime_error("Cannot create output color conversion");
    s.rgba.frame_width=s.rgba.image_width=s.format.w;s.rgba.frame_height=s.rgba.image_height=s.format.h;s.rgba.pixel_width=s.video->sample_aspect_ratio.num;s.rgba.pixel_height=s.video->sample_aspect_ratio.den;s.rgba.pixelformat=GAVL_RGBA_32;s.rgba.interlace_mode=GAVL_INTERLACE_NONE;
    s.check(avio_open(&s.mux->pb,s.temporary.c_str(),AVIO_FLAG_WRITE),"Open output");
    s.check(avformat_write_header(s.mux,nullptr),"Write container header");s.header=true;
  } catch(const std::exception &e){s.problem=e.what();}
}
FFmpegWriter::~FFmpegWriter()=default;
video_format *FFmpegWriter::format(){return &state->format;}
bool FFmpegWriter::ok() const{return state->problem.empty()&&state->header;}
const char *FFmpegWriter::error() const{return state->problem.c_str();}
int FFmpegWriter::stereoLayout() const{return state->stereo;}
void FFmpegWriter::encodeVideoFrame(LazyFrame *frame) {
  auto &s=*state;if(!ok())return;
  try {
    s.check(av_frame_make_writable(s.pixels),"Make conversion frame writable");
    if(frame){frame->set_target(&s.rgba);auto input=frame->target();const uint8_t *planes[4]={input->planes[0],nullptr,nullptr,nullptr};int strides[4]={input->strides[0],0,0,0};sws_scale(s.scale,planes,strides,0,s.format.h,s.pixels->data,s.pixels->linesize);}
    else { // Timeline gaps are opaque black, including video-only projects.
      if(s.pixels->format==AV_PIX_FMT_NV12){for(int y=0;y<s.format.h;++y)std::memset(s.pixels->data[0]+y*s.pixels->linesize[0],16,s.format.w);for(int y=0;y<s.format.h/2;++y)std::memset(s.pixels->data[1]+y*s.pixels->linesize[1],128,s.format.w);}
      else throw std::runtime_error("Timeline returned no composed output frame");
    }
    s.pixels->pts=s.videoPts++;s.pixels->sample_aspect_ratio=s.video->sample_aspect_ratio;
    if(s.hardware){AVFrame *gpu=av_frame_alloc();if(!gpu)throw std::bad_alloc();int status=av_hwframe_get_buffer(s.pool,gpu,0);if(status>=0)status=av_hwframe_transfer_data(gpu,s.pixels,0);if(status<0){av_frame_free(&gpu);s.check(status,"Upload composed QSV frame");}av_frame_copy_props(gpu,s.pixels);try{s.packets(s.video,s.vs,gpu);}catch(...){av_frame_free(&gpu);throw;}av_frame_free(&gpu);}
    else s.packets(s.video,s.vs,s.pixels);
  }catch(const std::exception &e){s.problem=e.what();}
}
void FFmpegWriter::encodeAudioFrame(float *buffer,int frames){if(!ok()||!state->audio)return;try{state->convertedAudio(buffer,frames);}catch(const std::exception &e){state->problem=e.what();}}
bool FFmpegWriter::finish(){
  auto &s=*state;if(s.committed)return true;if(!ok())return false;
  try{if(s.audio){s.convertedAudio(nullptr,0);s.audioFrames(true);s.packets(s.audio,s.as,nullptr);}s.packets(s.video,s.vs,nullptr);s.check(av_write_trailer(s.mux),"Finish output");s.check(avio_closep(&s.mux->pb),"Close output");if(std::rename(s.temporary.c_str(),s.destination.c_str())!=0)throw std::runtime_error("Cannot publish completed output");s.committed=true;s.temporary.clear();return true;}catch(const std::exception &e){s.problem=e.what();return false;}
}
void FFmpegWriter::cancel(){state->problem="Rendering cancelled";}
}
