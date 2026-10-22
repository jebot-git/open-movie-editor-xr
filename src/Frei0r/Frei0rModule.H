// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "frei0r.h"
#include <string>
namespace nle {
void *acquire_frei0r(const std::string &filename,f0r_plugin_info_t &info);
void release_frei0r(void *module);
}
