#include <algorithm>
/*  AudioClip.cxx
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

#include "AudioClip.H"
#include "IAudioFile.H"
#include "WavArtist.H"
#include "AudioClipArtist.H"
#include "IAudioFilter.H"
#include "timeline/Track.H"


namespace nle
{
AudioClip::AudioClip( Track *track, int64_t position, IAudioFile* af, int64_t trimA, int64_t trimB, int id )
	: FilterClip( track, position, id )
{
	m_mute = false;
    m_lastSamplePosition=-1;
	m_audioFile = af;
	m_trimA = trimA;
	m_trimB = trimB;
	m_audioReader = 0;
	if ( track->render_mode() ) {  // Render mode
		m_audioReader = m_audioFile;
		m_artist = 0;
	} else { // Playback mode
		g_wavArtist->add( af );
		m_artist = new AudioClipArtist( this );
		if ( m_audioFile ) {
			m_audioReader = m_audioFile;
		}
	}
}

AudioClip::AudioClip( Track* track, int64_t position, IAudioFile* af, int id )
	: FilterClip( track, position, id )
{
	m_mute = false;
    m_lastSamplePosition=-1;
	m_audioFile = af;
	m_artist = 0;
	m_audioReader = 0;
	if ( m_audioFile ) {
		if ( track->render_mode() ) {
			m_audioReader = m_audioFile;
		} else {
			m_audioReader = m_audioFile;
		}
	}
}
AudioClip::~AudioClip()
{
	m_audioReader = 0;
	if ( m_artist ) {
		g_wavArtist->remove( m_audioFile->filename() );
		delete m_artist;
		m_artist = 0;
	}
	if ( m_audioFile ) {
		delete m_audioFile;
		m_audioFile = 0;
	}
}
int AudioClip::fillBufferRaw(float *output,unsigned long frames,int64_t position)
{
  std::fill(output,output+frames*2,0.0f);
  const int64_t start=audioPosition(),end=start+audioLength();
  if(position>=end)return 0;
  int64_t consumed=std::min<int64_t>(frames,end-position);
  if(!m_mute&&m_audioReader){
    int64_t first=std::max<int64_t>(position,start),count=std::min<int64_t>(position+frames,end)-first;
    if(count>0){if(m_lastSamplePosition!=first)m_audioReader->sampleseek(0,first-start+audioTrimA());m_audioReader->fillBuffer(output+(first-position)*2,count);}
  }
  m_lastSamplePosition=position+consumed;return consumed;
}

int AudioClip::fillBuffer( float* output, unsigned long frames, int64_t position )
{
	int result = fillBufferRaw( output, frames, position );
	filter_stack* node = m_filters;
	IAudioFilter* filter;
	while ( node ) {
		filter = dynamic_cast<IAudioFilter*>( node->filter );
		if ( filter ) {
			filter->fillBuffer( output, frames, position );
		}
		node = node->next;
	}
	return result;
}
void AudioClip::reset()
{
	if ( m_audioFile ) {
		m_audioFile->seek( audioTrimA() );
	}
	m_lastSamplePosition = 0;
	FilterClip::reset();
}
int64_t AudioClip::trimA( int64_t trim )
{
	if ( trim + m_trimA < 0 ) {
		trim = -m_trimA;
	}
	if ( length() - trim <= 0 || trim == 0 ) {
		return 0;
	}
	for ( filter_stack* node = m_filters; node; node = node->next ) {
		node->filter->trimA( trim );
	}
	return Clip::trimA( trim );
}
int64_t AudioClip::trimB( int64_t trim )
{
	if ( trim + m_trimB < 0 ) {
		trim = -m_trimB;
	}
	if  ( length() - trim <= 0 ) {
		return 0;
	}
	for ( filter_stack* node = m_filters; node; node = node->next ) {
		node->filter->trimB( trim );
	}
	return Clip::trimB( trim );
}
int64_t AudioClip::audioPosition()
{
	return position();
}

DragHandler* AudioClip::onMouseDown( Rect& rect, int x, int y, bool shift )
{
	DragHandler* h;
	filter_stack* node = m_filters;
	IAudioFilter* filter;
	while ( node ) {
		filter = dynamic_cast<IAudioFilter*>( node->filter );
		if ( filter ) {
			h = filter->onMouseDown( rect, x, y, shift );
			if ( h ) {
				return h;
			}
		}
		node = node->next;
	}
	return 0;
}

int64_t AudioClip::audioTrimA()
{
	return m_trimA;
}
int64_t AudioClip::audioTrimB()
{
	return m_trimB;
}
int64_t AudioClip::audioLength()
{
	if ( !m_audioFile ) {
		return 0;
	}
	return m_audioFile->length() - ( audioTrimA() + audioTrimB() );
}
int64_t AudioClip::length()
{
	return audioLength();
}
int64_t AudioClip::fileLength()
{
	return m_audioFile->length();
}
string AudioClip::filename()
{
	return m_audioFile->filename();
}


	
} /* namespace nle */
