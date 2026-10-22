/*  InkscapeClip.cxx
 *
 *  Copyright (C) 2007 Richard Spindler <richard.spindler AT gmail.com>
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

#include <FL/Fl_Shared_Image.H>
#include <FL/Fl.H>

#include "InkscapeClip.H"
#include "ErrorDialog/IErrorHandler.H"
#include "ImageClip.H"
#include "ImageClipArtist.H"
#include "helper.H"
#include "globals.H"
#include "timeline/Track.H"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "VideoViewGL.H"
#include <cassert>
#include "appearance.h"
#include <filesystem>
#include <cerrno>
#define BUFFER_LEN 1024
namespace nle
{

static std::string svgCache(const std::string &source){
  auto root=std::filesystem::path(getenv("XDG_CACHE_HOME")?getenv("XDG_CACHE_HOME"):(std::string(g_homefolder)+"/.cache"))/"openmovieeditor/svg";
  auto path=root/std::filesystem::absolute(source).relative_path();path+=".png";
  std::filesystem::create_directories(path.parent_path());return path.string();
}
static bool renderSvg(const std::string &source,const std::string &target){
  std::vector<std::string> command={"inkscape","--export-type=png","--export-filename="+target,"--",source};
  std::vector<char*> args;for(auto &s:command)args.push_back(s.data());args.push_back(nullptr);
  pid_t child;int result=posix_spawnp(&child,"inkscape",nullptr,nullptr,args.data(),environ);
  if(result)return false;int status;while(waitpid(child,&status,0)<0){if(errno!=EINTR)return false;}
  return WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static void detect_svg_update_callback( void* data );

InkscapeClip::InkscapeClip( Track* track, int64_t position, string filename, int64_t length, int id, ClipData* data )
	: FilterClip( track, position, id ), VideoEffectClip( this )
{
	"/usr/share/openmovieeditor/svg-titles/0000.svg";
	"720x576";
	"inkscape --export-png=123.png /usr/share/openmovieeditor/svg-titles/0002.svg";
	m_filename = filename;


  auto cachefile=svgCache(filename);
  if(!renderSvg(filename,cachefile))std::cerr<<"Cannot render SVG with Inkscape: "<<filename<<std::endl;

	if ( length > 0 ) {
		m_length = length;
	} else {
		m_length = NLE_TIME_BASE * 10;
	}
	m_image_clip = new ImageClip( track, position, cachefile, length, id );

	setEffects( data );
	if(!track->render_mode())Fl::add_timeout(1.0, detect_svg_update_callback, this );
}


InkscapeClip::~InkscapeClip()
{
	Fl::remove_timeout( detect_svg_update_callback, this );
	delete m_image_clip;
}
int64_t InkscapeClip::length()
{
	return m_length;
}

void InkscapeClip::doAction( int index )
{
  dx::spawn({"inkscape","--",m_filename});
}
static void detect_svg_update_callback( void* data ) {
	InkscapeClip* ic = (InkscapeClip*)data;
	ic->detectSvgUpdate();
	Fl::repeat_timeout(1.0, detect_svg_update_callback, data);
}
void InkscapeClip::detectSvgUpdate()
{
  auto cachefile=svgCache(m_filename);
  struct stat source{},cached{};
  if(stat(m_filename.c_str(),&source)<0)return;
  if(stat(cachefile.c_str(),&cached)<0||source.st_mtime>cached.st_mtime){
    if(!renderSvg(m_filename,cachefile))return;
    delete m_image_clip;m_image_clip=new ImageClip(track(),position(),cachefile,length(),m_id);g_videoView->redraw();
  }
}

LazyFrame* InkscapeClip::getRawFrame( int64_t position, int64_t &position_in_file )
{
	position_in_file = position - m_position;
	if ( position >= m_position && position <= m_position + m_length ) {
		return m_image_clip->getRawFrame( m_image_clip->position(), position_in_file );
	} else {
		return 0;
	}
}
int InkscapeClip::w()
{
	return m_image_clip->w();
}
int InkscapeClip::h()
{
	return m_image_clip->h();
}
int64_t InkscapeClip::trimA( int64_t trim )
{
	if ( m_length - trim < 0 ) {
		return 0;
	}
	if ( m_position + trim < 0 ) {
		trim = (-1) * m_position;
	}
	m_length -= trim;
	m_position += trim;
	return trim;
}
int64_t InkscapeClip::trimB( int64_t trim )
{
	if ( m_length - trim < 0 ) {
		return 0;
	}
	m_length -= trim;
	return trim;
}
int64_t InkscapeClip::fileLength()
{
	return m_image_clip->fileLength();
}
bool InkscapeClip::ok()
{
	return m_image_clip->ok();
}
IClipArtist* InkscapeClip::getArtist()
{
	return m_image_clip->getArtist();
}

} /* namespace nle */
