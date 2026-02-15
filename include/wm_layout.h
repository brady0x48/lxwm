#ifndef LXWM_LAYOUT_H
#define LXWM_LAYOUT_H

#include "wm_state.h"

void arrange_monitor(Monitor *m);
void arrange_all(void);
void restack_workspace(Monitor *m);
void insert_into_workspace(Workspace *ws, Client *c);
void remove_from_workspace(Workspace *ws, Client *c);
void detach_client(Client *c);
void attach_client(Client *c, Monitor *m, int ws_idx);
int client_is_fixed_or_transient(Window w);
void move_resize_floating(Client *c, int nx, int ny, int nw, int nh);
void set_floating(Client *c, int floating);
void toggle_fullscreen(Client *c);
void resize_tiled_client(Client *c, int dx, int dy);
void toggle_split_orientation(Client *c);
void rotate_workspace_layout(Monitor *m);
void swap_with_next_tiled(Monitor *m, Client *c);

#endif
