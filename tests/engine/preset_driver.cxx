// SPDX-License-Identifier: GPL-2.0-or-later
#include "RenderDialog/EncodingPreset.H"
#include "media/EncoderRegistry.H"
#include "media/FFmpegWriter.H"
#include "globals.H"
#include "LazyFrame.H"
#include <FL/Fl_Return_Button.H>
#include <tinyxml.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
namespace nle {
bool g_PREVENT_OFFSCREEN_CRASH = true, g_SEEKING = false, g_INTERLACING = false;
lqt_codec_info_t** g_audio_codec_info;
lqt_codec_info_t** g_video_codec_info;
NleUI* g_ui = nullptr;
char* g_homefolder = nullptr;
}
static void select(Fl_Choice* menu,const char* name) {
    for(int i=0;i<menu->size();++i) {
        auto info=static_cast<lqt_codec_info_t*>(menu->menu()[i].user_data());
        if(info && !std::strcmp(info->name,name)) {
            menu->value(i);menu->do_callback();return;
        }
    }
    throw std::runtime_error(std::string("Missing codec: ")+name);
}
static void accept(CustomFormatDialog& dialog) {
    for(int i=0;i<dialog.dialog_window->children();++i)
        if(auto save=dynamic_cast<Fl_Return_Button*>(dialog.dialog_window->child(i)))save->do_callback();
}
int main(int argc,char** argv) {
    assert(argc==2);
    nle::g_audio_codec_info=lqt_query_registry(1,0,1,0);
    nle::g_video_codec_info=lqt_query_registry(0,1,1,0);
    nle::extend_encoder_registry();
    CustomFormatDialog dialog;
    dialog.selectContainer(4);
    assert(std::string(dialog.lqt_container_menu->text()).find("WebM")!=std::string::npos);
    for(int i=0;i<dialog.video_codec_menu->size();++i) {
        auto info=static_cast<lqt_codec_info_t*>(dialog.video_codec_menu->menu()[i].user_data());
        assert(!info || std::string(info->name)!="libx264");
    }
    select(dialog.video_codec_menu,"libvpx-vp9");
    select(dialog.audio_codec_menu,"none");
    assert(!dialog.audio_bitrate->active());
    dialog.frame_size_w->value(64);dialog.frame_size_h->value(48);dialog.name->value("Silent WebM fixture");
    accept(dialog);
    std::unique_ptr<nle::EncodingPreset> selected(dialog.getEncodingPreset());assert(selected);
    assert(selected->container()=="webm");
    nle::video_format format{};selected->getFormat(&format);
    assert(std::string(format.audio_codec)=="none");
    nle::ParameterValue quality(31);selected->setVideoParameter("ome_quality",quality);
    TiXmlElement xml("preset");selected->writeXML(&xml);
    nle::EncodingPreset restored;restored.readXML(&xml);
    assert(restored.container()=="webm");assert(restored.videoInt("ome_quality",0)==31);
    restored.getFormat(&format);assert(std::string(format.audio_codec)=="none");
    dialog.setEncodingPreset(&restored);
    assert(dialog.lqt_container_menu->value()==4);
    assert(std::string(static_cast<lqt_codec_info_t*>(dialog.audio_codec_menu->mvalue()->user_data())->name)=="none");
    assert(!dialog.audio_bitrate->active());
    // Changing container rebuilds menus and synchronizes their codec models.
    dialog.selectContainer(2);select(dialog.video_codec_menu,"libx264");
    dialog.selectContainer(4);
    assert(std::string(static_cast<lqt_codec_info_t*>(dialog.video_codec_menu->mvalue()->user_data())->name)!="libx264");
    for(const char* audio : {"none","libopus"}) {
        restored.getFormat(&format);std::strcpy(format.audio_codec,audio);restored.setFormat(&format);
        restored.setAudioCodec(nle::encoding_codec(audio,true));
        std::string file=std::string(argv[1])+"/"+audio+".webm";
        nle::FFmpegWriter writer(&restored,file.c_str());
        if(!writer.ok())throw std::runtime_error(writer.error());
        nle::LazyFrame frame(64,48);
        auto pixels=gavl_video_frame_create(frame.format());
        for(int y=0;y<48;++y)for(int x=0;x<64;++x) {
            auto p=pixels->planes[0]+y*pixels->strides[0]+x*4;p[0]=220;p[1]=20;p[2]=10;p[3]=255;
        }
        frame.put_data(pixels);
        float silence[2000*2]{};
        for(int i=0;i<25;++i){writer.encodeVideoFrame(&frame);writer.encodeAudioFrame(silence,1920);}
        if(!writer.finish())throw std::runtime_error(writer.error());
        gavl_video_frame_destroy(pixels);
    }
    std::cout<<"PASS WebM codec filtering, no-audio controls, preset copy/XML/reopen and exports\n";
}
