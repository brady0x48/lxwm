#ifndef LXWM_KEYPRESSES_H
#define LXWM_KEYPRESSES_H

#include "wm_state.h"

void grab_keys(void);
void apply_monitor_workspace_config(void);
void execute_action(KeyBind *kb);
void reload_runtime_config(void);

#endif
