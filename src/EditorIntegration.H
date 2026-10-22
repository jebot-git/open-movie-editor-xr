// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <FL/Fl.H>
class Fl_Widget;
class NleUI;
namespace nle {
Fl_Font editor_font();
int editor_font_size(int fallback=14);
bool edit_focus(const char *action);
void editor_menu(Fl_Widget*,void *action);
void install_editor_integration(NleUI &ui);
void shutdown_editor_integration();
}
