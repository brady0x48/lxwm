#include "wm_internal.h"

static int drag_last_root_x;
static int drag_last_root_y;
static int drag_resize_total_dx;
static int drag_resize_total_dy;
static int drag_resize_axis;
static Node *drag_resize_node;
static float drag_resize_start_ratio;
static Window drag_resize_line_win;

static Node *find_resize_node_for_drag(Node *leaf, int want_vertical_split)
{
    for (Node *n = leaf ? leaf->parent : NULL; n; n = n->parent) {
        if (n->split_vertical == want_vertical_split) {
            return n;
        }
    }
    return NULL;
}

static void ensure_drag_resize_line_window(void)
{
    if (drag_resize_line_win) {
        return;
    }
    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.override_redirect = True;
    attrs.background_pixel = color_focus;
    drag_resize_line_win =
        XCreateWindow(dpy, root, 0, 0, 2, 2, 0, DefaultDepth(dpy, screen_num), CopyFromParent,
                      DefaultVisual(dpy, screen_num), CWOverrideRedirect | CWBackPixel, &attrs);
}

static void hide_drag_resize_line(void)
{
    if (!drag_resize_line_win) {
        return;
    }
    XUnmapWindow(dpy, drag_resize_line_win);
}

static void show_drag_resize_line_for_tiled(Client *c)
{
    if (!c || !c->leaf) {
        return;
    }

    if (drag_resize_axis == 0) {
        int absx = abs(drag_resize_total_dx);
        int absy = abs(drag_resize_total_dy);
        drag_resize_axis = (absx >= absy) ? 1 : 2;
    }

    int want_vertical = (drag_resize_axis == 1) ? 1 : 0;
    if (!drag_resize_node) {
        drag_resize_node = find_resize_node_for_drag(c->leaf, want_vertical);
        if (!drag_resize_node) {
            if (drag_resize_axis == 1) {
                drag_resize_axis = 2;
                want_vertical = 0;
            } else {
                drag_resize_axis = 1;
                want_vertical = 1;
            }
            drag_resize_node = find_resize_node_for_drag(c->leaf, want_vertical);
        }
        if (drag_resize_node) {
            drag_resize_start_ratio = drag_resize_node->ratio;
        }
    }

    if (!drag_resize_node) {
        return;
    }

    float ratio = drag_resize_start_ratio;
    if (drag_resize_axis == 1 && drag_resize_node->w > 0) {
        ratio += (float)drag_resize_total_dx / (float)drag_resize_node->w;
    } else if (drag_resize_axis == 2 && drag_resize_node->h > 0) {
        ratio += (float)drag_resize_total_dy / (float)drag_resize_node->h;
    }

    if (ratio < 0.1f) {
        ratio = 0.1f;
    } else if (ratio > 0.9f) {
        ratio = 0.9f;
    }

    ensure_drag_resize_line_window();
    if (!drag_resize_line_win) {
        return;
    }

    if (drag_resize_axis == 1) {
        int x = drag_resize_node->x + (int)((float)drag_resize_node->w * ratio);
        int y = drag_resize_node->y;
        int h = drag_resize_node->h;
        if (h < 2) {
            h = 2;
        }
        XMoveResizeWindow(dpy, drag_resize_line_win, x - 1, y, 2, (unsigned int)h);
    } else {
        int x = drag_resize_node->x;
        int y = drag_resize_node->y + (int)((float)drag_resize_node->h * ratio);
        int w = drag_resize_node->w;
        if (w < 2) {
            w = 2;
        }
        XMoveResizeWindow(dpy, drag_resize_line_win, x, y - 1, (unsigned int)w, 2);
    }

    XMapRaised(dpy, drag_resize_line_win);
}

static void close_ipc_fd(void)
{
    if (ipc_fd >= 0) {
        close(ipc_fd);
        ipc_fd = -1;
    }
}

static void cleanup_ipc_socket(void)
{
    close_ipc_fd();
    if (ipc_socket_path[0]) {
        unlink(ipc_socket_path);
        ipc_socket_path[0] = '\0';
    }
}

static void build_ipc_socket_path(void)
{
    const char *disp = getenv("DISPLAY");
    if (!disp || !*disp) {
        disp = ":0";
    }

    char disp_tag[64];
    size_t n = 0;
    for (size_t i = 0; disp[i] && n + 1 < sizeof(disp_tag); i++) {
        char ch = disp[i];
        if (isalnum((unsigned char)ch)) {
            disp_tag[n++] = ch;
        } else {
            disp_tag[n++] = '_';
        }
    }
    disp_tag[n] = '\0';

    snprintf(ipc_socket_path, sizeof(ipc_socket_path), "/tmp/lxwm-%u-%s.sock",
             (unsigned int)getuid(), disp_tag);
}

static void setup_ipc_socket(void)
{
    cleanup_ipc_socket();
    if (!ipc_enabled) {
        return;
    }

    build_ipc_socket_path();
    ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_fd < 0) {
        ipc_socket_path[0] = '\0';
        return;
    }

    int flags = fcntl(ipc_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(ipc_fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ipc_socket_path);

    unlink(ipc_socket_path);
    if (bind(ipc_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        cleanup_ipc_socket();
        return;
    }

    if (listen(ipc_fd, 8) < 0) {
        cleanup_ipc_socket();
    }
}

void refresh_ipc_state(void)
{
    if (ipc_enabled) {
        if (ipc_fd < 0) {
            setup_ipc_socket();
        }
    } else {
        cleanup_ipc_socket();
    }
}

static void ipc_write_reply(int cfd, const char *reply)
{
    if (cfd < 0 || !reply) {
        return;
    }
    (void)write(cfd, reply, strlen(reply));
}

static Monitor *ipc_target_monitor(void)
{
    Monitor *m = monitor_from_pointer();
    if (!m) {
        m = selmon;
    }
    if (!m && monitor_count > 0) {
        m = &monitors[0];
    }
    return m;
}

static bool ipc_focus_action(const char *dir)
{
    if (!dir || !*dir) {
        return false;
    }
    Monitor *m = ipc_target_monitor();
    if (!m) {
        return false;
    }
    Workspace *ws = &m->ws[m->current_ws];
    Client *focus = current_focused_client(m);
    Client *next = NULL;

    if (!strcasecmp(dir, "left")) {
        next = focus_direction(m, focus, -1, 0);
    } else if (!strcasecmp(dir, "right")) {
        next = focus_direction(m, focus, 1, 0);
    } else if (!strcasecmp(dir, "up")) {
        next = focus_direction(m, focus, 0, -1);
    } else if (!strcasecmp(dir, "down")) {
        next = focus_direction(m, focus, 0, 1);
    } else if (!strcasecmp(dir, "next")) {
        next = next_client_in_ws(ws, focus, 0);
    } else if (!strcasecmp(dir, "prev")) {
        next = next_client_in_ws(ws, focus, 1);
    } else {
        return false;
    }

    if (next) {
        set_focus(next);
        return true;
    }
    return false;
}

static bool ipc_parse_bool(const char *value, int fallback)
{
    if (!value || !*value) {
        return fallback ? true : false;
    }
    if (!strcasecmp(value, "1") || !strcasecmp(value, "on") || !strcasecmp(value, "true") ||
        !strcasecmp(value, "yes")) {
        return true;
    }
    if (!strcasecmp(value, "0") || !strcasecmp(value, "off") || !strcasecmp(value, "false") ||
        !strcasecmp(value, "no")) {
        return false;
    }
    return fallback ? true : false;
}

static void ipc_handle_command(char *line, int cfd)
{
    if (!line) {
        ipc_write_reply(cfd, "error invalid\n");
        return;
    }

    while (*line && isspace((unsigned char)*line)) {
        line++;
    }
    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    if (!*line) {
        ipc_write_reply(cfd, "error empty\n");
        return;
    }

    if (!strcasecmp(line, "reload")) {
        reload_runtime_config();
        ipc_write_reply(cfd, "ok\n");
        return;
    }
    if (!strcasecmp(line, "quit")) {
        running = 0;
        ipc_write_reply(cfd, "ok\n");
        return;
    }
    if (!strcasecmp(line, "restart")) {
        wm_restart_requested = 1;
        running = 0;
        ipc_write_reply(cfd, "ok\n");
        return;
    }

    if (!strncasecmp(line, "ws ", 3)) {
        int ws = atoi(line + 3) - 1;
        Monitor *m = ipc_target_monitor();
        if (!m || ws < 0 || ws >= MAX_WORKSPACES) {
            ipc_write_reply(cfd, "error ws\n");
            return;
        }
        switch_workspace(m, ws);
        ipc_write_reply(cfd, "ok\n");
        return;
    }

    if (!strncasecmp(line, "focus ", 6)) {
        if (ipc_focus_action(line + 6)) {
            ipc_write_reply(cfd, "ok\n");
        } else {
            ipc_write_reply(cfd, "error focus\n");
        }
        return;
    }

    if (!strncasecmp(line, "spawn ", 6)) {
        char *cmd = line + 6;
        while (*cmd && isspace((unsigned char)*cmd)) {
            cmd++;
        }
        if (!*cmd) {
            ipc_write_reply(cfd, "error spawn\n");
            return;
        }
        spawn_target_monitor = ipc_target_monitor();
        spawn_target_time = time(NULL);
        if (spawn_target_monitor) {
            selmon = spawn_target_monitor;
        }
        spawn_command(cmd);
        ipc_write_reply(cfd, "ok\n");
        return;
    }

    if (!strncasecmp(line, "bar ", 4)) {
        bool enable = ipc_parse_bool(line + 4, bar_enabled);
        bar_enabled = enable ? 1 : 0;
        sync_bar_windows();
        arrange_all();
        ewmh_update_workarea();
        ipc_write_reply(cfd, "ok\n");
        return;
    }

    ipc_write_reply(cfd, "error unknown\n");
}

static void ipc_drain_connections(void)
{
    if (ipc_fd < 0) {
        return;
    }

    for (;;) {
        int cfd = accept(ipc_fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            cleanup_ipc_socket();
            if (ipc_enabled) {
                setup_ipc_socket();
            }
            return;
        }

        char buf[1024];
        ssize_t n = read(cfd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            char *newline = strchr(buf, '\n');
            if (newline) {
                *newline = '\0';
            }
            ipc_handle_command(buf, cfd);
        } else {
            ipc_write_reply(cfd, "error read\n");
        }
        close(cfd);
    }
}

void cleanup(void)
{
    cleanup_ipc_socket();
    hide_drag_resize_line();
    if (drag_resize_line_win) {
        XDestroyWindow(dpy, drag_resize_line_win);
        drag_resize_line_win = 0;
    }

    for (Client *c = clients; c;) {
        Client *next = c->next;
        XSetWindowBorderWidth(dpy, c->win, c->old_bw);
        if (c->titlebar) {
            XDestroyWindow(dpy, c->titlebar);
            c->titlebar = 0;
        }
        free(c);
        c = next;
    }

    for (int i = 0; i < monitor_count; i++) {
        for (int w = 0; w < MAX_WORKSPACES; w++) {
            free_tree(monitors[i].ws[w].root);
            monitors[i].ws[w].root = NULL;
            monitors[i].ws[w].focus = NULL;
        }
        if (monitors[i].bar) {
            XDestroyWindow(dpy, monitors[i].bar);
            monitors[i].bar = 0;
        }
    }

    if (bar_font) {
        XFreeFont(dpy, bar_font);
        bar_font = NULL;
    }
    if (wm_xft_draw) {
        XftDrawDestroy(wm_xft_draw);
        wm_xft_draw = NULL;
    }
    if (wm_xft_font) {
        XftFontClose(dpy, wm_xft_font);
        wm_xft_font = NULL;
    }
    if (wm_xft_color_valid) {
        XftColorFree(dpy, DefaultVisual(dpy, screen_num), DefaultColormap(dpy, screen_num),
                     &wm_xft_color);
        wm_xft_color_valid = 0;
    }
    if (bar_gc) {
        XFreeGC(dpy, bar_gc);
        bar_gc = 0;
    }

    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    XSync(dpy, False);
}

void ewmh_set_active_window(Window w)
{
    unsigned long val = (unsigned long)w;
    XChangeProperty(dpy, root, net_active_window, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&val, 1);
}

void ewmh_set_current_desktop(unsigned long d)
{
    XChangeProperty(dpy, root, net_current_desktop, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&d, 1);
}

void ewmh_update_client_list(void)
{
    int n = 0;
    for (Client *c = clients; c; c = c->next) {
        n++;
    }
    if (n <= 0) {
        XDeleteProperty(dpy, root, net_client_list);
        XDeleteProperty(dpy, root, net_client_list_stacking);
        return;
    }

    Window *wins = ecalloc((size_t)n, sizeof(Window));
    int i = 0;
    for (Client *c = clients; c; c = c->next) {
        wins[i++] = c->win;
    }
    XChangeProperty(dpy, root, net_client_list, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)wins, n);
    XChangeProperty(dpy, root, net_client_list_stacking, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)wins, n);
    free(wins);
}

void ewmh_update_workarea(void)
{
    if (monitor_count <= 0) {
        return;
    }

    unsigned long work[4 * MAX_WORKSPACES];
    Monitor *base = &monitors[0];
    unsigned long wx = (unsigned long)base->x;
    unsigned long wy = (unsigned long)base->y;
    unsigned long ww = (unsigned long)base->w;
    unsigned long wh = (unsigned long)((base->h > base->bar_h) ? (base->h - base->bar_h) : base->h);

    for (int i = 0; i < MAX_WORKSPACES; i++) {
        work[i * 4 + 0] = wx;
        work[i * 4 + 1] = wy;
        work[i * 4 + 2] = ww;
        work[i * 4 + 3] = wh;
    }
    XChangeProperty(dpy, root, net_workarea, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)work, 4 * MAX_WORKSPACES);
}

static void scan_existing_windows(void)
{
    Window parent;
    Window *wins = NULL;
    unsigned int nwins = 0;

    if (!XQueryTree(dpy, root, &root, &parent, &wins, &nwins)) {
        return;
    }

    for (unsigned int i = 0; i < nwins; i++) {
        Window w = wins[i];
        XWindowAttributes wa;
        if (!XGetWindowAttributes(dpy, w, &wa)) {
            continue;
        }
        if (wa.override_redirect || wa.map_state != IsViewable) {
            continue;
        }
        if (!should_manage(w)) {
            if (window_is_dock(w)) {
                XRaiseWindow(dpy, w);
            }
            continue;
        }
        manage(w);
    }

    if (wins) {
        XFree(wins);
    }
}

static void on_client_message(XClientMessageEvent *e)
{
    if (!e) {
        return;
    }

    Client *c = find_client(e->window);
    if (!c) {
        return;
    }

    if (e->message_type == net_active_window) {
        set_focus(c);
        return;
    }

    if (e->message_type == net_wm_state) {
        Atom a1 = (Atom)e->data.l[1];
        Atom a2 = (Atom)e->data.l[2];
        if (a1 == net_wm_state_fullscreen || a2 == net_wm_state_fullscreen) {
            long action = e->data.l[0];
            if (action == 2 || (action == 1 && !c->is_fullscreen) ||
                (action == 0 && c->is_fullscreen)) {
                toggle_fullscreen(c);
            }
        }
    }
}

static void on_key_press(XKeyEvent *e)
{
    unsigned int st = normalize_mods(e->state);

    for (int i = 0; i < keybind_count; i++) {
        KeyBind *kb = &keybinds[i];
        if (e->keycode == kb->keycode && st == kb->mod) {
            execute_action(kb);
            return;
        }
    }
}

static void on_map_request(XMapRequestEvent *e)
{
    if (!should_manage(e->window)) {
        XMapRaised(dpy, e->window);
        if (window_is_dock(e->window)) {
            XRaiseWindow(dpy, e->window);
        }
        return;
    }
    manage(e->window);
}

static void on_configure_request(XConfigureRequestEvent *e)
{
    Client *c = find_client(e->window);

    if (c && c->mon && (e->value_mask & CWX) && (e->value_mask & CWY) && (e->value_mask & CWWidth) &&
        (e->value_mask & CWHeight)) {
        int req_x = e->x;
        int req_y = e->y;
        int req_w = e->width;
        int req_h = e->height;

        int mon_x = c->mon->x;
        int mon_y = c->mon->y;
        int mon_w = c->mon->w;
        int mon_h = c->mon->h;

        int near_fullscreen =
            (abs(req_x - mon_x) <= 2) && (abs(req_y - mon_y) <= 2) && (abs(req_w - mon_w) <= 4) &&
            ((abs(req_h - mon_h) <= 4) || (abs(req_h - (mon_h - c->mon->bar_h)) <= 4));

        if (near_fullscreen && !c->is_fullscreen) {
            toggle_fullscreen(c);
            return;
        }
    }

    if (c && !c->is_floating) {
        XConfigureEvent ce;
        memset(&ce, 0, sizeof(ce));
        ce.type = ConfigureNotify;
        ce.display = dpy;
        ce.event = c->win;
        ce.window = c->win;
        ce.x = c->x;
        ce.y = c->y;
        ce.width = c->w;
        ce.height = c->h;
        ce.border_width = BORDER_WIDTH;
        ce.above = None;
        ce.override_redirect = False;
        XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
        return;
    }

    if (c && c->is_floating) {
        int nx = c->x;
        int ny = c->y;
        int nw = c->w;
        int nh = c->h;
        if (e->value_mask & CWX) {
            nx = e->x;
        }
        if (e->value_mask & CWY) {
            ny = e->y;
        }
        if (e->value_mask & CWWidth) {
            nw = e->width;
        }
        if (e->value_mask & CWHeight) {
            nh = e->height;
        }
        move_resize_floating(c, nx, ny, nw, nh);
        return;
    }

    XWindowChanges wc;
    wc.x = e->x;
    wc.y = e->y;
    wc.width = e->width;
    wc.height = e->height;
    wc.border_width = e->border_width;
    wc.sibling = e->above;
    wc.stack_mode = e->detail;

    XConfigureWindow(dpy, e->window, e->value_mask, &wc);
}

static void on_destroy(XDestroyWindowEvent *e)
{
    Client *title_client = find_client_any(e->window);
    if (title_client && title_client->titlebar == e->window) {
        return;
    }
    Client *c = find_client(e->window);
    if (c) {
        unmanage(c, 1);
    }
}

static void on_unmap(XUnmapEvent *e)
{
    Client *title_client = find_client_any(e->window);
    if (title_client && title_client->titlebar == e->window) {
        return;
    }
    Client *c = find_client(e->window);
    if (!c) {
        return;
    }
    if (c->is_hidden) {
        return;
    }
    if (c->ignore_unmap > 0) {
        c->ignore_unmap--;
        return;
    }
    unmanage(c, 0);
}

static void on_button_press(XButtonPressedEvent *e)
{
    for (int i = 0; i < monitor_count; i++) {
        if (e->window == monitors[i].bar) {
            selmon = &monitors[i];
            int ws = bar_workspace_from_x(selmon, e->x);
            if (ws >= 0 && ws < MAX_WORKSPACES) {
                switch_workspace(selmon, ws);
            }
            draw_bar(selmon);
            return;
        }
    }

    Window clicked = e->window;
    if (clicked == root && e->subwindow != None) {
        clicked = e->subwindow;
    }

    Client *c = find_client_from_window(clicked);
    if (!c) {
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        return;
    }
    selmon = c->mon;
    set_focus(c);

    if ((normalize_mods(e->state) & primary_mod_mask) &&
        (e->button == Button1 || e->button == Button3)) {
        if (!c->is_floating && e->button == Button1) {
            set_floating(c, 1);
        }
        drag_client = c;
        drag_mode = (e->button == Button3) ? 2 : 1;
        drag_start_root_x = e->x_root;
        drag_start_root_y = e->y_root;
        drag_last_root_x = e->x_root;
        drag_last_root_y = e->y_root;
        drag_resize_total_dx = 0;
        drag_resize_total_dy = 0;
        drag_resize_axis = 0;
        drag_resize_node = NULL;
        drag_resize_start_ratio = 0.5f;
        drag_start_win_x = c->x;
        drag_start_win_y = c->y;
        drag_start_win_w = c->w;
        drag_start_win_h = c->h;
        XGrabPointer(dpy, root, False, PointerMotionMask | ButtonReleaseMask, GrabModeAsync,
                     GrabModeAsync, None, None, CurrentTime);
        XAllowEvents(dpy, AsyncPointer, CurrentTime);
    } else {
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
    }
}

static void on_enter_notify(XCrossingEvent *e)
{
    if (!e || e->mode != NotifyNormal || e->detail == NotifyInferior) {
        return;
    }

    Client *c = find_client_from_window(e->window);
    if (c && focus_policy == FOCUS_HOVER) {
        set_focus(c);
        return;
    }

    Monitor *m = monitor_from_pointer();
    if (m) {
        selmon = m;
        for (int i = 0; i < monitor_count; i++) {
            draw_bar(&monitors[i]);
        }
    }
}

static void on_property_notify(XPropertyEvent *e)
{
    Client *c = find_client(e->window);
    if (!c) {
        return;
    }
    if (e->atom == net_wm_state) {
        Atom actual = None;
        int format = 0;
        unsigned long nitems = 0;
        unsigned long bytes_after = 0;
        unsigned char *data = NULL;
        int has_fullscreen = 0;

        if (XGetWindowProperty(dpy, c->win, net_wm_state, 0, 64, False, XA_ATOM, &actual, &format,
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

        if (has_fullscreen != c->is_fullscreen) {
            toggle_fullscreen(c);
            return;
        }
    }
    if (e->atom == XA_WM_HINTS) {
        update_client_urgent(c);
    }
    draw_titlebar(c);
    if (c->mon && c->workspace == c->mon->current_ws) {
        draw_bar(c->mon);
    }
}

static void on_motion_notify(XMotionEvent *e)
{
    if (!drag_client || !drag_mode) {
        return;
    }

    if (!drag_client->is_floating && drag_mode == 2) {
        int dx = e->x_root - drag_last_root_x;
        int dy = e->y_root - drag_last_root_y;
        if (dx != 0 || dy != 0) {
            drag_resize_total_dx += dx;
            drag_resize_total_dy += dy;
            show_drag_resize_line_for_tiled(drag_client);
            drag_last_root_x = e->x_root;
            drag_last_root_y = e->y_root;
        }
        return;
    }

    if (!drag_client->is_floating) {
        return;
    }

    int dx = e->x_root - drag_start_root_x;
    int dy = e->y_root - drag_start_root_y;
    if (drag_mode == 1) {
        move_resize_floating(drag_client, drag_start_win_x + dx, drag_start_win_y + dy,
                             drag_start_win_w, drag_start_win_h);
    } else if (drag_mode == 2) {
        move_resize_floating(drag_client, drag_start_win_x, drag_start_win_y, drag_start_win_w + dx,
                             drag_start_win_h + dy);
    }
}

static void on_button_release(XButtonReleasedEvent *e)
{
    (void)e;
    if (!drag_client) {
        return;
    }

    if (!drag_client->is_floating && drag_mode == 2) {
        hide_drag_resize_line();
        if (drag_resize_axis == 1) {
            resize_tiled_client(drag_client, drag_resize_total_dx, 0);
        } else if (drag_resize_axis == 2) {
            resize_tiled_client(drag_client, 0, drag_resize_total_dy);
        }
    }

    XUngrabPointer(dpy, CurrentTime);
    drag_client = NULL;
    drag_mode = 0;
    drag_resize_total_dx = 0;
    drag_resize_total_dy = 0;
    drag_resize_axis = 0;
    drag_resize_node = NULL;
    drag_resize_start_ratio = 0.5f;
}

void run(void)
{
    XEvent ev;
    time_t last_bar_tick = 0;
    struct timespec last_dock_raise;
    struct timespec last_title_refresh;
    clock_gettime(CLOCK_MONOTONIC, &last_dock_raise);
    clock_gettime(CLOCK_MONOTONIC, &last_title_refresh);
    while (running) {
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            switch (ev.type) {
            case KeyPress:
                on_key_press(&ev.xkey);
                break;
            case MapRequest:
                on_map_request(&ev.xmaprequest);
                break;
            case ConfigureRequest:
                on_configure_request(&ev.xconfigurerequest);
                break;
            case DestroyNotify:
                on_destroy(&ev.xdestroywindow);
                break;
            case UnmapNotify:
                on_unmap(&ev.xunmap);
                break;
            case ButtonPress:
                on_button_press(&ev.xbutton);
                break;
            case EnterNotify:
                on_enter_notify(&ev.xcrossing);
                break;
            case Expose: {
                bool handled = false;
                for (int i = 0; i < monitor_count; i++) {
                    if (ev.xexpose.window == monitors[i].bar) {
                        draw_bar(&monitors[i]);
                        handled = true;
                        break;
                    }
                }
                if (!handled) {
                    Client *c = find_client_any(ev.xexpose.window);
                    if (c && c->titlebar == ev.xexpose.window) {
                        draw_titlebar(c);
                    }
                }
            } break;
            case PropertyNotify:
                on_property_notify(&ev.xproperty);
                break;
            case ClientMessage:
                on_client_message(&ev.xclient);
                break;
            case MotionNotify:
                on_motion_notify(&ev.xmotion);
                break;
            case ButtonRelease:
                on_button_release(&ev.xbutton);
                break;
            default:
                break;
            }
        }
        ipc_drain_connections();
        time_t now = time(NULL);
        if (bar_enabled && now != (time_t)-1 && now != last_bar_tick) {
            last_bar_tick = now;
            for (int i = 0; i < monitor_count; i++) {
                draw_bar(&monitors[i]);
            }
        }

        struct timespec cur_ts;
        clock_gettime(CLOCK_MONOTONIC, &cur_ts);
        long dt_ms = (cur_ts.tv_sec - last_title_refresh.tv_sec) * 1000L +
                     (cur_ts.tv_nsec - last_title_refresh.tv_nsec) / 1000000L;
        if (dt_ms >= 100) {
            last_title_refresh = cur_ts;
            for (Client *c = clients; c; c = c->next) {
                if (!c->mapped || c->is_hidden || !c->mon) {
                    continue;
                }
                if (c->workspace != c->mon->current_ws) {
                    continue;
                }
                char cur_title[TITLE_MAX];
                window_title(c->win, cur_title, sizeof(cur_title));
                if (strcmp(cur_title, c->last_title) != 0) {
                    draw_titlebar(c);
                }
            }
        }
        long dock_dt_ms = (cur_ts.tv_sec - last_dock_raise.tv_sec) * 1000L +
                          (cur_ts.tv_nsec - last_dock_raise.tv_nsec) / 1000000L;
        if (dock_dt_ms >= 250) {
            last_dock_raise = cur_ts;
            raise_dock_windows();
        }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 20 * 1000 * 1000;
        nanosleep(&ts, NULL);
    }
}

void setup(void)
{
    screen_num = DefaultScreen(dpy);
    root = RootWindow(dpy, screen_num);
    wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_state_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    net_active_window = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    net_current_desktop = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    net_number_of_desktops = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    net_client_list_stacking = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", False);
    net_workarea = XInternAtom(dpy, "_NET_WORKAREA", False);

    color_focus = get_color("#0b5f5f", WhitePixel(dpy, screen_num));
    color_unfocus = get_color("#2d2d2d", BlackPixel(dpy, screen_num));
    color_bg = get_color("#1b1b1b", BlackPixel(dpy, screen_num));
    color_fg = get_color("#d8d8d8", WhitePixel(dpy, screen_num));
    color_ws_active = get_color("#2f3f3f", color_focus);
    color_ws_inactive = get_color("#232323", color_unfocus);
    color_bar_mid = get_color("#2f3f3f", color_focus);

    bar_gc = XCreateGC(dpy, root, 0, NULL);

    XSetErrorHandler(xerror_start);
    XSelectInput(dpy, root,
                 SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask |
                     PointerMotionMask | PropertyChangeMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);

    load_keybinds();
    load_wm_font();
    grab_keys();
    setup_monitors();
    refresh_ipc_state();
    Atom supported[] = {net_wm_state,
                        net_wm_state_fullscreen,
                        net_active_window,
                        net_current_desktop,
                        net_number_of_desktops,
                        net_client_list,
                        net_client_list_stacking,
                        net_workarea};
    XChangeProperty(dpy, root, net_supported, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)supported, 8);
    unsigned long ndesks = MAX_WORKSPACES;
    XChangeProperty(dpy, root, net_number_of_desktops, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&ndesks, 1);
    ewmh_set_current_desktop((unsigned long)selmon->current_ws);
    ewmh_update_workarea();
    scan_existing_windows();
    ewmh_update_client_list();
    arrange_all();
    run_autostart_commands();
}
