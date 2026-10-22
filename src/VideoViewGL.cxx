/*  VideoViewGL.cxx
 *
 *  Copyright (C) 2005 Richard Spindler <richard.spindler AT gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <iostream>

#include <FL/gl.h>
#include <FL/Fl_PNG_Image.H>

#include "VideoViewGL.H"
#include "globals.H"
#include "SwitchBoard.H"
#include "IPlaybackCore.H"
#include "Timeline.H"
#include "render_helper.H"
#include "int_float_scale.H"
#include "LazyFrame.H"
#include "media/Stereo.H"
#include "VideoClip.H"
#include "IVideoFile.H"
#include <array>



extern bool g_16_9;
extern bool g_black_borders;
extern bool g_black_borders_2_35;

using namespace std;

namespace nle
{


VideoViewGL* g_videoView=nullptr;
VideoViewGL::VideoViewGL(int x,int y,int w,int h,const char *l):Fl_Gl_Window(x,y,w,h,l),m_seekPosition(0),m_zoom(0),m_once(true){g_videoView=this;}
VideoViewGL::~VideoViewGL(){resetPreview();if(m_texture&&shown()){make_current();glDeleteTextures(1,&m_texture);}}
void VideoViewGL::resetPreview(){m_stereo.reset();m_snapshot.clear();}
void VideoViewGL::drawVideoBorder()
{
	//int w_ = 1024;
	int w_ = g_16_9 ? 1024 : 768;
	int h_ = 576;
	float gl_x, gl_y, gl_w, gl_h;
	{
		float f_v = ( (float)w_ / (float)h_ );
		float f_w = ( (float)w() / (float)h() );
		float f_g = f_v / f_w;
		if ( f_g > 1.0 ) {
			gl_h = 10.0 / f_g;
			gl_w = 10.0;
		} else {
			gl_h = 10.0;
			gl_w = f_g * 10.0;
		}
		gl_x = ( 10.0 - gl_w ) / 2;
		gl_y = ( 10.0 - gl_h ) / 2;

	}

	glDisable (GL_TEXTURE_2D);
	if ( (g_black_borders||g_black_borders_2_35) && !g_16_9 ) {
		int w_ = 768;
		//int w_ = 1024;
		int h_ = 576;
		float gl_x, gl_y, gl_w, gl_h;
		{
			float f_v = ( (float)w_ / (float)h_ );
			float f_w = ( (float)w() / (float)h() );
			float f_g = f_v / f_w;
			if ( f_g > 1.0 ) {
				gl_h = 10.0 / f_g;
				gl_w = 10.0;
			} else {
				gl_h = 10.0;
				gl_w = f_g * 10.0;
			}
			if ( g_black_borders_2_35 ) {
				gl_h = gl_h / ( ( 2.35 / 1.0 ) / ( 4.0 / 3.0 ) );
			} else {
				gl_h = gl_h / ( ( 16.0 / 9.0 ) / ( 4.0 / 3.0 ) );
			}
			gl_x = ( 10.0 - gl_w ) / 2;
			gl_y = ( 10.0 - gl_h ) / 2;

		}
		
		glBegin (GL_QUADS);

		glColor4f( 0.0f, 0.0f, 0.0f, 1.0f );

		glVertex3f( 0.0f, 0.0f, 0.0f );
		glVertex3f( gl_x + gl_w, 0.0f, 0.0f );
		glVertex3f( gl_x + gl_w, gl_y, 0.0f );
		glVertex3f( 0.0f, gl_y, 0.0f );
		
		glVertex3f( 0.0f, gl_y + gl_h, 0.0f );
		glVertex3f( gl_x + gl_w, gl_y + gl_h, 0.0f );
		glVertex3f( gl_x + gl_w, 10.0f, 0.0f );
		glVertex3f( 0.0f, 10.0f, 0.0f );


		glEnd();
	}

	glLineWidth(3);
	glBegin (GL_LINE_LOOP);
	glColor4f( 0.0f, 0.0f, 0.0f, 1.0f );
	
	glVertex3f   (  gl_x,      gl_y, 0.0 );
	glVertex3f   ( gl_x + gl_w,      gl_y, 0.0 );
	glVertex3f   ( gl_x + gl_w,     gl_y + gl_h, 0.0 );
	glVertex3f   (  gl_x,     gl_y + gl_h, 0.0 );

	glEnd();
	glLineWidth(1);
	glBegin (GL_LINE_LOOP);
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	glVertex3f   (  gl_x,      gl_y, 0.0 );
	glVertex3f   ( gl_x + gl_w,      gl_y, 0.0 );
	glVertex3f   ( gl_x + gl_w,     gl_y + gl_h, 0.0 );
	glVertex3f   (  gl_x,     gl_y + gl_h, 0.0 );

	glEnd();

	
	glEnable (GL_TEXTURE_2D);
	
/*
	float _x, _y, _w, _h;
	_x = gl_x + (gl_w / 8.0);
	_y = gl_y;
	_w = gl_w / 1.33333333333333333;
	_h = gl_h;
	glDisable (GL_TEXTURE_2D);
	glLineWidth(3);
	glBegin (GL_LINE_LOOP);
	glColor4f( 0.0f, 0.0f, 0.0f, 1.0f );
	
	glVertex3f   (  _x,      _y, 0.0 );
	glVertex3f   ( _x + _w,      _y, 0.0 );
	glVertex3f   ( _x + _w,     _y + _h, 0.0 );
	glVertex3f   (  _x,     _y + _h, 0.0 );

	glEnd();
	glLineWidth(1);
	glBegin (GL_LINE_LOOP);
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	glVertex3f   (  _x,      _y, 0.0 );
	glVertex3f   ( _x + _w,      _y, 0.0 );
	glVertex3f   ( _x + _w,     _y + _h, 0.0 );
	glVertex3f   (  _x,     _y + _h, 0.0 );

	glEnd();
	glEnable (GL_TEXTURE_2D);
	*/
}


void VideoViewGL::not_valid(){
  glMatrixMode(GL_PROJECTION);glLoadIdentity();
  double a=m_zoom<0?10*m_zoom:5*m_zoom,b=10-a;
  glViewport(0,0,pixel_w(),pixel_h());glOrtho(a,b,b,a,-20000,10000);
  glMatrixMode(GL_MODELVIEW);glLoadIdentity();
  glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_TEXTURE_2D);
}
void VideoViewGL::pushFrameStack(LazyFrame **fs,bool move_cursor){
  make_current();if(!valid())not_valid();glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  drawFrameStack(fs);swap_buffers();
}
void VideoViewGL::pushFrame(LazyFrame *f,bool move_cursor){LazyFrame *stack[]={f,nullptr};pushFrameStack(stack,move_cursor);}
void VideoViewGL::draw(){
  if(!valid())not_valid();
  if(m_once){opengl_system_information<<"OpenGL: "<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION);m_once=false;}
  if(g_playbackCore->active())return;
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  if(m_seekPosition>=0){g_SEEKING=true;auto fs=g_timeline->getFrameStack(m_seekPosition);g_SEEKING=false;drawFrameStack(fs);}
  g_PREVENT_OFFSCREEN_CRASH=false;
}
void VideoViewGL::seek(int64_t p){if(g_playbackCore->active())return;m_seekPosition=p;redraw();}
void VideoViewGL::play(){if(g_playbackCore->active())return;m_seekPosition=-1;g_timeline->sort();g_playbackCore->play();}
void VideoViewGL::stop(){g_playbackCore->stop();}
void VideoViewGL::drawFrameStack(LazyFrame **fs){
  int mode=g_timeline->stereoPreview;
  bool stereo=g_timeline->stereoInput>0;
  for(auto t=g_timeline->getTracks();t&&!stereo;t=t->next)
    for(auto c=t->track->getClips();c&&!stereo;c=c->next)
      if(auto clip=dynamic_cast<VideoClip*>(c->clip)){auto file=clip->file();int layout=file->stereoOverride()>=0?file->stereoOverride():g_timeline->stereoInput>=0?g_timeline->stereoInput:file->sourceStereoLayout();stereo=layout>0;}
  bool pair=mode==1||(mode==0&&stereo);
  int eyeWidth=720,eyeHeight=g_16_9?405:540;
  auto upload=[&](LazyFrame *f,double x,double y,double width,double height,float u0=0,float u1=1){
    auto format=f->format();auto rgba=f->RGBA();
    if(!m_texture){glGenTextures(1,&m_texture);}
    glBindTexture(GL_TEXTURE_2D,m_texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,rgba->strides[0]/4);
    if(m_textureWidth!=format->frame_width||m_textureHeight!=format->frame_height){
      m_textureWidth=format->frame_width;m_textureHeight=format->frame_height;
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,m_textureWidth,m_textureHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    }
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,format->frame_width,format->frame_height,GL_RGBA,GL_UNSIGNED_BYTE,rgba->planes[0]);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glColor4f(1,1,1,f->alpha());glBegin(GL_QUADS);
    glTexCoord2f(u0,0);glVertex2d(x,y);glTexCoord2f(u1,0);glVertex2d(x+width,y);
    glTexCoord2f(u1,1);glVertex2d(x+width,y+height);glTexCoord2f(u0,1);glVertex2d(x,y+height);glEnd();
  };
  auto rectangle=[&](double aspect){
    double factor=aspect/(double(w())/h());double width=factor>1?10:factor*10,height=factor>1?10/factor:10;
    return std::array<double,4>{(10-width)/2,(10-height)/2,width,height};
  };
  if(mode!=5&&(stereo||mode>0)){
    try{
      auto snapshot=g_timeline->snapshot();
      if(!m_stereo||snapshot!=m_snapshot||m_previewMode!=mode){m_stereo.reset(new StereoRender(g_timeline,eyeWidth*2,eyeHeight,1,1));m_snapshot=snapshot;m_previewMode=mode;}
      auto frame=m_stereo->frame(m_seekPosition<0?g_timeline->m_playPosition:m_seekPosition);
      auto r=rectangle(double(eyeWidth)/eyeHeight*(pair?2:1));
      if(mode==4){
        glColorMask(GL_TRUE,GL_FALSE,GL_FALSE,GL_TRUE);upload(frame,r[0],r[1],r[2],r[3],0,.5);
        glColorMask(GL_FALSE,GL_TRUE,GL_TRUE,GL_TRUE);upload(frame,r[0],r[1],r[2],r[3],.5,1);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
      }else upload(frame,r[0],r[1],r[2],r[3],pair?0:mode==3?.5:0,pair?1:mode==3?1:.5);
    }catch(const std::exception &e){std::cerr<<"Stereo preview: "<<e.what()<<std::endl;resetPreview();}
    return;
  }
  resetPreview();
  int count=0;while(fs&&fs[count])++count;
  for(int i=count-1;i>=0;--i){
    auto f=fs[i]->format();double par=f->pixel_height?double(f->pixel_width)/f->pixel_height:1;
    auto r=rectangle(double(f->image_width)/f->image_height*par);upload(fs[i],r[0],r[1],r[2],r[3]);
  }
  drawVideoBorder();
}
} // namespace nle
