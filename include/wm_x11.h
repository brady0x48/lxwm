#ifndef LXWM_X11_H
#define LXWM_X11_H

#include "wm_state.h"

void cleanup(void);
void run(void);
void setup(void);
void refresh_ipc_state(void);
void ewmh_set_active_window(Window w);
void ewmh_set_current_desktop(unsigned long d);
void ewmh_update_client_list(void);
void ewmh_update_workarea(void);

#endif
