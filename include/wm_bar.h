#ifndef LXWM_BAR_H
#define LXWM_BAR_H

#include "wm_state.h"

void draw_bar(Monitor *m);
void map_workspace_floating(Monitor *m, int ws_idx, int visible);
void grab_client_buttons(Window w);
int bar_workspace_from_x(Monitor *m, int x);

#endif
