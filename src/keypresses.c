#include "wm_internal.h"

void grab_keys(void)
{
    XUngrabKey(dpy, AnyKey, AnyModifier, root);

    unsigned int ignored[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    for (int i = 0; i < keybind_count; i++) {
        KeyBind *kb = &keybinds[i];
        if (!kb->keycode) {
            continue;
        }
        for (size_t j = 0; j < sizeof(ignored) / sizeof(ignored[0]); j++) {
            XGrabKey(dpy, kb->keycode, kb->mod | ignored[j], root, True, GrabModeAsync,
                     GrabModeAsync);
        }
    }
}

void apply_monitor_workspace_config(void)
{
    unsigned int default_mask = all_workspaces_mask();

    for (int i = 0; i < monitor_count; i++) {
        monitors[i].ws_mask = default_mask;
    }

    if (monitor_count < 2) {
        return;
    }

    for (int i = 0; i < monitor_count; i++) {
        if (configured_ws_mask_set[i] && configured_ws_masks[i] != 0) {
            monitors[i].ws_mask = configured_ws_masks[i];
        }
    }

    for (int ws = 0; ws < MAX_WORKSPACES; ws++) {
        bool assigned = false;
        for (int m = 0; m < monitor_count; m++) {
            if (monitor_allows_workspace(&monitors[m], ws)) {
                assigned = true;
                break;
            }
        }
        if (!assigned) {
            monitors[0].ws_mask |= workspace_bit(ws);
        }
    }

    for (int i = 0; i < monitor_count; i++) {
        Monitor *m = &monitors[i];
        if (!monitor_allows_workspace(m, m->current_ws)) {
            int next_ws = first_allowed_workspace(m);
            Workspace *old_ws = &m->ws[m->current_ws];
            Workspace *new_ws = &m->ws[next_ws];
            unmap_tree(old_ws->root);
            map_workspace_floating(m, m->current_ws, 0);
            m->current_ws = next_ws;
            map_tree(new_ws->root);
            map_workspace_floating(m, m->current_ws, 1);
            if (new_ws->focus && new_ws->focus->mapped) {
                set_focus(new_ws->focus);
            }
        }
    }
}

void execute_action(KeyBind *kb)
{
    if (!kb) {
        return;
    }

    Monitor *m = selmon ? selmon : monitor_from_pointer();
    if (!m && monitor_count > 0) {
        m = &monitors[0];
    }
    if (!m) {
        return;
    }

    Workspace *ws = &m->ws[m->current_ws];
    Client *focus = current_focused_client(m);
    int dx = 0, dy = 0;

    switch (kb->action) {
    case ACT_SPAWN:
        spawn_target_monitor = monitor_from_pointer();
        if (!spawn_target_monitor) {
            spawn_target_monitor = m;
        }
        spawn_target_time = time(NULL);
        if (spawn_target_monitor) {
            selmon = spawn_target_monitor;
        }
        spawn_command(kb->arg);
        break;
    case ACT_FOCUS_NEXT: {
        Client *n = next_client_in_ws(ws, focus, 0);
        if (n) {
            set_focus(n);
        }
    } break;
    case ACT_FOCUS_PREV: {
        Client *n = next_client_in_ws(ws, focus, 1);
        if (n) {
            set_focus(n);
        }
    } break;
    case ACT_FOCUS_LEFT: {
        Client *n = focus_direction(m, focus, -1, 0);
        if (n) {
            set_focus(n);
        }
    } break;
    case ACT_FOCUS_RIGHT: {
        Client *n = focus_direction(m, focus, 1, 0);
        if (n) {
            set_focus(n);
        }
    } break;
    case ACT_FOCUS_UP: {
        Client *n = focus_direction(m, focus, 0, -1);
        if (n) {
            set_focus(n);
        }
    } break;
    case ACT_FOCUS_DOWN: {
        Client *n = focus_direction(m, focus, 0, 1);
        if (n) {
            set_focus(n);
        }
    } break;
    case ACT_KILL:
        if (focus) {
            kill_client(focus);
        }
        break;
    case ACT_QUIT:
        running = 0;
        break;
    case ACT_WS: {
        Monitor *pm = monitor_from_pointer();
        switch_workspace(pm ? pm : m, kb->iarg);
    } break;
    case ACT_MOVE_TO_WS:
        if (focus) {
            move_client_to_workspace(focus, kb->iarg);
        }
        break;
    case ACT_RELOAD:
        reload_runtime_config();
        break;
    case ACT_RESTART:
        wm_restart_requested = 1;
        running = 0;
        break;
    case ACT_TOGGLE_FLOAT: {
        Client *target = focus;
        Window fw = None;
        int revert_to = RevertToPointerRoot;
        XGetInputFocus(dpy, &fw, &revert_to);
        Client *focused_any = find_client_any(fw);
        if (focused_any && focused_any->mapped && focused_any->mon == m &&
            focused_any->workspace == m->current_ws) {
            target = focused_any;
        }
        if (target) {
            set_floating(target, !target->is_floating);
        }
    } break;
    case ACT_TOGGLE_FULLSCREEN:
        if (focus) {
            toggle_fullscreen(focus);
        }
        break;
    case ACT_SPLIT_TOGGLE:
        if (focus) {
            toggle_split_orientation(focus);
        }
        break;
    case ACT_ROTATE_TREE:
        rotate_workspace_layout(m);
        break;
    case ACT_SWAP_NEXT:
        if (focus) {
            swap_with_next_tiled(m, focus);
        }
        break;
    case ACT_SCRATCHPAD_TOGGLE:
        toggle_scratchpad(m, focus);
        break;
    case ACT_MOVE_FLOAT:
        if (focus && kb->arg[0] && sscanf(kb->arg, "%d %d", &dx, &dy) == 2) {
            if (focus->is_floating) {
                move_resize_floating(focus, focus->x + dx, focus->y + dy, focus->w, focus->h);
            }
        }
        break;
    case ACT_RESIZE_FLOAT:
        if (focus && kb->arg[0] && sscanf(kb->arg, "%d %d", &dx, &dy) == 2) {
            if (focus->is_floating) {
                move_resize_floating(focus, focus->x, focus->y, focus->w + dx, focus->h + dy);
            } else {
                resize_tiled_client(focus, dx, dy);
            }
        }
        break;
    case ACT_NONE:
    default:
        break;
    }
}

void reload_runtime_config(void)
{
    load_keybinds();
    load_wm_font();
    grab_keys();
    sync_bar_windows();
    refresh_ipc_state();
    for (Client *c = clients; c; c = c->next) {
        grab_client_buttons(c->win);
        grab_client_buttons(c->titlebar);
    }
    apply_monitor_workspace_config();
    arrange_all();
    ewmh_update_workarea();
}
