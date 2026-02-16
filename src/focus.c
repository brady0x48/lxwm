#include "wm_internal.h"

Client *next_client_in_ws(Workspace *ws, Client *from, int reverse)
{
    Client *head = clients;
    Client *cand = NULL;
    Client *first = NULL;
    Client *prev = NULL;

    for (Client *c = head; c; c = c->next) {
        if (!c->mapped || !c->mon || c->mon->ws + c->workspace != ws) {
            continue;
        }
        if (!first) {
            first = c;
        }
        if (!reverse) {
            if (cand == from) {
                return c;
            }
            cand = c;
        } else {
            if (c == from) {
                return prev ? prev : first;
            }
            prev = c;
        }
    }

    if (!reverse) {
        return first;
    }
    return prev ? prev : first;
}

void set_focus(Client *c)
{
    if (!c || !c->mapped) {
        return;
    }

    selmon = c->mon;
    Workspace *ws = &c->mon->ws[c->workspace];
    ws->focus = c;
    clear_client_urgent(c);

    for (Client *it = clients; it; it = it->next) {
        if (!it->mapped || !it->mon) {
            continue;
        }
        unsigned long border = (it == c) ? color_border_focus : color_border_unfocus;
        XSetWindowBorder(dpy, it->win, border);
        draw_titlebar(it);
    }

    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
    restack_workspace(c->mon);
    if (c->is_floating || c->is_fullscreen) {
        if (c->is_fullscreen) {
            XUnmapWindow(dpy, c->titlebar);
        } else {
            XRaiseWindow(dpy, c->titlebar);
        }
        XRaiseWindow(dpy, c->win);
    }
    ewmh_set_active_window(c->win);

    for (int i = 0; i < monitor_count; i++) {
        draw_bar(&monitors[i]);
    }
}

Client *find_client(Window w)
{
    for (Client *c = clients; c; c = c->next) {
        if (c->win == w) {
            return c;
        }
    }
    return NULL;
}

Client *find_client_any(Window w)
{
    for (Client *c = clients; c; c = c->next) {
        if (c->win == w || c->titlebar == w) {
            return c;
        }
    }
    return NULL;
}

Client *find_client_from_window(Window w)
{
    if (!w || w == root) {
        return NULL;
    }

    Client *c = find_client_any(w);
    if (c) {
        return c;
    }

    Window cur = w;
    while (cur && cur != root) {
        Window root_ret = None, parent = None, *children = NULL;
        unsigned int nchildren = 0;
        if (!XQueryTree(dpy, cur, &root_ret, &parent, &children, &nchildren)) {
            return NULL;
        }
        if (children) {
            XFree(children);
        }
        if (!parent || parent == cur) {
            break;
        }
        c = find_client_any(parent);
        if (c) {
            return c;
        }
        cur = parent;
    }
    return NULL;
}

Client *focus_direction(Monitor *m, Client *from, int dir_x, int dir_y)
{
    if (!m || (!dir_x && !dir_y)) {
        return NULL;
    }

    if (!from) {
        from = current_focused_client(m);
    }
    if (!from) {
        return next_client_in_ws(&m->ws[m->current_ws], NULL, 0);
    }

    int from_left = from->x;
    int from_right = from->x + from->w;
    int from_top = from->y;
    int from_bottom = from->y + from->h;
    int from_cx = from->x + (from->w / 2);
    int from_cy = from->y + (from->h / 2);

    Client *best = NULL;
    int best_overlap_class = -1;
    int best_primary = INT_MAX;
    int best_secondary = INT_MAX;

    for (Client *c = clients; c; c = c->next) {
        if (c == from || !c->mapped || c->is_hidden || c->mon != m ||
            c->workspace != m->current_ws) {
            continue;
        }

        int c_left = c->x;
        int c_right = c->x + c->w;
        int c_top = c->y;
        int c_bottom = c->y + c->h;
        int cx = c->x + (c->w / 2);
        int cy = c->y + (c->h / 2);
        int primary = 0;
        int secondary = 0;
        int overlap = 0;
        int overlap_class = 0;

        if (dir_x < 0) {
            if (c_right <= from_left) {
                primary = from_left - c_right;
                overlap = ((c_bottom < from_bottom) ? c_bottom : from_bottom) -
                          ((c_top > from_top) ? c_top : from_top);
                if (overlap < 0) {
                    overlap = 0;
                }
                secondary = abs(cy - from_cy);
            } else if (cx < from_cx) {
                primary = from_cx - cx;
                secondary = abs(cy - from_cy);
            } else {
                continue;
            }
        } else if (dir_x > 0) {
            if (c_left >= from_right) {
                primary = c_left - from_right;
                overlap = ((c_bottom < from_bottom) ? c_bottom : from_bottom) -
                          ((c_top > from_top) ? c_top : from_top);
                if (overlap < 0) {
                    overlap = 0;
                }
                secondary = abs(cy - from_cy);
            } else if (cx > from_cx) {
                primary = cx - from_cx;
                secondary = abs(cy - from_cy);
            } else {
                continue;
            }
        } else if (dir_y < 0) {
            if (c_bottom <= from_top) {
                primary = from_top - c_bottom;
                overlap = ((c_right < from_right) ? c_right : from_right) -
                          ((c_left > from_left) ? c_left : from_left);
                if (overlap < 0) {
                    overlap = 0;
                }
                secondary = abs(cx - from_cx);
            } else if (cy < from_cy) {
                primary = from_cy - cy;
                secondary = abs(cx - from_cx);
            } else {
                continue;
            }
        } else {
            if (c_top >= from_bottom) {
                primary = c_top - from_bottom;
                overlap = ((c_right < from_right) ? c_right : from_right) -
                          ((c_left > from_left) ? c_left : from_left);
                if (overlap < 0) {
                    overlap = 0;
                }
                secondary = abs(cx - from_cx);
            } else if (cy > from_cy) {
                primary = cy - from_cy;
                secondary = abs(cx - from_cx);
            } else {
                continue;
            }
        }

        overlap_class = (overlap > 0) ? 1 : 0;
        if (overlap_class > best_overlap_class ||
            (overlap_class == best_overlap_class && primary < best_primary) ||
            (overlap_class == best_overlap_class && primary == best_primary &&
             secondary < best_secondary)) {
            best = c;
            best_overlap_class = overlap_class;
            best_primary = primary;
            best_secondary = secondary;
        }
    }

    return best;
}
