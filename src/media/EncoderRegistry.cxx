// SPDX-License-Identifier: GPL-2.0-or-later
#include "EncoderRegistry.H"
#include "globals.H"
#include <cstring>
#include <deque>
#include <vector>
#include <map>
extern "C" {
#include <libavcodec/avcodec.h>
}
namespace nle {
static std::deque<lqt_codec_info_t> descriptions;
static std::deque<std::vector<lqt_parameter_info_t>> parameters;
static std::map<std::string,std::string> videoNames,audioNames;
static std::vector<lqt_codec_info_t*> videos,audios;
static char *text(const char *s){return const_cast<char*>(s);}
static lqt_parameter_info_t integer(const char *name,const char *label,int value,int lo,int hi) {
  lqt_parameter_info_t p{};p.name=text(name);p.real_name=text(label);p.type=LQT_PARAMETER_INT;
  p.val_default.val_int=value;p.val_min.val_int=lo;p.val_max.val_int=hi;return p;
}
static void add(const char *name,const char *encoder,const char *label,bool audio) {
  const bool silent = audio && std::strcmp(name,"none")==0;
  if(!silent && !avcodec_find_encoder_by_name(encoder))return;
  auto &names=audio?audioNames:videoNames;names[name]=encoder;
  // Keep any existing libquicktime descriptor with the same legacy name.
  auto &list=audio?audios:videos;
  for(auto item:list)if(!std::strcmp(item->name,name))return;
  parameters.emplace_back();auto &params=parameters.back();
  if(silent) {}
  else if(audio)params.push_back(integer("bit_rate_audio","Audio bitrate (bits/s)",192000,8000,1536000));
  else {
    params.push_back(integer("ff_bit_rate_video","Video bitrate (bits/s; VP9 QSV)",8000000,64000,200000000));
    params.push_back(integer("ome_quality","Quality / CRF (lower is higher quality)",23,1,51));
    params.push_back(integer("ome_gop","Maximum keyframe interval",120,1,1000));
    lqt_parameter_info_t p{};p.name=text("ome_qsv_device");p.real_name=text("Intel QSV render node");p.type=LQT_PARAMETER_STRING;
    p.val_default.val_string=text("/dev/dri/by-path/pci-0000:03:00.0-render");params.push_back(p);
  }
  descriptions.emplace_back();auto &info=descriptions.back();info={};
  info.name=text(name);info.long_name=text(label);info.description=text("Native FFmpeg encoder in the Open Movie Editor render pipeline");
  info.type=audio?LQT_CODEC_AUDIO:LQT_CODEC_VIDEO;info.direction=LQT_DIRECTION_ENCODE;info.compatibility_flags=0x00FFFFFF;
  info.num_encoding_parameters=params.size();info.encoding_parameters=params.data();
  list.push_back(&info);
}
void extend_encoder_registry() {
  for(auto p=g_video_codec_info;p&&*p;++p)videos.push_back(*p);
  for(auto p=g_audio_codec_info;p&&*p;++p)audios.push_back(*p);
  add("av1_qsv","av1_qsv","AV1 — Intel QSV",false);
  add("h264_qsv","h264_qsv","H.264 — Intel QSV",false);
  add("hevc_qsv","hevc_qsv","H.265 (HEVC) — Intel QSV",false);
  add("vp9_qsv","vp9_qsv","VP9 — Intel QSV (bitrate mode)",false);
  add("libx264","libx264","H.264 — FFmpeg software",false);
  add("libvpx-vp9","libvpx-vp9","VP9 — FFmpeg software",false);
  add("ffv1","ffv1","FFV1 — lossless",false);
  add("ffmpeg_mpg4","mpeg4","MPEG-4 — legacy preset via FFmpeg",false);
  add("ffmpeg_msmpeg4v3","msmpeg4","MS MPEG-4 v3 — legacy preset via FFmpeg",false);
  add("x264","libx264","H.264 — legacy x264 preset via FFmpeg",false);
  add("aac","aac","AAC — FFmpeg",true);
  add("faac","aac","AAC — legacy preset via FFmpeg",true);
  add("pcm_s16le","pcm_s16le","PCM 16-bit — uncompressed",true);
  add("flac","flac","FLAC — lossless",true);
  add("libopus","libopus","Opus — FFmpeg",true);
  add("libvorbis","libvorbis","Vorbis — FFmpeg",true);
  add("none","","No audio",true);
  videos.push_back(nullptr);audios.push_back(nullptr);
  g_video_codec_info=videos.data();g_audio_codec_info=audios.data();
}
lqt_codec_info_t *encoding_codec(const char *name,bool audio) {
  if(!name)return nullptr;
  for(auto p=audio?g_audio_codec_info:g_video_codec_info;p&&*p;++p)if(!std::strcmp((*p)->name,name))return *p;
  return nullptr;
}
lqt_codec_info_t **find_encoding_codec(const char *name,bool audio) {return new lqt_codec_info_t*[2]{encoding_codec(name,audio),nullptr};}
void free_encoding_query(lqt_codec_info_t **info){delete[]info;}
bool ffmpeg_video_encoder(const char *name){return name&&videoNames.count(name);}
std::string video_encoder_name(const char *name){auto it=videoNames.find(name?name:"");return it==videoNames.end()?std::string(name?name:""):it->second;}
std::string audio_encoder_name(const char *name){auto it=audioNames.find(name?name:"");if(it!=audioNames.end())return it->second;if(name&&std::string(name)=="lame")return "libmp3lame";if(name&&std::string(name)=="twos")return "pcm_s16be";return name?name:"aac";}
}
