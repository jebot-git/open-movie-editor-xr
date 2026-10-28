// SPDX-License-Identifier: GPL-2.0-or-later
#include "desktop-menu.h"
#include <FL/Fl.H>
#include <fstream>
#include <string>
#include <cstdio>
struct State { DesktopMenuBar* menu; DesktopMenu* bridge; const char* path; int clicks=0; };
static void record(void* data) {
    auto& state=*static_cast<State*>(data);
    std::string temp=std::string(state.path)+".tmp";
    { std::ofstream out(temp);out<<state.menu->h()<<' '<<state.clicks<<' '<<state.bridge->serviceName()<<'\n'; }
    std::rename(temp.c_str(),state.path);
    Fl::repeat_timeout(.05,record,data);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    Fl_Window window(300,100,"OME menu regression");
    DesktopMenuBar menu(0,0,300,25);
    window.end();
    DesktopMenu bridge(window,menu);
    State state{&menu,&bridge,argv[1]};
    menu.add("File/Action",FL_CTRL+'s',[](Fl_Widget*,void* data){++static_cast<State*>(data)->clicks;},&state);
    window.show();Fl::add_timeout(.05,record,&state);
    return Fl::run();
}
