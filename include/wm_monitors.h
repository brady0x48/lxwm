#ifndef LXWM_MONITORS_H
#define LXWM_MONITORS_H

#include "wm_state.h"

Monitor *monitor_from_point(int x, int y);
Monitor *monitor_from_window(Window w);
Monitor *monitor_from_pointer(void);
void warp_pointer_to_monitor(Monitor *m);
void setup_monitors(void);
void sync_bar_windows(void);

#endif
