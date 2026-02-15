#ifndef LXWM_WORKSPACE_H
#define LXWM_WORKSPACE_H

#include "wm_state.h"

void switch_workspace(Monitor *m, int idx);
void move_client_to_workspace(Client *c, int idx);

#endif
