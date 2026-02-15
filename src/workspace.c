#include "wm_internal.h"

void switch_workspace(Monitor *m, int idx)
{
    if (!m || idx < 0 || idx >= MAX_WORKSPACES) {
        return;
    }

    Monitor *pointer_mon = monitor_from_pointer();

    Monitor *target = m;
    if (!monitor_allows_workspace(target, idx) && monitor_count > 1) {
        Monitor *owner = first_monitor_for_workspace(idx);
        if (owner) {
            target = owner;
        }
    }
    if (!monitor_allows_workspace(target, idx)) {
        return;
    }

    bool monitor_changed = (selmon != target);
    bool pointer_on_other_monitor = pointer_mon && pointer_mon != target;
    selmon = target;

    if (idx == target->current_ws) {
        if (monitor_changed || pointer_on_other_monitor) {
            warp_pointer_to_monitor(target);
        }
        ewmh_set_current_desktop((unsigned long)target->current_ws);
        for (int i = 0; i < monitor_count; i++) {
            draw_bar(&monitors[i]);
        }
        return;
    }

    m = target;

    Workspace *old_ws = &m->ws[m->current_ws];
    Workspace *new_ws = &m->ws[idx];

    unmap_tree(old_ws->root);
    map_workspace_floating(m, m->current_ws, 0);
    m->current_ws = idx;
    map_tree(new_ws->root);
    map_workspace_floating(m, idx, 1);
    arrange_monitor(m);

    if (new_ws->focus && new_ws->focus->mapped) {
        set_focus(new_ws->focus);
    }
    ewmh_set_current_desktop((unsigned long)idx);
    if (monitor_changed || pointer_on_other_monitor) {
        warp_pointer_to_monitor(m);
    }
    for (int i = 0; i < monitor_count; i++) {
        draw_bar(&monitors[i]);
    }
}

void move_client_to_workspace(Client *c, int idx)
{
    if (!c || !c->mon || idx < 0 || idx >= MAX_WORKSPACES || c->workspace == idx) {
        return;
    }

    Monitor *src_mon = c->mon;
    int src_ws_idx = c->workspace;
    bool moved_was_focused = false;
    Monitor *dst_mon = src_mon;
    if (!monitor_allows_workspace(src_mon, idx) && monitor_count > 1) {
        Monitor *owner = first_monitor_for_workspace(idx);
        if (owner) {
            dst_mon = owner;
        }
    }
    if (!monitor_allows_workspace(dst_mon, idx)) {
        return;
    }

    Workspace *src = &src_mon->ws[c->workspace];
    Workspace *dst = &dst_mon->ws[idx];

    if (src->focus == c) {
        moved_was_focused = true;
        src->focus = NULL;
    }

    remove_from_workspace(src, c);
    c->workspace = idx;
    c->mon = dst_mon;
    if (!c->is_floating) {
        insert_into_workspace(dst, c);
    }
    dst->focus = c;

    if (dst_mon->current_ws != idx) {
        c->is_hidden = 1;
        c->ignore_unmap++;
        XUnmapWindow(dpy, c->titlebar);
        XUnmapWindow(dpy, c->win);
    } else {
        c->is_hidden = 0;
        XMapWindow(dpy, c->titlebar);
        XMapWindow(dpy, c->win);
    }

    if (dst_mon->current_ws == idx) {
        set_focus(c);
    }

    if (!src->focus) {
        src->focus = next_client_in_ws(src, NULL, 0);
    }

    if (moved_was_focused && src_mon->current_ws == src_ws_idx && src->focus &&
        dst_mon->current_ws != idx) {
        set_focus(src->focus);
    }

    arrange_monitor(src_mon);
    if (dst_mon != src_mon) {
        arrange_monitor(dst_mon);
    }
}
