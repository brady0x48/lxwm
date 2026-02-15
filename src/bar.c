#include "wm_internal.h"

static void format_module_text(const char *templ, const char *fallback_value,
                               const char *const *keys, const char *const *values, size_t nkeys,
                               char *out, size_t outsz)
{
    apply_template_format(templ, keys, values, nkeys, out, outsz);
    if (!strchr(out, '{') && !strchr(out, '}')) {
        return;
    }
    if (fallback_value && fallback_value[0]) {
        snprintf(out, outsz, "%s", fallback_value);
    } else if (outsz > 0) {
        out[0] = '\0';
    }
}

void draw_bar(Monitor *m)
{
    if (!bar_enabled || !m || !m->bar) {
        return;
    }

    update_system_stats();

    Drawable canvas = m->bar;
    Pixmap back = XCreatePixmap(dpy, m->bar, (unsigned int)m->w, (unsigned int)m->bar_h,
                                (unsigned int)DefaultDepth(dpy, screen_num));
    if (back) {
        canvas = back;
    }

    XSetForeground(dpy, bar_gc, color_bg);
    XFillRectangle(dpy, canvas, bar_gc, 0, 0, (unsigned int)m->w, (unsigned int)m->bar_h);

    int ascent = wm_xft_font ? wm_xft_font->ascent : (bar_font ? bar_font->ascent : 10);
    int descent = wm_xft_font ? wm_xft_font->descent : (bar_font ? bar_font->descent : 2);
    int text_y = (m->bar_h + ascent - descent) / 2;

    int box_w = bar_ws_slot_w;
    int box_h = m->bar_h - (2 * bar_ws_pad_y);
    if (box_h < 1) {
        box_h = 1;
    }
    int start_x = bar_ws_pad_x;
    int slot = 0;
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        if (!monitor_allows_workspace(m, i)) {
            continue;
        }
        int x = start_x + slot * (box_w + bar_ws_gap);
        int has_urgent = 0;
        for (Client *c = clients; c; c = c->next) {
            if (c->mapped && c->mon == m && c->workspace == i && c->is_urgent) {
                has_urgent = 1;
                break;
            }
        }
        unsigned long bg = color_ws_inactive;
        if (i == m->current_ws && m == selmon) {
            bg = color_ws_active;
        } else if (has_urgent && i != m->current_ws) {
            bg = color_ws_urgent;
        }
        XSetForeground(dpy, bar_gc, bg);
        XFillRectangle(dpy, canvas, bar_gc, x, bar_ws_pad_y, (unsigned int)box_w,
                       (unsigned int)box_h);

        char ws_label[32];
        if (workspace_names[i][0]) {
            strncpy(ws_label, workspace_names[i], sizeof(ws_label) - 1);
            ws_label[sizeof(ws_label) - 1] = '\0';
        } else {
            snprintf(ws_label, sizeof(ws_label), "%d", i + 1);
        }
        XSetForeground(dpy, bar_gc, color_fg);
        int tw = text_width_px(ws_label);
        int tx = x + (box_w - tw) / 2;
        draw_text(canvas, tx, text_y, ws_label);
        slot++;
    }

    int mod_count = 0;
    char mod_chunks[BAR_MOD_COUNT][128];
    unsigned long mod_colors[BAR_MOD_COUNT];
    int mod_widths[BAR_MOD_COUNT];
    for (int i = 0; i < BAR_MOD_COUNT; i++) {
        mod_chunks[i][0] = '\0';
        mod_colors[i] = color_fg;
        mod_widths[i] = bar_module_slot_w;
    }
    for (int i = 0; i < bar_modules_count && mod_count < BAR_MOD_COUNT; i++) {
        char value[64];
        value[0] = '\0';
        int mod = bar_modules_order[i];
        if (mod == BAR_MOD_CPU) {
            snprintf(value, sizeof(value), "%.0f%%", cpu_usage_pct);
            const char *keys[] = {"cpu", "percent"};
            const char *vals[] = {value, value};
            format_module_text(bar_cpu_fmt, value, keys, vals, 2, mod_chunks[mod_count],
                               sizeof(mod_chunks[mod_count]));
            mod_colors[mod_count] = bar_cpu_color;
        } else if (mod == BAR_MOD_MEM) {
            snprintf(value, sizeof(value), "%d%%", mem_usage_pct);
            const char *keys[] = {"mem", "percent"};
            const char *vals[] = {value, value};
            format_module_text(bar_mem_fmt, value, keys, vals, 2, mod_chunks[mod_count],
                               sizeof(mod_chunks[mod_count]));
            mod_colors[mod_count] = bar_mem_color;
        } else if (mod == BAR_MOD_TIME) {
            time_t now = time(NULL);
            struct tm tmv;
            if (now != (time_t)-1 && localtime_r(&now, &tmv)) {
                strftime(value, sizeof(value), bar_time_fmt, &tmv);
            } else {
                snprintf(value, sizeof(value), "--:--");
            }
            snprintf(mod_chunks[mod_count], sizeof(mod_chunks[mod_count]), "%s", value);
            mod_colors[mod_count] = bar_time_color;
        } else if (mod == BAR_MOD_WIFI) {
            if (wifi_strength_pct >= 0) {
                snprintf(value, sizeof(value), "%d%%", wifi_strength_pct);
            } else {
                snprintf(value, sizeof(value), "--");
            }
            char state[8];
            snprintf(state, sizeof(state), "%s", wifi_link_up ? "up" : "down");
            const char *keys[] = {"wifi", "strength", "state"};
            const char *vals[] = {value, value, state};
            format_module_text(bar_wifi_fmt, value, keys, vals, 3, mod_chunks[mod_count],
                               sizeof(mod_chunks[mod_count]));
            mod_colors[mod_count] = wifi_link_up ? bar_wifi_color : bar_wifi_down_color;
        } else if (mod == BAR_MOD_ETHERNET) {
            snprintf(value, sizeof(value), "%s", ethernet_link_up ? ethernet_iface : "--");
            char state[8];
            snprintf(state, sizeof(state), "%s", ethernet_link_up ? "up" : "down");
            const char *keys[] = {"ethernet", "iface", "state"};
            const char *vals[] = {value, value, state};
            format_module_text(bar_ethernet_fmt, value, keys, vals, 3, mod_chunks[mod_count],
                               sizeof(mod_chunks[mod_count]));
            mod_colors[mod_count] = ethernet_link_up ? bar_ethernet_color : bar_ethernet_down_color;
        } else if (mod == BAR_MOD_IP) {
            snprintf(value, sizeof(value), "%s", ip_addr[0] ? ip_addr : "-");
            char state[8];
            snprintf(state, sizeof(state), "%s", ip_link_up ? "up" : "down");
            const char *keys[] = {"ip", "addr", "state"};
            const char *vals[] = {value, value, state};
            format_module_text(bar_ip_fmt, value, keys, vals, 3, mod_chunks[mod_count],
                               sizeof(mod_chunks[mod_count]));
            mod_colors[mod_count] = ip_link_up ? bar_ip_color : bar_ip_down_color;
        } else if (mod == BAR_MOD_BATTERY) {
            if (battery_pct >= 0) {
                snprintf(value, sizeof(value), "%d%%", battery_pct);
            } else {
                snprintf(value, sizeof(value), "--");
            }
            const char *keys[] = {"battery", "percent"};
            const char *vals[] = {value, value};
            format_module_text(bar_battery_fmt, value, keys, vals, 2, mod_chunks[mod_count],
                               sizeof(mod_chunks[mod_count]));
            mod_colors[mod_count] = bar_battery_color;
        } else if (mod == BAR_MOD_CPUTEMP) {
            if (cpu_temp_c >= 0) {
                snprintf(value, sizeof(value), "%dC", cpu_temp_c);
            } else {
                snprintf(value, sizeof(value), "--");
            }
            const char *keys[] = {"temp", "cputemp"};
            const char *vals[] = {value, value};
            format_module_text(bar_cputemp_fmt, value, keys, vals, 2, mod_chunks[mod_count],
                               sizeof(mod_chunks[mod_count]));
            mod_colors[mod_count] = bar_cputemp_color;
        } else if (mod == BAR_MOD_DISK) {
            char used[32];
            char freev[32];
            char total[32];
            char used_total[64];
            snprintf(used, sizeof(used), "%.1fG", disk_used_gb);
            snprintf(freev, sizeof(freev), "%.1fG", disk_avail_gb);
            snprintf(total, sizeof(total), "%.1fG", disk_total_gb);
            snprintf(used_total, sizeof(used_total), "%s/%s", used, total);
            if (!strcmp(bar_disk_mode, "used")) {
                snprintf(value, sizeof(value), "%s", used);
            } else if (!strcmp(bar_disk_mode, "free") || !strcmp(bar_disk_mode, "avail")) {
                snprintf(value, sizeof(value), "%s", freev);
            } else {
                snprintf(value, sizeof(value), "%s", used_total);
            }
            const char *keys[] = {"disk", "used", "free", "avail", "total", "used_total"};
            const char *vals[] = {value, used, freev, freev, total, used_total};
            format_module_text(bar_disk_fmt, value, keys, vals, 6, mod_chunks[mod_count],
                               sizeof(mod_chunks[mod_count]));
            mod_colors[mod_count] = bar_disk_color;
        }
        if (mod_chunks[mod_count][0]) {
            int tw = text_width_px(mod_chunks[mod_count]);
            int min_w = tw + 12;
            if (min_w < bar_module_slot_w) {
                min_w = bar_module_slot_w;
            }
            mod_widths[mod_count] = min_w;
            mod_count++;
        }
    }
    int right_w = 0;
    if (mod_count > 0) {
        for (int i = 0; i < mod_count; i++) {
            right_w += mod_widths[i];
        }
        if (mod_count > 1) {
            right_w += (mod_count - 1) * bar_module_gap;
        }
    }
    int right_zone_w = bar_right_zone_min_w;
    int min_for_text = right_w + (2 * bar_ws_pad_x);
    if (right_zone_w < min_for_text) {
        right_zone_w = min_for_text;
    }
    if (right_zone_w > m->w) {
        right_zone_w = m->w;
    }
    int right_zone_x = m->w - right_zone_w;
    if (right_zone_x < 0) {
        right_zone_x = 0;
    }
    int right_x = right_zone_x + (right_zone_w - right_w) / 2;
    if (right_x < right_zone_x + bar_ws_pad_x) {
        right_x = right_zone_x + bar_ws_pad_x;
    }

    int left_w = (slot > 0) ? (start_x + slot * box_w + (slot - 1) * bar_ws_gap) : 0;
    int mid_x = left_w + 2;
    int mid_w = right_zone_x - mid_x - 2;
    if (mid_w < bar_divider_min_w) {
        mid_w = bar_divider_min_w;
    }
    if (mid_x + mid_w > right_zone_x && right_zone_x > mid_x) {
        mid_w = right_zone_x - mid_x;
    }
    if (mid_w > 0 && mid_x < m->w) {
        if (mid_x + mid_w > m->w) {
            mid_w = m->w - mid_x;
        }
        XSetForeground(dpy, bar_gc, color_bar_mid);
        XFillRectangle(dpy, canvas, bar_gc, mid_x, 0, (unsigned int)mid_w, (unsigned int)m->bar_h);
    }

    if (mod_count > 0) {
        int draw_x = right_x;
        for (int i = 0; i < mod_count; i++) {
            int cell_x = draw_x;
            int cell_w = mod_widths[i];
            int tw = text_width_px(mod_chunks[i]);
            int tx = cell_x + (cell_w - tw) / 2;
            XSetForeground(dpy, bar_gc, mod_colors[i]);
            draw_text(canvas, tx, text_y, mod_chunks[i]);
            draw_x += cell_w;
            if (i < mod_count - 1) {
                if (bar_module_sep[0]) {
                    int sep_w = text_width_px(bar_module_sep);
                    int sep_x = draw_x + (bar_module_gap - sep_w) / 2;
                    XSetForeground(dpy, bar_gc, color_fg);
                    draw_text(canvas, sep_x, text_y, bar_module_sep);
                }
                draw_x += bar_module_gap;
            }
        }
    }

    if (back) {
        XCopyArea(dpy, back, m->bar, bar_gc, 0, 0, (unsigned int)m->w, (unsigned int)m->bar_h, 0,
                  0);
        XFreePixmap(dpy, back);
    }
}

void map_workspace_floating(Monitor *m, int ws_idx, int visible)
{
    if (!m || ws_idx < 0 || ws_idx >= MAX_WORKSPACES) {
        return;
    }
    for (Client *c = clients; c; c = c->next) {
        if (!c->mapped || !c->is_floating || c->mon != m || c->workspace != ws_idx) {
            continue;
        }
        if (visible) {
            c->is_hidden = 0;
            XMapWindow(dpy, c->titlebar);
            XMapWindow(dpy, c->win);
            draw_titlebar(c);
        } else {
            c->is_hidden = 1;
            c->ignore_unmap++;
            XUnmapWindow(dpy, c->titlebar);
            XUnmapWindow(dpy, c->win);
        }
    }
}

void grab_client_buttons(Window w)
{
    if (!w) {
        return;
    }
    unsigned int ignored[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    XUngrabButton(dpy, AnyButton, AnyModifier, w);
    for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) {
        XGrabButton(dpy, AnyButton, ignored[i], w, True, ButtonPressMask, GrabModeSync,
                    GrabModeSync, None, None);
        XGrabButton(dpy, Button1, primary_mod_mask | ignored[i], w, True, ButtonPressMask,
                    GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(dpy, Button3, primary_mod_mask | ignored[i], w, True, ButtonPressMask,
                    GrabModeAsync, GrabModeAsync, None, None);
    }
}

int bar_workspace_from_x(Monitor *m, int x)
{
    int box_w = bar_ws_slot_w;
    int start_x = bar_ws_pad_x;
    int slot = 0;
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        if (!monitor_allows_workspace(m, i)) {
            continue;
        }
        int bx = start_x + slot * (box_w + bar_ws_gap);
        if (x >= bx && x < bx + box_w) {
            return i;
        }
        slot++;
    }
    return -1;
}
