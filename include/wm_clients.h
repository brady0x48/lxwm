#ifndef LXWM_CLIENTS_H
#define LXWM_CLIENTS_H

#include "wm_state.h"

int should_manage(Window w);
void manage(Window w);
void unmanage(Client *c, int destroyed);
void kill_client(Client *c);
void update_client_urgent(Client *c);
void clear_client_urgent(Client *c);
void toggle_scratchpad(Monitor *m, Client *focus);

#endif
