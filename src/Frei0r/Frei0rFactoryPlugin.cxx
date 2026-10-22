#include "Frei0r/Frei0rModule.H"
/* Frei0rFactoryPlugin.cxx
 *
 *  Copyright (C) 2006 Richard Spindler <richard.spindler AT gmail.com>
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

#include <dlfcn.h>
#include "Frei0rFactoryPlugin.H"
#include "Frei0rEffect.H"
#include "timeline/Clip.H"
#include "VideoEffectClip.H"

namespace nle
{

Frei0rFactoryPlugin::Frei0rFactoryPlugin( std::string filename )
{
    m_handle=nle::acquire_frei0r(filename,m_info);
    m_ok=m_handle!=nullptr;
    m_ok=m_ok && m_info.plugin_type==F0R_PLUGIN_TYPE_FILTER;
}

Frei0rFactoryPlugin::~Frei0rFactoryPlugin() {nle::release_frei0r(m_handle);}

FilterBase* Frei0rFactoryPlugin::get( Clip* clip )
{
	VideoEffectClip* effectClip = dynamic_cast<VideoEffectClip*>(clip);
	if ( !effectClip ) {
		return 0;
	}
	Frei0rEffect* effect = new Frei0rEffect( &m_info, m_handle, effectClip->w(), effectClip->h() );
	return effect;

}
/*
IVideoEffect* Frei0rFactoryPlugin::get( int w, int h )
{
	Frei0rEffect* effect = new Frei0rEffect( &m_info, m_handle, w, h );
	return effect;
}*/
const char* Frei0rFactoryPlugin::name()
{
	return m_info.name;
}
const char* Frei0rFactoryPlugin::identifier()
{
	m_identifier = "effect:frei0r:";
	m_identifier += name();
	return m_identifier.c_str();
}

} /* namespace nle */
