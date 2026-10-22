// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <lqt.h>
#include <string>
namespace nle {
void extend_encoder_registry();
lqt_codec_info_t *encoding_codec(const char *name,bool audio=false);
lqt_codec_info_t **find_encoding_codec(const char *name,bool audio=false);
void free_encoding_query(lqt_codec_info_t **info);
bool ffmpeg_video_encoder(const char *name);
std::string video_encoder_name(const char *name);
std::string audio_encoder_name(const char *name);
}
