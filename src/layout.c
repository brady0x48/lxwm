#include "wm_internal.h"

static Client *first_tiled_client_in_ws(Workspace *ws)
{
    if (!ws) {
        return NULL;
    }
    for (Client *c = clients; c; c = c->next) {
        if (!c->mapped || c->is_hidden || c->is_floating || !c->leaf || !c->mon) {
            continue;
        }
        if (c->mon->ws + c->workspace == ws) {
            return c;
        }
    }
    return NULL;
}

void restack_workspace(Monitor *m)
{
    if (!m) {
        return;
    }

    for (Client *c = clients; c; c = c->next) {
        if (!c->mapped || c->is_hidden || c->mon != m || c->workspace != m->current_ws) {
            continue;
        }
        if (c->is_fullscreen || c->is_floating) {
            continue;
        }
        XRaiseWindow(dpy, c->titlebar);
        XRaiseWindow(dpy, c->win);
    }

    for (Client *c = clients; c; c = c->next) {
        if (!c->mapped || c->is_hidden || c->mon != m || c->workspace != m->current_ws) {
            continue;
        }
        if (!c->is_floating || c->is_fullscreen) {
            continue;
        }
        XRaiseWindow(dpy, c->titlebar);
        XRaiseWindow(dpy, c->win);
    }
    raise_dock_windows();
}

void arrange_monitor(Monitor *m)
{
    if (!m) {
        return;
    }
    Workspace *ws = &m->ws[m->current_ws];

    Client *fullscreen = NULL;
    for (Client *c = clients; c; c = c->next) {
        if (!c->mapped || c->mon != m || c->workspace != m->current_ws) {
            continue;
        }
        if (c->is_fullscreen) {
            fullscreen = c;
            break;
        }
    }

    if (fullscreen) {
        fullscreen->x = m->x;
        fullscreen->y = m->y;
        fullscreen->w = m->w;
        fullscreen->h = m->h;
        XUnmapWindow(dpy, fullscreen->titlebar);
        XMoveResizeWindow(dpy, fullscreen->win, fullscreen->x, fullscreen->y,
                          (unsigned int)fullscreen->w, (unsigned int)fullscreen->h);
        XRaiseWindow(dpy, fullscreen->win);
        if (m->bar) {
            XUnmapWindow(dpy, m->bar);
        }
        return;
    }

    if (!ws->root) {
        if (bar_enabled && m->bar) {
            XMapRaised(dpy, m->bar);
        }
        restack_workspace(m);
        draw_bar(m);
        return;
    }

    ws->root->x = m->x;
    ws->root->y = m->y;
    ws->root->w = m->w;
    ws->root->h = m->h - m->bar_h;

    traverse_arrange(ws->root);

    if (bar_enabled && m->bar) {
        XMapRaised(dpy, m->bar);
    }
    restack_workspace(m);
    draw_bar(m);
}

void arrange_all(void)
{
    for (int i = 0; i < monitor_count; i++) {
        arrange_monitor(&monitors[i]);
    }
    XSync(dpy, False);
}

void insert_into_workspace(Workspace *ws, Client *c)
{
    if (!ws->root) {
        ws->root = create_leaf(c);
        c->leaf = ws->root;
        return;
    }

    Node *target = NULL;
    if (ws->focus && ws->focus->leaf) {
        target = ws->focus->leaf;
    }
    if (!target) {
        target = find_first_leaf(ws->root);
    }
    if (!target) {
        ws->root = create_leaf(c);
        c->leaf = ws->root;
        return;
    }

    Client *old = target->client;
    Node *left = create_leaf(old);
    Node *right = create_leaf(c);

    left->parent = target;
    right->parent = target;

    target->client = NULL;
    target->first = left;
    target->second = right;
    target->ratio = 0.5f;

    if (target->w >= target->h) {
        target->split_vertical = 1;
    } else {
        target->split_vertical = 0;
    }

    if (old) {
        old->leaf = left;
    }
    c->leaf = right;
}

void remove_from_workspace(Workspace *ws, Client *c)
{
    if (!ws || !ws->root || !c || !c->leaf) {
        return;
    }

    Node *leaf = c->leaf;
    Node *parent = leaf->parent;

    if (!parent) {
        free(leaf);
        ws->root = NULL;
        c->leaf = NULL;
        return;
    }

    Node *sibling = (parent->first == leaf) ? parent->second : parent->first;
    Node *grand = parent->parent;

    if (!grand) {
        ws->root = sibling;
        sibling->parent = NULL;
    } else {
        if (grand->first == parent) {
            grand->first = sibling;
        } else {
            grand->second = sibling;
        }
        sibling->parent = grand;
    }

    if (parent->first == leaf) {
        parent->first = NULL;
    } else {
        parent->second = NULL;
    }

    free(leaf);
    free(parent);
    c->leaf = NULL;
}

void detach_client(Client *c)
{
    if (!c) {
        return;
    }

    if (c->mon) {
        Workspace *ws = &c->mon->ws[c->workspace];
        if (ws->focus == c) {
            ws->focus = NULL;
        }
        remove_from_workspace(ws, c);
    }

    if (clients == c) {
        clients = c->next;
    } else {
        for (Client *it = clients; it; it = it->next) {
            if (it->next == c) {
                it->next = c->next;
                break;
            }
        }
    }
}

void attach_client(Client *c, Monitor *m, int ws_idx)
{
    c->mon = m;
    c->workspace = ws_idx;
    c->next = clients;
    clients = c;

    Workspace *ws = &m->ws[ws_idx];
    if (!c->is_floating) {
        insert_into_workspace(ws, c);
    }
    ws->focus = c;
}

int client_is_fixed_or_transient(Window w)
{
    XSizeHints hints;
    long supplied = 0;
    if (XGetWMNormalHints(dpy, w, &hints, &supplied)) {
        if ((hints.flags & PMinSize) && (hints.flags & PMaxSize) && hints.min_width > 0 &&
            hints.min_height > 0 && hints.min_width == hints.max_width &&
            hints.min_height == hints.max_height) {
            return 1;
        }
    }
    Window transient_for = None;
    if (XGetTransientForHint(dpy, w, &transient_for)) {
        return 1;
    }
    return 0;
}

void move_resize_floating(Client *c, int nx, int ny, int nw, int nh)
{
    if (!c || !c->mon) {
        return;
    }
    Monitor *m = c->mon;
    int min_w = 64;
    int min_h = 48;
    int max_w = m->w - (2 * BORDER_WIDTH);
    int max_h = m->h - m->bar_h - TITLEBAR_HEIGHT - (2 * BORDER_WIDTH);
    if (max_w < min_w) {
        max_w = min_w;
    }
    if (max_h < min_h) {
        max_h = min_h;
    }
    if (nw < min_w) {
        nw = min_w;
    }
    if (nh < min_h) {
        nh = min_h;
    }
    if (nw > max_w) {
        nw = max_w;
    }
    if (nh > max_h) {
        nh = max_h;
    }

    int min_x = m->x;
    int min_y = m->y + TITLEBAR_HEIGHT;
    int max_x = m->x + m->w - nw - (2 * BORDER_WIDTH);
    int max_y = m->y + m->h - m->bar_h - nh - (2 * BORDER_WIDTH);
    if (max_x < min_x) {
        max_x = min_x;
    }
    if (max_y < min_y) {
        max_y = min_y;
    }
    nx = clamp_int(nx, min_x, max_x);
    ny = clamp_int(ny, min_y, max_y);

    c->x = nx;
    c->y = ny;
    c->w = nw;
    c->h = nh;

    XMoveResizeWindow(dpy, c->titlebar, c->x, c->y - TITLEBAR_HEIGHT,
                      (unsigned int)(c->w + (2 * BORDER_WIDTH)), TITLEBAR_HEIGHT);
    XMoveResizeWindow(dpy, c->win, c->x, c->y, (unsigned int)c->w, (unsigned int)c->h);
    draw_titlebar(c);
}

void set_floating(Client *c, int floating)
{
    if (!c || !c->mon) {
        return;
    }
    if (!!c->is_floating == !!floating) {
        return;
    }

    Workspace *ws = &c->mon->ws[c->workspace];
    if (floating) {
        int was_tiled = !c->is_floating;
        XWindowAttributes wa;
        if (XGetWindowAttributes(dpy, c->win, &wa)) {
            c->x = wa.x;
            c->y = wa.y;
            c->w = wa.width;
            c->h = wa.height;
        }
        remove_from_workspace(ws, c);
        c->is_floating = 1;
        int nx = c->x;
        int ny = c->y;
        int nw = c->w;
        int nh = c->h;
        if (was_tiled) {
            int avail_w = c->mon->w - (2 * BORDER_WIDTH);
            int avail_h = c->mon->h - c->mon->bar_h - TITLEBAR_HEIGHT - (2 * BORDER_WIDTH);
            nw = (c->w * 85) / 100;
            nh = (c->h * 85) / 100;
            if (nw < 320) {
                nw = 320;
            }
            if (nh < 220) {
                nh = 220;
            }
            if (nw > avail_w) {
                nw = avail_w;
            }
            if (nh > avail_h) {
                nh = avail_h;
            }
            nx = c->mon->x + (c->mon->w - nw) / 2;
            ny = c->mon->y + ((c->mon->h - c->mon->bar_h) - nh) / 2;
        }
        move_resize_floating(c, nx, ny, nw, nh);
        if (ws->focus == c) {
            Client *next_tiled = first_tiled_client_in_ws(ws);
            ws->focus = next_tiled ? next_tiled : next_client_in_ws(ws, NULL, 0);
        }
    } else {
        c->is_floating = 0;
        insert_into_workspace(ws, c);
        ws->focus = c;
    }
    arrange_monitor(c->mon);
    set_focus(c);
}

void toggle_fullscreen(Client *c)
{
    if (!c || !c->mon) {
        return;
    }
    Atom state = net_wm_state_fullscreen;
    if (!c->is_fullscreen) {
        c->is_fullscreen = 1;
        c->was_floating_before_fullscreen = c->is_floating;
        c->old_x = c->x;
        c->old_y = c->y;
        c->old_w = c->w;
        c->old_h = c->h;
        c->is_floating = 1;
        XChangeProperty(dpy, c->win, net_wm_state, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&state, 1);
    } else {
        c->is_fullscreen = 0;
        c->is_floating = c->was_floating_before_fullscreen;
        c->x = c->old_x;
        c->y = c->old_y;
        c->w = c->old_w;
        c->h = c->old_h;
        XDeleteProperty(dpy, c->win, net_wm_state);
        XMapWindow(dpy, c->titlebar);
    }
    arrange_monitor(c->mon);
    set_focus(c);
}

Client *current_focused_client(Monitor *m)
{
    if (!m) {
        return NULL;
    }

    Window focus_win = None;
    int revert_to = RevertToPointerRoot;
    XGetInputFocus(dpy, &focus_win, &revert_to);
    Client *focused = find_client_any(focus_win);
    if (focused && focused->mapped && focused->mon == m && focused->workspace == m->current_ws) {
        return focused;
    }

    Workspace *ws = &m->ws[m->current_ws];
    if (ws->focus && ws->focus->mapped) {
        return ws->focus;
    }
    return NULL;
}

static Node *find_resize_node(Node *leaf, int want_vertical_split)
{
    for (Node *n = leaf ? leaf->parent : NULL; n; n = n->parent) {
        if (n->split_vertical == want_vertical_split) {
            return n;
        }
    }
    return NULL;
}

void resize_tiled_client(Client *c, int dx, int dy)
{
    if (!c || !c->mon || c->is_floating || !c->leaf) {
        return;
    }

    float step = 0.0f;
    Node *n = NULL;

    if (dx != 0) {
        n = find_resize_node(c->leaf, 1);
        if (n && n->w > 0) {
            step = (float)dx / (float)n->w;
        }
    } else if (dy != 0) {
        n = find_resize_node(c->leaf, 0);
        if (n && n->h > 0) {
            step = (float)dy / (float)n->h;
        }
    }

    if (!n || step == 0.0f) {
        return;
    }

    n->ratio += step;
    if (n->ratio < 0.1f) {
        n->ratio = 0.1f;
    } else if (n->ratio > 0.9f) {
        n->ratio = 0.9f;
    }
    arrange_monitor(c->mon);
    set_focus(c);
}

void toggle_split_orientation(Client *c)
{
    if (!c || !c->leaf || !c->leaf->parent) {
        return;
    }
    c->leaf->parent->split_vertical = !c->leaf->parent->split_vertical;
    arrange_monitor(c->mon);
    set_focus(c);
}

static void rotate_node(Node *n)
{
    if (!n || is_leaf(n)) {
        return;
    }
    n->split_vertical = !n->split_vertical;
    rotate_node(n->first);
    rotate_node(n->second);
}

void rotate_workspace_layout(Monitor *m)
{
    if (!m) {
        return;
    }
    Workspace *ws = &m->ws[m->current_ws];
    if (!ws->root) {
        return;
    }
    rotate_node(ws->root);
    arrange_monitor(m);
}

static Client *next_tiled_in_ws(Workspace *ws, Client *from)
{
    bool seen = (from == NULL);
    Client *first = NULL;
    for (Client *c = clients; c; c = c->next) {
        if (!c->mapped || !c->leaf || !c->mon || (c->mon->ws + c->workspace != ws)) {
            continue;
        }
        if (!first) {
            first = c;
        }
        if (seen) {
            return c;
        }
        if (c == from) {
            seen = true;
        }
    }
    return first;
}

void swap_with_next_tiled(Monitor *m, Client *c)
{
    if (!m || !c || !c->leaf) {
        return;
    }
    Workspace *ws = &m->ws[m->current_ws];
    Client *n = next_tiled_in_ws(ws, c);
    if (!n || n == c || !n->leaf) {
        return;
    }

    Node *a = c->leaf;
    Node *b = n->leaf;
    a->client = n;
    b->client = c;
    c->leaf = b;
    n->leaf = a;
    ws->focus = c;
    arrange_monitor(m);
    set_focus(c);
}
