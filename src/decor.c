#include "wm_internal.h"

static void copy_clean_text_n(char *dst, size_t dstsz, const unsigned char *src, size_t src_len)
{
    if (!dst || dstsz == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    size_t out = 0;
    for (size_t i = 0; i < src_len && out + 1 < dstsz; i++) {
        unsigned char ch = src[i];
        if (ch == '\0') {
            break;
        }
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            dst[out++] = ' ';
            continue;
        }
        dst[out++] = (char)ch;
    }
    dst[out] = '\0';
}

static int textprop_to_utf8(const XTextProperty *prop, char *buf, size_t bufsz)
{
    if (!prop || !prop->value || prop->nitems == 0 || !buf || bufsz == 0) {
        return 0;
    }

    char **list = NULL;
    int count = 0;
    if (Xutf8TextPropertyToTextList(dpy, (XTextProperty *)prop, &list, &count) >= Success &&
        list && count > 0 && list[0]) {
        copy_clean_text_n(buf, bufsz, (const unsigned char *)list[0], strlen(list[0]));
        XFreeStringList(list);
        return buf[0] != '\0';
    }
    if (list) {
        XFreeStringList(list);
        list = NULL;
    }

    count = 0;
    if (XmbTextPropertyToTextList(dpy, (XTextProperty *)prop, &list, &count) >= Success && list &&
        count > 0 && list[0]) {
        copy_clean_text_n(buf, bufsz, (const unsigned char *)list[0], strlen(list[0]));
        XFreeStringList(list);
        return buf[0] != '\0';
    }
    if (list) {
        XFreeStringList(list);
    }

    copy_clean_text_n(buf, bufsz, prop->value, prop->nitems);
    return (buf[0] != '\0');
}

char *window_title(Window w, char *buf, size_t bufsz)
{
    if (!buf || bufsz == 0) {
        return NULL;
    }
    buf[0] = '\0';

    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    XTextProperty prop;
    if (XGetTextProperty(dpy, w, &prop, net_wm_name) && prop.value) {
        if (textprop_to_utf8(&prop, buf, bufsz)) {
            XFree(prop.value);
            return buf;
        }
        XFree(prop.value);
    }

    if (XGetWMName(dpy, w, &prop) && prop.value) {
        if (textprop_to_utf8(&prop, buf, bufsz)) {
            XFree(prop.value);
            return buf;
        }
        XFree(prop.value);
    }

    snprintf(buf, bufsz, "0x%lx", w);
    return buf;
}

void update_system_stats(void)
{
    time_t now = time(NULL);
    if (now == (time_t)-1 || now == last_stats_update) {
        return;
    }
    last_stats_update = now;

    FILE *statf = fopen("/proc/stat", "r");
    if (statf) {
        char line[256];
        if (fgets(line, sizeof(line), statf)) {
            unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0,
                               softirq = 0, steal = 0;
            if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &sys,
                       &idle, &iowait, &irq, &softirq, &steal) >= 4) {
                unsigned long long total =
                    user + nice + sys + idle + iowait + irq + softirq + steal;
                unsigned long long idle_total = idle + iowait;
                if (last_cpu_total > 0 && total > last_cpu_total) {
                    unsigned long long dt = total - last_cpu_total;
                    unsigned long long di = idle_total - last_cpu_idle;
                    if (dt > 0 && di <= dt) {
                        cpu_usage_pct = (100.0 * (double)(dt - di)) / (double)dt;
                    }
                }
                last_cpu_total = total;
                last_cpu_idle = idle_total;
            }
        }
        fclose(statf);
    }

    FILE *memf = fopen("/proc/meminfo", "r");
    if (memf) {
        char key[64];
        unsigned long long val = 0;
        char unit[32];
        unsigned long long total_kb = 0;
        unsigned long long avail_kb = 0;
        while (fscanf(memf, "%63s %llu %31s", key, &val, unit) == 3) {
            if (!strcmp(key, "MemTotal:")) {
                total_kb = val;
            } else if (!strcmp(key, "MemAvailable:")) {
                avail_kb = val;
            }
            if (total_kb && avail_kb) {
                break;
            }
        }
        fclose(memf);
        if (total_kb > 0 && avail_kb <= total_kb) {
            mem_usage_pct = (int)(((total_kb - avail_kb) * 100ULL) / total_kb);
        }
    }

    char default_iface[64] = {0};
    FILE *routef = fopen("/proc/net/route", "r");
    if (routef) {
        char line[256];
        if (fgets(line, sizeof(line), routef)) {
        }
        while (fgets(line, sizeof(line), routef)) {
            char iface[64];
            unsigned long dest = 0, flags = 0;
            if (sscanf(line, "%63s %lx %*s %lx", iface, &dest, &flags) == 3) {
                if (dest == 0 && (flags & 0x2)) {
                    snprintf(default_iface, sizeof(default_iface), "%s", iface);
                    break;
                }
            }
        }
        fclose(routef);
    }

    wifi_strength_pct = -1;
    wifi_link_up = 0;
    ethernet_link_up = 0;
    ip_link_up = 0;
    snprintf(ethernet_iface, sizeof(ethernet_iface), "-");
    snprintf(wifi_iface, sizeof(wifi_iface), "-");
    if (default_iface[0]) {
        FILE *wf = fopen("/proc/net/wireless", "r");
        if (wf) {
            char line[256];
            int line_no = 0;
            while (fgets(line, sizeof(line), wf)) {
                line_no++;
                if (line_no <= 2) {
                    continue;
                }
                char iface[64];
                float quality = 0.0f;
                if (sscanf(line, " %63[^:]: %*d %f", iface, &quality) == 2) {
                    if (!strcmp(iface, default_iface)) {
                        int pct = (int)((quality / 70.0f) * 100.0f);
                        wifi_strength_pct = clamp_int(pct, 0, 100);
                        wifi_link_up = 1;
                        snprintf(wifi_iface, sizeof(wifi_iface), "%s", default_iface);
                        break;
                    }
                }
            }
            fclose(wf);
        }
    }

    if (default_iface[0] && !wifi_link_up) {
        char state_path[PATH_MAX];
        snprintf(state_path, sizeof(state_path), "/sys/class/net/%s/operstate", default_iface);
        FILE *sf = fopen(state_path, "r");
        if (sf) {
            char state[32];
            if (fgets(state, sizeof(state), sf) && !strncmp(state, "up", 2)) {
                ethernet_link_up = 1;
                snprintf(ethernet_iface, sizeof(ethernet_iface), "%s", default_iface);
            }
            fclose(sf);
        }
    }

    snprintf(ip_addr, sizeof(ip_addr), "-");
    if (default_iface[0]) {
        struct ifaddrs *ifaddr = NULL;
        if (getifaddrs(&ifaddr) == 0) {
            for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
                    continue;
                }
                if (strcmp(ifa->ifa_name, default_iface) != 0) {
                    continue;
                }
                struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
                if (inet_ntop(AF_INET, &sa->sin_addr, ip_addr, sizeof(ip_addr))) {
                    ip_link_up = 1;
                    break;
                }
            }
            freeifaddrs(ifaddr);
        }
    }

    battery_pct = -1;
    DIR *pwr = opendir("/sys/class/power_supply");
    if (pwr) {
        struct dirent *de;
        while ((de = readdir(pwr)) != NULL) {
            if (de->d_name[0] == '.') {
                continue;
            }
            char type_path[PATH_MAX];
            snprintf(type_path, sizeof(type_path), "/sys/class/power_supply/%s/type", de->d_name);
            FILE *tf = fopen(type_path, "r");
            if (!tf) {
                continue;
            }
            char type[32];
            type[0] = '\0';
            if (!fgets(type, sizeof(type), tf)) {
                fclose(tf);
                continue;
            }
            fclose(tf);
            if (strncmp(type, "Battery", 7) != 0) {
                continue;
            }
            char cap_path[PATH_MAX];
            snprintf(cap_path, sizeof(cap_path), "/sys/class/power_supply/%s/capacity", de->d_name);
            FILE *cf = fopen(cap_path, "r");
            if (!cf) {
                continue;
            }
            int cap = -1;
            if (fscanf(cf, "%d", &cap) == 1) {
                battery_pct = clamp_int(cap, 0, 100);
            }
            fclose(cf);
            break;
        }
        closedir(pwr);
    }

    cpu_temp_c = -1;
    DIR *thermal = opendir("/sys/class/thermal");
    if (thermal) {
        struct dirent *de;
        while ((de = readdir(thermal)) != NULL) {
            if (strncmp(de->d_name, "thermal_zone", 12) != 0) {
                continue;
            }
            char temp_path[PATH_MAX];
            snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/%s/temp", de->d_name);
            FILE *tf = fopen(temp_path, "r");
            if (!tf) {
                continue;
            }
            int milli = -1;
            if (fscanf(tf, "%d", &milli) == 1 && milli > 0) {
                cpu_temp_c = milli / 1000;
                fclose(tf);
                break;
            }
            fclose(tf);
        }
        closedir(thermal);
    }

    disk_used_gb = 0.0;
    disk_avail_gb = 0.0;
    disk_total_gb = 0.0;
    struct statvfs sv;
    if (statvfs(bar_disk_path[0] ? bar_disk_path : "/", &sv) == 0 && sv.f_blocks > 0) {
        unsigned long long bsize = (unsigned long long)sv.f_frsize;
        unsigned long long total = (unsigned long long)sv.f_blocks * bsize;
        unsigned long long free_all = (unsigned long long)sv.f_bfree * bsize;
        unsigned long long avail = (unsigned long long)sv.f_bavail * bsize;
        unsigned long long used = (total >= free_all) ? (total - free_all) : 0;
        const double gib = 1024.0 * 1024.0 * 1024.0;
        disk_total_gb = (double)total / gib;
        disk_avail_gb = (double)avail / gib;
        disk_used_gb = (double)used / gib;
    }
}

static bool is_focused_client(Client *c)
{
    if (!c || !c->mon) {
        return false;
    }
    Monitor *m = c->mon;
    if (c->workspace != m->current_ws) {
        return false;
    }
    return m->ws[c->workspace].focus == c;
}

void draw_titlebar(Client *c)
{
    if (!c || !c->titlebar || !c->mapped) {
        return;
    }

    unsigned long bg = is_focused_client(c) ? color_titlebar_focus : color_titlebar_unfocus;
    XSetForeground(dpy, bar_gc, bg);
    int title_w = c->w + (2 * BORDER_WIDTH);
    if (title_w < 1) {
        title_w = 1;
    }
    XFillRectangle(dpy, c->titlebar, bar_gc, 0, 0, (unsigned int)title_w, TITLEBAR_HEIGHT);

    char title[TITLE_MAX];
    window_title(c->win, title, sizeof(title));
    snprintf(c->last_title, sizeof(c->last_title), "%s", title);
    XSetForeground(dpy, bar_gc, color_fg);
    int ascent = wm_xft_font ? wm_xft_font->ascent : (bar_font ? bar_font->ascent : 10);
    int descent = wm_xft_font ? wm_xft_font->descent : (bar_font ? bar_font->descent : 2);
    int text_y = (TITLEBAR_HEIGHT + ascent - descent) / 2;
    draw_text(c->titlebar, 8, text_y, title);
}

void load_wm_font(void)
{
    if (!dpy || !bar_gc) {
        return;
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

    char pattern[384];
    if (strstr(wm_font_name, ":size=") || strstr(wm_font_name, "-")) {
        snprintf(pattern, sizeof(pattern), "%s", wm_font_name);
    } else {
        snprintf(pattern, sizeof(pattern), "%s:size=%d", wm_font_name, wm_font_size);
    }
    wm_xft_font = XftFontOpenName(dpy, screen_num, pattern);
    if (!wm_xft_font) {
        wm_xft_font = XftFontOpenName(dpy, screen_num, "monospace:size=12");
    }

    Colormap cmap = DefaultColormap(dpy, screen_num);
    XRenderColor xr;
    XColor xc;
    xc.pixel = color_fg;
    XQueryColor(dpy, cmap, &xc);
    xr.red = xc.red;
    xr.green = xc.green;
    xr.blue = xc.blue;
    xr.alpha = 0xFFFF;
    if (XftColorAllocValue(dpy, DefaultVisual(dpy, screen_num), cmap, &xr, &wm_xft_color)) {
        wm_xft_color_valid = 1;
    }

    wm_xft_draw = XftDrawCreate(dpy, root, DefaultVisual(dpy, screen_num), cmap);

    if (bar_font) {
        XFreeFont(dpy, bar_font);
        bar_font = NULL;
    }

    bar_font = XLoadQueryFont(dpy, pattern);
    if (!bar_font) {
        bar_font = XLoadQueryFont(dpy, "fixed");
        if (bar_font) {
            strncpy(wm_font_name, "fixed", sizeof(wm_font_name) - 1);
            wm_font_name[sizeof(wm_font_name) - 1] = '\0';
        }
    }
    if (bar_font) {
        XSetFont(dpy, bar_gc, bar_font->fid);
    }
}

int text_width_px(const char *s)
{
    if (!s) {
        return 0;
    }
    if (wm_xft_font) {
        XGlyphInfo ext;
        XftTextExtentsUtf8(dpy, wm_xft_font, (const FcChar8 *)s, (int)strlen(s), &ext);
        return (int)ext.xOff;
    }
    if (bar_font) {
        return XTextWidth(bar_font, s, (int)strlen(s));
    }
    return (int)strlen(s) * 8;
}

void draw_text(Drawable dr, int x, int baseline_y, const char *s)
{
    if (!s || !*s) {
        return;
    }
    if (wm_xft_font && wm_xft_draw && wm_xft_color_valid) {
        XftDrawChange(wm_xft_draw, dr);
        unsigned long pixel = color_fg;
        XGCValues gcv;
        if (bar_gc && XGetGCValues(dpy, bar_gc, GCForeground, &gcv)) {
            pixel = gcv.foreground;
        }

        Colormap cmap = DefaultColormap(dpy, screen_num);
        XColor xc;
        xc.pixel = pixel;
        XQueryColor(dpy, cmap, &xc);

        XRenderColor xr;
        xr.red = xc.red;
        xr.green = xc.green;
        xr.blue = xc.blue;
        xr.alpha = 0xFFFF;

        XftColor dyn;
        if (XftColorAllocValue(dpy, DefaultVisual(dpy, screen_num), cmap, &xr, &dyn)) {
            XftDrawStringUtf8(wm_xft_draw, &dyn, wm_xft_font, x, baseline_y, (const FcChar8 *)s,
                              (int)strlen(s));
            XftColorFree(dpy, DefaultVisual(dpy, screen_num), cmap, &dyn);
            return;
        }

        XftDrawStringUtf8(wm_xft_draw, &wm_xft_color, wm_xft_font, x, baseline_y,
                          (const FcChar8 *)s, (int)strlen(s));
        return;
    }
    XDrawString(dpy, dr, bar_gc, x, baseline_y, s, (int)strlen(s));
}

void apply_value_format(const char *templ, const char *value, char *out, size_t outsz)
{
    const char *keys[] = {"value", "var"};
    const char *vals[] = {value ? value : "", value ? value : ""};
    apply_template_format(templ, keys, vals, 2, out, outsz);
}

void apply_template_format(const char *templ, const char *const *keys, const char *const *values,
                           size_t nkeys, char *out, size_t outsz)
{
    if (!out || outsz == 0) {
        return;
    }
    out[0] = '\0';
    if (!templ || !*templ) {
        if (nkeys > 0 && values && values[0]) {
            snprintf(out, outsz, "%s", values[0]);
        }
        return;
    }

    size_t used = 0;
    int replaced_any = 0;
    for (size_t i = 0; templ[i] && used + 1 < outsz;) {
        if (templ[i] == '{') {
            const char *end = strchr(templ + i + 1, '}');
            if (end) {
                size_t klen = (size_t)(end - (templ + i + 1));
                char key[64];
                if (klen < sizeof(key)) {
                    memcpy(key, templ + i + 1, klen);
                    key[klen] = '\0';
                    for (size_t k = 0; k < nkeys; k++) {
                        if (keys && keys[k] && !strcmp(key, keys[k])) {
                            const char *v = (values && values[k]) ? values[k] : "";
                            while (*v && used + 1 < outsz) {
                                out[used++] = *v++;
                            }
                            replaced_any = 1;
                            i = (size_t)(end - templ) + 1;
                            goto next_char;
                        }
                    }
                }
            }
        }
        out[used++] = templ[i++];
    next_char:;
    }
    out[used] = '\0';

    if (!replaced_any && nkeys > 0 && values && values[0] && values[0][0] && used + 2 < outsz) {
        if (used > 0 && out[used - 1] != ' ') {
            out[used++] = ' ';
        }
        strncat(out, values[0], outsz - used - 1);
    }
}
