// SPDX-License-Identifier: GPL-2.0-or-later
#include "EngineCLI.H"
#include "nle.h"
#include "EncoderRegistry.H"
#include "Timeline.H"
#include "Renderer.H"
#include "RenderDialog/EncodingPreset.H"
#include "ProgressDialog/IProgressListener.H"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
namespace nle {
class ConsoleProgress : public IProgressListener {
public:
  int limit=-1,count=0;
  void start(){}void end(){}
  bool progress(double value){++count;return limit>0&&count>=limit;}
};
int engine_command(int argc,char **argv) {
  if(argc<2)return -1;
  std::string command=argv[1];
  if(command!="--render"&&command!="--roundtrip"&&command!="--export-srt"&&command!="--export-smil")return -1;
  try {
    if(argc<4)throw std::runtime_error("Usage: openmovieeditor --render project.vproj output.mkv [--encoder av1_qsv] [--size 1280x720] [--fps 24] [--sbs mono|full|half]");
    std::string input=argv[2],output=argv[3],encoder="av1_qsv",fps="24",audio="aac";
    int width=1280,height=720,layout=-1,quality=23,bitrate=8000000,cancel=-1,track=0;
    for(int i=4;i<argc;++i) {
      std::string key=argv[i];if(i+1>=argc)throw std::runtime_error("Option requires a value: "+key);
      std::string val=argv[++i];
      if(key=="--track")track=std::stoi(val);
      else if(key=="--encoder")encoder=val;
      else if(key=="--audio")audio=val;
      else if(key=="--size"){if(std::sscanf(val.c_str(),"%dx%d",&width,&height)!=2)throw std::runtime_error("Invalid output size");}
      else if(key=="--fps")fps=val;
      else if(key=="--sbs"){layout=val=="mono"?0:val=="full"?1:val=="half"?2:-1;if(layout<0)throw std::runtime_error("Invalid SBS layout");}
      else if(key=="--quality")quality=std::stoi(val);
      else if(key=="--bitrate")bitrate=std::stoi(val);
      else if(key=="--cancel-after")cancel=std::stoi(val);
      else if(key=="--device")setenv("OME_QSV_DEVICE",val.c_str(),1);
      else throw std::runtime_error("Unknown option: "+key);
    }
    g_ui->mainWindow->show();Fl::check();g_PREVENT_OFFSCREEN_CRASH=false;
    g_timeline->render_mode(true);
    if(!g_timeline->read(input))throw std::runtime_error("Cannot read the Open Movie Editor project");
    if(command=="--export-srt"||command=="--export-smil"){
      int good=command=="--export-srt"?g_timeline->write_srt(output,track):g_timeline->write_smil(output,track);
      if(!good)throw std::runtime_error("Cannot export the selected track");return 0;
    }
    if(command=="--roundtrip") {
      if(!g_timeline->write(output,"Round trip"))throw std::runtime_error("Cannot save project");
      std::cout<<nlohmann::json({{"ok",true},{"engine","original"},{"output",output},{"duration_ticks",g_timeline->durationTicks()}}).dump()<<std::endl;return 0;
    }
    EncodingPreset preset;video_format format{};preset.getFormat(&format);
    format.w=width;format.h=height;format.pixel_aspect_ratio=1;format.interlacing=0;format.samplerate=48000;
    size_t slash=fps.find('/');int num=0,den=1;
    if(slash==std::string::npos)num=std::stoi(fps);else{num=std::stoi(fps.substr(0,slash));den=std::stoi(fps.substr(slash+1));}
    if(num<=0||den<=0||double(num)/den>240)throw std::runtime_error("Invalid frame rate");
    format.framerate.timescale=num;format.framerate.frame_duration=den;
    std::snprintf(format.video_codec,sizeof(format.video_codec),"%s",encoder.c_str());std::snprintf(format.audio_codec,sizeof(format.audio_codec),"%s",audio.c_str());
    auto vc=encoding_codec(encoder.c_str());auto ac=encoding_codec(audio.c_str(),true);
    if(!vc||!ac)throw std::runtime_error("Encoder is unavailable");
    preset.setFormat(&format);preset.setVideoCodec(vc);preset.setAudioCodec(ac);preset.stereoLayout(layout<0?g_timeline->stereoOutput:layout);
    ParameterValue q(quality),b(bitrate);preset.setVideoParameter("ome_quality",q);preset.setVideoParameter("ff_bit_rate_video",b);
    ConsoleProgress progress;progress.limit=cancel;
    Renderer renderer(preset.getFileWriter(output.c_str()));if(!renderer.ok())throw std::runtime_error(renderer.error());
    renderer.go(&progress);if(!renderer.ok())throw std::runtime_error(renderer.error());
    std::cout<<nlohmann::json({{"ok",true},{"engine","original"},{"output",output},{"encoder",encoder},{"frames",progress.count},{"stereo_layout",preset.stereoLayout()}}).dump()<<std::endl;return 0;
  }catch(const std::exception &e){std::cerr<<nlohmann::json({{"ok",false},{"engine","original"},{"error",e.what()}}).dump()<<std::endl;return 1;}
}
}
