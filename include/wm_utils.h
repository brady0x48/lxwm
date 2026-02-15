#ifndef LXWM_UTILS_H
#define LXWM_UTILS_H

#include "wm_state.h"

void die(const char *fmt, ...);
void *ecalloc(size_t n, size_t sz);
unsigned int workspace_bit(int idx);
unsigned int all_workspaces_mask(void);
bool monitor_allows_workspace(const Monitor *m, int ws);
int first_allowed_workspace(const Monitor *m);
Monitor *first_monitor_for_workspace(int ws);
unsigned int normalize_mods(unsigned int state);
int xerror(Display *d, XErrorEvent *ee);
int xerror_start(Display *d, XErrorEvent *ee);
unsigned long get_color(const char *name, unsigned long fallback);
int clamp_int(int v, int min_v, int max_v);
void wm_log(WmLogLevel level, const char *fmt, ...);

#endif
