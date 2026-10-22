// SPDX-License-Identifier: GPL-2.0-or-later
#include "AudioFileGmerlin.H"
#include "media/FFmpegInput.H"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace nle {
struct AudioFileGmerlin::State {
  SwrContext *resample=nullptr;
  std::vector<float> pending;
  size_t offset=0;
  int64_t target=0;
  bool seeking=false,finished=false;
  ~State(){swr_free(&resample);}
};
AudioFileGmerlin::AudioFileGmerlin(std::string filename)
 : input(new FFmpegInput(filename,AVMEDIA_TYPE_AUDIO)),state(new State) {
  m_ok=false;m_filename=filename;m_samplerate=48000;m_length=0;
  if(!input->ok())return;
  AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
  int status=swr_alloc_set_opts2(&state->resample,&stereo,AV_SAMPLE_FMT_FLT,48000,
      &input->codec->ch_layout,input->codec->sample_fmt,input->codec->sample_rate,0,nullptr);
  if(status<0||swr_init(state->resample)<0)return;
  m_length=std::llround(input->duration()*48000);m_ok=m_length>0;
}
AudioFileGmerlin::~AudioFileGmerlin()=default;
void AudioFileGmerlin::seek(int64_t sample) {
  if(!m_ok)return;
  state->target=std::max<int64_t>(0,sample);state->seeking=true;state->finished=false;
  state->pending.clear();state->offset=0;
  input->seek(double(state->target)/48000);
  swr_close(state->resample);swr_init(state->resample);
}
int AudioFileGmerlin::fillBuffer(float *output,unsigned long frames) {
  std::memset(output,0,frames*2*sizeof(float));if(!m_ok)return 0;
  size_t written=0;
  while(written<frames) {
    size_t available=(state->pending.size()-state->offset)/2;
    if(available) {
      size_t n=std::min<size_t>(available,frames-written);
      std::memcpy(output+written*2,state->pending.data()+state->offset,n*2*sizeof(float));
      state->offset+=n*2;written+=n;continue;
    }
    if(state->finished)break;
    bool got=input->next();
    int capacity=got?swr_get_out_samples(state->resample,input->frame->nb_samples):swr_get_out_samples(state->resample,0);
    state->pending.resize(std::max(32,capacity)*2);state->offset=0;
    uint8_t *dest=reinterpret_cast<uint8_t*>(state->pending.data());
    int count=swr_convert(state->resample,&dest,std::max(32,capacity),got?const_cast<const uint8_t**>(input->frame->extended_data):nullptr,got?input->frame->nb_samples:0);
    if(count<0)break;
    state->pending.resize(count*2);
    if(state->seeking && got) {
      int64_t begin=std::llround(std::max(0.,input->seconds())*48000);
      int64_t skip=std::max<int64_t>(0,state->target-begin);
      state->offset=std::min<int64_t>(count,skip)*2;
      if(skip<count)state->seeking=false;
    }
    if(!got)state->finished=true;
  }
  return static_cast<int>(written);
}
}
