// SPDX-License-Identifier: GPL-2.0-or-later
#include "EditorIntegration.H"
#include "appearance.h"
#include "nle.h"
#include "Timeline.H"
#include "VideoClip.H"
#include "IVideoFile.H"
#include "DocManager.H"
#include "Command.H"
#include "AddCommand.H"
#include "TimelineView.H"
#include "VideoViewGL.H"
#include "IPlaybackCore.H"
#include "LoadSaveManager/LoadSaveManager.H"
#include "Renderer.H"
#include "ProgressDialog/ProgressDialog.h"
#include "RenderDialog/EncodingPreset.H"
#include "media/EncoderRegistry.H"
#include "media/FFmpegWriter.H"
#include <FL/Fl_Input_.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_Return_Button.H>
#include <filesystem>
#include <fcntl.h>
#include <signal.h>
#include <chrono>
#include <map>
namespace nle {
Fl_Font editor_font(){return dx::dome()?dx::font:FL_HELVETICA;}
int editor_font_size(int fallback){return dx::dome()?dx::size:fallback;}
bool edit_focus(const char *operation) {
  std::string action=operation;
  if(auto input=dynamic_cast<Fl_Input_*>(Fl::focus())) {
    if(action=="copy")input->copy(1);
    else if(action=="all")input->insert_position(input->size(),0);
    else if(!input->readonly()) {
      if(action=="cut"){input->copy(1);input->cut();}
      else if(action=="paste")Fl::paste(*input,1);
      else if(action=="undo")input->undo();
      else if(action=="redo")input->redo();
      else if(action=="delete")input->cut();
    }
    return true;
  }
  if(auto editor=dynamic_cast<Fl_Text_Editor*>(Fl::focus())) {
    if(action=="copy")Fl_Text_Editor::kf_copy(0,editor);
    else if(action=="cut")Fl_Text_Editor::kf_cut(0,editor);
    else if(action=="paste")Fl_Text_Editor::kf_paste(0,editor);
    else if(action=="all")Fl_Text_Editor::kf_select_all(0,editor);
    else if(action=="undo")Fl_Text_Editor::kf_undo(0,editor);
    else if(action=="redo")Fl_Text_Editor::kf_redo(0,editor);
    return true;
  }
  return false;
}
static NleUI *ui=nullptr;
static Fl_Menu_Bar *bar=nullptr;

static std::string choose(int type,const char *title,const char *filter=nullptr) {
  Fl_Native_File_Chooser dialog;dialog.type(type);dialog.title(title);if(filter)dialog.filter(filter);
  if(type==Fl_Native_File_Chooser::BROWSE_SAVE_FILE)dialog.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
  if(dialog.show())return {};return dialog.filename();
}
static void menuState(void *) {
  bool text=dynamic_cast<Fl_Input_*>(Fl::focus())||dynamic_cast<Fl_Text_Editor*>(Fl::focus());
  auto input=dynamic_cast<Fl_Input_*>(Fl::focus());
  if(g_docManager&&ui) {
    auto set=[](Fl_Menu_Item *m,bool value){if(value)m->activate();else m->deactivate();};
    set(ui->undo_item,text?(input?input->can_undo():true):g_docManager->canUndo());
    set(ui->redo_item,text?(input?input->can_redo():true):g_docManager->canRedo());
    if(text){set(ui->cut_item,!input||!input->readonly());set(ui->copy_item,true);set(ui->paste_item,!input||!input->readonly());set(ui->delete_item,!input||!input->readonly());}
    if(!text)g_timelineView->setSelectionButtons();
    bool modal=Fl::modal()&&Fl::modal()!=ui->mainWindow;
    if(modal&&!text)for(auto item:{ui->undo_item,ui->redo_item,ui->cut_item,ui->copy_item,ui->paste_item,ui->delete_item})item->deactivate();
    if(bar)for(int i=0;i<bar->size();++i) {
      auto item=const_cast<Fl_Menu_Item*>(bar->menu()+i);if(!item->label())continue;
      std::string label=item->label();
      if(label=="New Project"||label=="Open..."||label=="Save"||label=="Save as..."||label=="Render..."||label=="Import media..."||label=="Convert 2D to SBS (Arc)..."||label=="View edited timeline in mpvr / XR...")set(item,!modal&&!g_playbackCore->active());
      if(label=="Selected clip stereo...")set(item,!modal&&g_timelineView->selectedVideoClip());
      if(item->callback()==editor_menu) {
        auto action=reinterpret_cast<intptr_t>(item->user_data());
        if(action!=4 && action!=3 && modal)item->deactivate();
        else if(action==5 || action>=10&&action<=15)set(item,!modal);
        if(action>=10&&action<=15){if(g_timeline->stereoPreview==action-10)item->set();else item->clear();}
      }
    }
  }
  Fl::repeat_timeout(.1,menuState);
}
class ClipStereoCommand : public Command {
  int track,clip,oldLayout,oldSwap,newLayout,newSwap;
  void apply(int layout,int swap){auto v=dynamic_cast<VideoClip*>(g_timeline->getClip(track,clip));if(v)v->file()->configureStereo(layout,swap);}
public:
  ClipStereoCommand(VideoClip *v,int layout,int swap):track(v->track()->num()),clip(v->id()),oldLayout(v->file()->stereoOverride()),oldSwap(v->file()->eyeOrderOverride()),newLayout(layout),newSwap(swap){}
  void doo(){apply(newLayout,newSwap);}void undo(){apply(oldLayout,oldSwap);}
};
static void stereoSettings(bool clip) {
  auto selected=g_timelineView->selectedVideoClip();if(clip&&!selected)return;
  Fl_Double_Window dialog(480,230,clip?"Clip stereo interpretation":"Project stereo settings");
  Fl_Choice input(190,25,260,28,"Input layout");input.add("Auto (metadata and name)|Mono|Full SBS|Half SBS");
  Fl_Choice order(190,75,260,28,"Input eye order");order.add("Auto|Left then right|Right then left");
  Fl_Choice output(190,125,260,28,"Export layout");output.add("Left-eye 2D|Full SBS|Half SBS");
  Fl_Button cancel(190,180,120,30,"Cancel");Fl_Return_Button apply(330,180,120,30,"Apply");
  int layout=clip?selected->file()->stereoOverride():g_timeline->stereoInput;
  int swap=clip?selected->file()->eyeOrderOverride():g_timeline->stereoSwap;
  input.value(layout+1);order.value(swap+1);output.value(g_timeline->stereoOutput);if(clip)output.deactivate();
  bool accepted=false;apply.callback([](Fl_Widget *w,void *v){*static_cast<bool*>(v)=true;w->window()->hide();},&accepted);
  cancel.callback([](Fl_Widget *w,void*){w->window()->hide();});dialog.end();dialog.set_modal();if(dx::dome())dx::style(&dialog);dialog.show();
  while(dialog.shown())Fl::wait();if(!accepted)return;
  if(clip)submit(new ClipStereoCommand(selected,input.value()-1,order.value()-1));
  else {g_timeline->stereoInput=input.value()-1;g_timeline->stereoSwap=order.value()-1;g_timeline->stereoOutput=output.value();}
  g_timeline->changing();g_videoView->redraw();g_timelineView->redraw();
}
static void importMedia(const std::string &path,int layout=-1) {
  Track *target=nullptr;
  for(auto t=g_timeline->getTracks();t;t=t->next)if(t->track->type()==TRACK_TYPE_VIDEO){target=t->track;break;}
  if(!target){g_timelineView->add_track(TRACK_TYPE_VIDEO);for(auto t=g_timeline->getTracks();t;t=t->next)if(t->track->type()==TRACK_TYPE_VIDEO){target=t->track;break;}}
  if(!target)throw std::runtime_error("Create a video track before importing media");
  submit(new AddCommand(path,target,g_timelineView->stylus()));
  if(layout>=0){auto clip=dynamic_cast<VideoClip*>(target->find(g_timelineView->stylus()));if(clip)submit(new ClipStereoCommand(clip,layout,-1));}
  g_timeline->changing();g_timelineView->adjustScrollbar();g_timelineView->redraw();g_videoView->redraw();
}
class ConversionJob : public Fl_Double_Window {
  Fl_Multiline_Output log{15,15,670,260};
  Fl_Button cancel{565,290,120,30,"Cancel"};
  pid_t pid=-1;int fd=-1;bool stopping=false;std::chrono::steady_clock::time_point stopped;
  std::string text,output;int layout=2;
  static void tick(void *v){auto self=static_cast<ConversionJob*>(v);self->pump();if(self->pid>0)Fl::repeat_timeout(.1,tick,v);}
  void pump(){char chunk[4096];ssize_t n;while((n=read(fd,chunk,sizeof(chunk)))>0){text.append(chunk,n);if(text.size()>24000)text.erase(0,text.size()-20000);log.value(text.c_str());}
    if(stopping&&std::chrono::steady_clock::now()-stopped>std::chrono::seconds(4))kill(-pid,SIGKILL);
    int status=0;if(waitpid(pid,&status,WNOHANG)==pid){close(fd);fd=-1;pid=-1;cancel.label("Close");
      if(WIFEXITED(status)&&WEXITSTATUS(status)==0&&!stopping){try{importMedia(output,layout);text+="\nConverted SBS imported into the timeline.\n";}catch(const std::exception &e){text+=e.what();}}
      else text+="\nConversion failed or was cancelled.\n";log.value(text.c_str());}}
  void stop(){if(pid>0&&!stopping){kill(-pid,SIGTERM);stopping=true;stopped=std::chrono::steady_clock::now();}else if(pid<0)hide();}
public:
  ConversionJob(const std::string &source,const std::string &target,int inputLayout):Fl_Double_Window(700,335,"Arc SBS conversion"),output(target),layout(inputLayout){
    end();cancel.callback([](Fl_Widget*,void *v){static_cast<ConversionJob*>(v)->stop();},this);callback([](Fl_Widget*,void *v){static_cast<ConversionJob*>(v)->stop();},this);
    std::vector<std::string> command={"arc-sbs","convert",source,target,"--layout",layout==1?"full":"half"};std::vector<char*> args;for(auto &s:command)args.push_back(s.data());args.push_back(nullptr);
    int pipes[2];if(pipe(pipes))throw std::runtime_error("Cannot create converter pipe");
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_adddup2(&actions,pipes[1],1);posix_spawn_file_actions_adddup2(&actions,pipes[1],2);posix_spawn_file_actions_addclose(&actions,pipes[0]);posix_spawn_file_actions_addclose(&actions,pipes[1]);
    posix_spawnattr_t attributes;posix_spawnattr_init(&attributes);posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attributes,0);
    int result=posix_spawnp(&pid,"arc-sbs",&actions,&attributes,args.data(),environ);posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attributes);close(pipes[1]);
    if(result){close(pipes[0]);pid=-1;throw std::runtime_error("Cannot start the installed arc-sbs command");}
    fd=pipes[0];fcntl(fd,F_SETFL,O_NONBLOCK);fcntl(fd,F_SETFD,FD_CLOEXEC);if(dx::dome())dx::style(this);Fl::add_timeout(.1,tick,this);
  }
  ~ConversionJob(){Fl::remove_timeout(tick,this);if(pid>0){kill(-pid,SIGKILL);waitpid(pid,nullptr,0);}if(fd>=0)close(fd);}
};
static std::unique_ptr<ConversionJob> conversion;
static void convertMedia(){
  if(conversion&&conversion->shown()){conversion->show();return;}
  auto input=choose(Fl_Native_File_Chooser::BROWSE_FILE,"Select 2D source");if(input.empty())return;
  auto output=choose(Fl_Native_File_Chooser::BROWSE_SAVE_FILE,"Save converted SBS video","Video\t*.{mp4,mkv}");if(output.empty())return;
  int layout=g_timeline->stereoOutput==1?1:2;
  conversion.reset(new ConversionJob(input,output,layout));conversion->show();
}
static void previewTimeline(){
  g_videoView->stop();
  auto cache=std::filesystem::path(getenv("XDG_CACHE_HOME")?getenv("XDG_CACHE_HOME"):(std::string(getenv("HOME"))+"/.cache"))/"openmovieeditor/previews";
  std::filesystem::create_directories(cache);
  char unique[]="preview-XXXXXX";std::string pattern=(cache/unique).string();std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
  int fd=mkstemp(name.data());if(fd<0)throw std::runtime_error("Cannot create preview path");close(fd);unlink(name.data());std::string path=std::string(name.data())+".mkv";
  EncodingPreset preset;video_format format{};preset.getFormat(&format);format.w=1280;format.h=720;format.samplerate=48000;format.pixel_aspect_ratio=1;format.interlacing=0;format.framerate.timescale=24;format.framerate.frame_duration=1;
  snprintf(format.video_codec,sizeof(format.video_codec),"h264_qsv");snprintf(format.audio_codec,sizeof(format.audio_codec),"aac");preset.setFormat(&format);preset.setVideoCodec(encoding_codec("h264_qsv"));preset.setAudioCodec(encoding_codec("aac",true));preset.stereoLayout(g_timeline->stereoOutput);
  ProgressDialog progress("Rendering edited timeline for mpvr");Renderer renderer(preset.getFileWriter(path.c_str()));
  if(!renderer.ok())throw std::runtime_error(renderer.error());renderer.go(&progress);if(!renderer.ok())throw std::runtime_error(renderer.error());
  std::string layout=g_timeline->stereoOutput==1?"full-sbs":g_timeline->stereoOutput==2?"half-sbs":"mono";
  if(dx::spawn({"mpvr","--hwdec=qsv-copy","--sbs-layout="+layout,"--",path})<0)throw std::runtime_error("Cannot start mpvr");
}
void editor_menu(Fl_Widget*,void *data) {
  intptr_t action=reinterpret_cast<intptr_t>(data);
  if(Fl::modal() && Fl::modal()!=ui->mainWindow && action!=4 && action!=3)return;
  try {
    if(action==1){auto path=choose(Fl_Native_File_Chooser::BROWSE_FILE,"Open Movie Editor project","OME projects\t*.vproj");if(!path.empty())g_loadSaveManager->openFile(path);}
    else if(action==2)g_loadSaveManager->save();
    else if(action==3){if(!edit_focus("all"))g_timelineView->selectAll();}
    else if(action==4){if(auto modal=Fl::modal())modal->do_callback();else {g_videoView->stop();g_loadSaveManager->shutdown();ui->mainWindow->hide();}}
    else if(action==5)stereoSettings(false);
    else if(action==6)stereoSettings(true);
    else if(action>=10&&action<=15){g_timeline->stereoPreview=action-10;g_timeline->changing();g_videoView->redraw();}
    else if(action==20)convertMedia();
    else if(action==21)previewTimeline();
    else if(action==22){auto path=choose(Fl_Native_File_Chooser::BROWSE_FILE,"Import media");if(!path.empty())importMedia(path);}
  }catch(const std::exception &e){fl_alert("%s",e.what());}
}
static Fl_Event_Dispatch previousDispatch=nullptr;
static int editorDispatch(int event,Fl_Window *window){
  if(event==FL_KEYDOWN && Fl::event_key()=='w' && (Fl::event_state()&(FL_CTRL|FL_ALT|FL_META))==FL_CTRL){editor_menu(nullptr,(void*)4);return 1;}
  return previousDispatch?previousDispatch(event,window):Fl::handle_(event,window);
}
void install_editor_integration(NleUI &editor){ui=&editor;previousDispatch=Fl::event_dispatch();Fl::event_dispatch(editorDispatch);for(int i=0;i<ui->mainWindow->children();++i)if(auto menu=dynamic_cast<Fl_Menu_Bar*>(ui->mainWindow->child(i))){bar=menu;break;}Fl::add_timeout(.1,menuState);}
void shutdown_editor_integration(){if(Fl::event_dispatch()==editorDispatch)Fl::event_dispatch(previousDispatch);if(g_videoView)g_videoView->resetPreview();Fl::remove_timeout(menuState);conversion.reset();ui=nullptr;bar=nullptr;}
}
