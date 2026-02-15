#ifndef LXWM_FOCUS_H
#define LXWM_FOCUS_H

#include "wm_state.h"

Client *next_client_in_ws(Workspace *ws, Client *from, int reverse);
void set_focus(Client *c);
Client *find_client(Window w);
Client *find_client_any(Window w);
Client *find_client_from_window(Window w);
Client *current_focused_client(Monitor *m);
Client *focus_direction(Monitor *m, Client *from, int dir_x, int dir_y);

#endif
