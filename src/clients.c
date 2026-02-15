#include "wm_internal.h"

static int window_has_fullscreen_state(Window w)
{
    Atom actual = None;
    int format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char *data = NULL;
    int has_fullscreen = 0;

    if (XGetWindowProperty(dpy, w, net_wm_state, 0, 64, False, XA_ATOM, &actual, &format,
                           &nitems, &bytes_after, &data) == Success &&
        actual == XA_ATOM && format == 32 && data) {
        Atom *atoms = (Atom *)data;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == net_wm_state_fullscreen) {
                has_fullscreen = 1;
                break;
            }
        }
    }
    if (data) {
        XFree(data);
    }
    return has_fullscreen;
}

static const char *find_case_substr(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !*needle) {
        return NULL;
    }
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, nlen) == 0) {
            return p;
        }
    }
    return NULL;
}

int should_manage(Window w)
{
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa)) {
        return 0;
    }
    if (wa.override_redirect) {
        return 0;
    }
    if (wa.map_state == IsViewable || wa.map_state == IsUnviewable || wa.map_state == IsUnmapped) {
        return 1;
    }
    return 0;
}

static int rule_matches(const Rule *r, const char *klass, const char *instance, const char *title)
{
    if (!r) {
        return 0;
    }
    int has_match = 0;
    if (r->class_name[0]) {
        has_match = 1;
        if (!klass || strcasecmp(r->class_name, klass) != 0) {
            return 0;
        }
    }
    if (r->instance_name[0]) {
        has_match = 1;
        if (!instance || strcasecmp(r->instance_name, instance) != 0) {
            return 0;
        }
    }
    if (r->title_substr[0]) {
        has_match = 1;
        if (!title || !find_case_substr(title, r->title_substr)) {
            return 0;
        }
    }
    return has_match;
}

static void apply_window_rules(Window w, Monitor **mon_out, int *ws_out, int *floating_out)
{
    if (!mon_out || !ws_out || !floating_out) {
        return;
    }
    if (rule_count <= 0) {
        return;
    }

    XClassHint ch;
    memset(&ch, 0, sizeof(ch));
    if (!XGetClassHint(dpy, w, &ch)) {
        return;
    }

    const char *klass = ch.res_class ? ch.res_class : NULL;
    const char *instance = ch.res_name ? ch.res_name : NULL;

    char title[TITLE_MAX];
    window_title(w, title, sizeof(title));
    const char *titlep = title[0] ? title : NULL;

    for (int i = 0; i < rule_count; i++) {
        Rule *r = &rules[i];
        if (!rule_matches(r, klass, instance, titlep)) {
            continue;
        }
        if (r->monitor >= 0 && r->monitor < monitor_count) {
            *mon_out = &monitors[r->monitor];
        }
        if (r->workspace >= 0 && r->workspace < MAX_WORKSPACES) {
            *ws_out = r->workspace;
        }
        if (r->set_floating) {
            *floating_out = r->floating;
        }
    }

    if (ch.res_name) {
        XFree(ch.res_name);
    }
    if (ch.res_class) {
        XFree(ch.res_class);
    }
}

void update_client_urgent(Client *c)
{
    if (!c) {
        return;
    }
    XWMHints *hints = XGetWMHints(dpy, c->win);
    int urgent = 0;
    if (hints) {
        urgent = (hints->flags & XUrgencyHint) ? 1 : 0;
        XFree(hints);
    }
    if (c->is_urgent != urgent) {
        c->is_urgent = urgent;
        if (c->mon) {
            draw_bar(c->mon);
        }
    }
}

void clear_client_urgent(Client *c)
{
    if (!c || !c->is_urgent) {
        return;
    }
    c->is_urgent = 0;
    XWMHints *hints = XGetWMHints(dpy, c->win);
    if (!hints) {
        return;
    }
    hints->flags &= ~XUrgencyHint;
    XSetWMHints(dpy, c->win, hints);
    XFree(hints);
}

static void hide_to_scratchpad(Client *c)
{
    if (!c || !c->mon || c->is_hidden) {
        return;
    }
    if (c->is_fullscreen) {
        toggle_fullscreen(c);
    }

    Workspace *ws = &c->mon->ws[c->workspace];
    c->scratch_was_floating = c->is_floating;
    if (!c->is_floating) {
        remove_from_workspace(ws, c);
    }
    if (ws->focus == c) {
        ws->focus = next_client_in_ws(ws, c, 0);
    }

    c->is_scratchpad = 1;
    c->is_hidden = 1;
    c->ignore_unmap++;
    XUnmapWindow(dpy, c->titlebar);
    XUnmapWindow(dpy, c->win);
    arrange_monitor(c->mon);
}

static void show_from_scratchpad(Client *c, Monitor *m)
{
    if (!c) {
        return;
    }
    if (!m) {
        m = selmon;
    }
    if (!m) {
        m = monitor_from_pointer();
    }
    if (!m) {
        return;
    }

    c->mon = m;
    c->workspace = m->current_ws;
    c->is_hidden = 0;
    c->is_scratchpad = 1;
    c->is_floating = c->scratch_was_floating;
    if (!c->is_floating) {
        Workspace *ws = &m->ws[m->current_ws];
        insert_into_workspace(ws, c);
        ws->focus = c;
    } else {
        int nw = c->w > 0 ? c->w : (m->w / 2);
        int nh = c->h > 0 ? c->h : (m->h / 2);
        int nx = m->x + (m->w - nw) / 2;
        int ny = m->y + ((m->h - m->bar_h) - nh) / 2;
        move_resize_floating(c, nx, ny, nw, nh);
    }

    XMapWindow(dpy, c->titlebar);
    XMapWindow(dpy, c->win);
    arrange_monitor(m);
    set_focus(c);
}

void toggle_scratchpad(Monitor *m, Client *focus)
{
    Client *c = scratchpad_client;
    if (!c) {
        for (Client *it = clients; it; it = it->next) {
            if (it->is_scratchpad) {
                c = it;
                break;
            }
        }
    }
    if (!c) {
        if (focus && !focus->is_scratchpad) {
            scratchpad_client = focus;
            hide_to_scratchpad(focus);
        }
        return;
    }

    scratchpad_client = c;
    if (c->is_hidden) {
        show_from_scratchpad(c, m);
    } else if (focus && focus->is_scratchpad) {
        hide_to_scratchpad(focus);
    } else if (focus && !focus->is_scratchpad) {
        scratchpad_client = focus;
        hide_to_scratchpad(focus);
    } else {
        set_focus(c);
    }
}

void manage(Window w)
{
    if (find_client(w)) {
        return;
    }

    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa)) {
        return;
    }
    if (wa.override_redirect) {
        return;
    }

    Monitor *m = NULL;
    int used_spawn_target = 0;
    time_t now = time(NULL);
    if (spawn_target_monitor && spawn_target_time > 0 && now != (time_t)-1 &&
        (now - spawn_target_time) <= 3) {
        m = spawn_target_monitor;
        used_spawn_target = 1;
        spawn_target_monitor = NULL;
        spawn_target_time = 0;
    } else {

        m = selmon;
        if (!m) {
            m = monitor_from_pointer();
        }
        if (!m) {
            m = monitor_from_window(w);
        }
    }
    if (!m) {
        m = selmon;
    }
    if (!m) {
        return;
    }

    int rule_ws = m->current_ws;
    int rule_floating = client_is_fixed_or_transient(w);
    apply_window_rules(w, &m, &rule_ws, &rule_floating);
    if (!monitor_allows_workspace(m, rule_ws) && monitor_count > 1) {
        Monitor *owner = first_monitor_for_workspace(rule_ws);
        if (owner) {
            m = owner;
        } else {
            rule_ws = first_allowed_workspace(m);
        }
    }
    if (!monitor_allows_workspace(m, rule_ws)) {
        rule_ws = first_allowed_workspace(m);
    }

    Client *c = ecalloc(1, sizeof(Client));
    c->win = w;
    c->x = wa.x;
    c->y = wa.y;
    c->w = wa.width;
    c->h = wa.height;
    c->mapped = 1;
    c->is_hidden = 0;
    c->old_bw = wa.border_width;
    c->is_floating = rule_floating;
    c->is_fullscreen = window_has_fullscreen_state(w);
    c->was_floating_before_fullscreen = c->is_floating;
    if (c->is_fullscreen) {
        c->is_floating = 1;
    }
    c->is_scratchpad = 0;
    c->is_urgent = 0;

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    c->titlebar =
        XCreateWindow(dpy, root, wa.x, wa.y, (unsigned int)(wa.width + (2 * BORDER_WIDTH)),
                      TITLEBAR_HEIGHT, 0, DefaultDepth(dpy, screen_num), CopyFromParent,
                      DefaultVisual(dpy, screen_num), CWOverrideRedirect, &attrs);
    XSelectInput(dpy, c->titlebar, ExposureMask | ButtonPressMask | EnterWindowMask);
    grab_client_buttons(c->titlebar);

    XSetWindowBorderWidth(dpy, w, BORDER_WIDTH);
    XSelectInput(dpy, w,
                 EnterWindowMask | FocusChangeMask | PropertyChangeMask | StructureNotifyMask |
                     ButtonPressMask);
    grab_client_buttons(w);

    attach_client(c, m, rule_ws);
    ewmh_update_client_list();

    Workspace *ws = &m->ws[rule_ws];
    map_tree(ws->root);

    if (c->is_floating) {
        int cx = c->x + (c->w / 2);
        int cy = c->y + (c->h / 2);
        Monitor *geom_mon = monitor_from_point(cx, cy);
        int place_on_target = used_spawn_target || (geom_mon != m);

        int avail_w = m->w - (2 * BORDER_WIDTH);
        int avail_h = m->h - m->bar_h - TITLEBAR_HEIGHT - (2 * BORDER_WIDTH);
        int nw = c->w;
        int nh = c->h;

        if (nw > avail_w) {
            nw = avail_w;
        }
        if (nh > avail_h) {
            nh = avail_h;
        }

        if (nw >= (m->w * 95) / 100) {
            nw = (m->w * 85) / 100;
        }
        if (nh >= ((m->h - m->bar_h) * 95) / 100) {
            nh = ((m->h - m->bar_h) * 85) / 100;
        }

        if (nw < 320) {
            nw = (avail_w < 320) ? avail_w : 320;
        }
        if (nh < 220) {
            nh = (avail_h < 220) ? avail_h : 220;
        }

        int nx = c->x;
        int ny = c->y;
        if (place_on_target || nw != c->w || nh != c->h) {
            nx = m->x + (m->w - nw) / 2;
            ny = m->y + ((m->h - m->bar_h) - nh) / 2;
        }

        move_resize_floating(c, nx, ny, nw, nh);
    }

    arrange_monitor(m);
    set_focus(c);
    if (!c->is_fullscreen) {
        XMapWindow(dpy, c->titlebar);
    }
    XMapWindow(dpy, w);
    update_client_urgent(c);
}

void unmanage(Client *c, int destroyed)
{
    if (!c) {
        return;
    }

    Monitor *m = c->mon;
    int ws_idx = c->workspace;

    if (!destroyed) {
        XSetWindowBorderWidth(dpy, c->win, c->old_bw);
        XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
    }
    if (c->titlebar) {
        XDestroyWindow(dpy, c->titlebar);
        c->titlebar = 0;
    }
    if (scratchpad_client == c) {
        scratchpad_client = NULL;
    }

    detach_client(c);
    ewmh_update_client_list();
    free(c);

    if (m) {
        Workspace *ws = &m->ws[ws_idx];
        if (!ws->focus) {
            ws->focus = next_client_in_ws(ws, NULL, 0);
        }
        if (ws->focus) {
            set_focus(ws->focus);
        }
        arrange_monitor(m);
    }
}

void kill_client(Client *c)
{
    if (!c) {
        return;
    }

    Atom *protocols = NULL;
    int nprotocols = 0;
    bool supports_delete = false;
    if (XGetWMProtocols(dpy, c->win, &protocols, &nprotocols)) {
        for (int i = 0; i < nprotocols; i++) {
            if (protocols[i] == wm_delete) {
                supports_delete = true;
                break;
            }
        }
    }
    if (protocols) {
        XFree(protocols);
    }
    if (!supports_delete) {
        return;
    }

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = c->win;
    ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete;
    ev.xclient.data.l[1] = CurrentTime;

    XSendEvent(dpy, c->win, False, NoEventMask, &ev);
}
