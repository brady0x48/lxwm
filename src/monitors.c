#include "wm_internal.h"

Monitor *monitor_from_point(int x, int y)
{
    for (int i = 0; i < monitor_count; i++) {
        Monitor *m = &monitors[i];
        if (x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h) {
            return m;
        }
    }
    return monitor_count > 0 ? &monitors[0] : NULL;
}

Monitor *monitor_from_window(Window w)
{
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa)) {
        return selmon;
    }
    return monitor_from_point(wa.x + wa.width / 2, wa.y + wa.height / 2);
}

Monitor *monitor_from_pointer(void)
{
    Window dummy_root, dummy_child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    if (XQueryPointer(dpy, root, &dummy_root, &dummy_child, &root_x, &root_y, &win_x, &win_y,
                      &mask)) {
        return monitor_from_point(root_x, root_y);
    }
    return selmon;
}

void warp_pointer_to_monitor(Monitor *m)
{
    if (!m || !dpy) {
        return;
    }

    Window dummy_root, dummy_child;
    int root_x = 0, root_y = 0, win_x, win_y;
    unsigned int mask;
    int px = m->x + m->w / 2;
    int py = m->y + (m->h - m->bar_h) / 2;

    if (XQueryPointer(dpy, root, &dummy_root, &dummy_child, &root_x, &root_y, &win_x, &win_y,
                      &mask)) {
        Monitor *src = monitor_from_point(root_x, root_y);
        if (src && src != m) {
            int src_h = src->h - src->bar_h;
            int dst_h = m->h - m->bar_h;
            if (src_h < 1) {
                src_h = src->h;
            }
            if (dst_h < 1) {
                dst_h = m->h;
            }
            int rel_x = clamp_int(root_x - src->x, 0, src->w - 1);
            int rel_y = clamp_int(root_y - src->y, 0, src_h - 1);
            px = m->x + clamp_int(rel_x, 0, m->w - 1);
            py = m->y + clamp_int(rel_y, 0, dst_h - 1);
        }
    }

    if (py < m->y) {
        py = m->y + m->h / 2;
    }

    XWarpPointer(dpy, None, root, 0, 0, 0, 0, px, py);
    XFlush(dpy);
}

void setup_monitors(void)
{
    monitor_count = 0;

#if HAVE_XINERAMA
    int event_base, error_base;
    if (XineramaQueryExtension(dpy, &event_base, &error_base) && XineramaIsActive(dpy)) {
        int n = 0;
        XineramaScreenInfo *info = XineramaQueryScreens(dpy, &n);
        if (info && n > 0) {
            if (n > MAX_MONITORS) {
                n = MAX_MONITORS;
            }
            for (int i = 0; i < n; i++) {
                monitors[i].id = i;
                monitors[i].x = info[i].x_org;
                monitors[i].y = info[i].y_org;
                monitors[i].w = info[i].width;
                monitors[i].h = info[i].height;
                monitors[i].bar_h = bar_enabled ? BAR_HEIGHT : 0;
                monitors[i].bar = 0;
                monitors[i].current_ws = 0;
                monitors[i].ws_mask = all_workspaces_mask();
                monitor_count++;
            }
            XFree(info);
        }
    }
#endif

    if (monitor_count == 0) {
        Monitor *m = &monitors[0];
        m->id = 0;
        m->x = 0;
        m->y = 0;
        m->w = DisplayWidth(dpy, screen_num);
        m->h = DisplayHeight(dpy, screen_num);
        m->bar_h = bar_enabled ? BAR_HEIGHT : 0;
        m->bar = 0;
        m->current_ws = 0;
        m->ws_mask = all_workspaces_mask();
        monitor_count = 1;
    }

    selmon = &monitors[0];
    sync_bar_windows();
    apply_monitor_workspace_config();
}

void sync_bar_windows(void)
{
    for (int i = 0; i < monitor_count; i++) {
        Monitor *m = &monitors[i];
        m->bar_h = bar_enabled ? BAR_HEIGHT : 0;

        if (!bar_enabled) {
            if (m->bar) {
                XDestroyWindow(dpy, m->bar);
                m->bar = 0;
            }
            continue;
        }

        if (!m->bar) {
            XSetWindowAttributes attrs;
            attrs.override_redirect = True;
            m->bar = XCreateWindow(dpy, root, m->x, m->y + m->h - m->bar_h, (unsigned int)m->w,
                                   (unsigned int)m->bar_h, 0, DefaultDepth(dpy, screen_num),
                                   CopyFromParent, DefaultVisual(dpy, screen_num),
                                   CWOverrideRedirect, &attrs);
            XSetWindowBackground(dpy, m->bar, color_bg);
            XSelectInput(dpy, m->bar, ExposureMask | ButtonPressMask);
            XMapRaised(dpy, m->bar);
        } else {
            XMoveResizeWindow(dpy, m->bar, m->x, m->y + m->h - m->bar_h, (unsigned int)m->w,
                              (unsigned int)m->bar_h);
            XMapRaised(dpy, m->bar);
        }
    }
}
