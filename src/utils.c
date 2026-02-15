#include "wm_internal.h"

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void wm_log(WmLogLevel level, const char *fmt, ...)
{
    if (level > wm_log_level || level == WM_LOG_OFF || !fmt) {
        return;
    }

    const char *lvl = "INFO";
    if (level == WM_LOG_ERROR) {
        lvl = "ERROR";
    } else if (level == WM_LOG_DEBUG) {
        lvl = "DEBUG";
    }

    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }

    int fd = -1;
    if (wm_log_path[0]) {
        fd = open(wm_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    }
    if (fd < 0) {
        fd = STDERR_FILENO;
    }

    dprintf(fd, "[%ld] %s: %s\n", (long)time(NULL), lvl, line);
    if (fd != STDERR_FILENO) {
        close(fd);
    }
}

void *ecalloc(size_t n, size_t sz)
{
    void *p = calloc(n, sz);
    if (!p) {
        die("out of memory");
    }
    return p;
}

unsigned int workspace_bit(int idx)
{
    if (idx < 0 || idx >= MAX_WORKSPACES) {
        return 0;
    }
    return (unsigned int)(1U << idx);
}

unsigned int all_workspaces_mask(void)
{
    return (MAX_WORKSPACES >= 32) ? 0xFFFFFFFFU : ((1U << MAX_WORKSPACES) - 1U);
}

bool monitor_allows_workspace(const Monitor *m, int ws)
{
    if (!m) {
        return false;
    }
    return (m->ws_mask & workspace_bit(ws)) != 0;
}

int first_allowed_workspace(const Monitor *m)
{
    if (!m) {
        return 0;
    }
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        if (monitor_allows_workspace(m, i)) {
            return i;
        }
    }
    return 0;
}

Monitor *first_monitor_for_workspace(int ws)
{
    for (int i = 0; i < monitor_count; i++) {
        if (monitor_allows_workspace(&monitors[i], ws)) {
            return &monitors[i];
        }
    }
    return NULL;
}

unsigned int normalize_mods(unsigned int state)
{
    return state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
}

int xerror(Display *d, XErrorEvent *ee)
{
    (void)d;
    if (ee->error_code == BadWindow || ee->error_code == BadMatch ||
        ee->error_code == BadDrawable) {
        return 0;
    }
    char text[128];
    XGetErrorText(dpy, ee->error_code, text, sizeof(text));
    wm_log(WM_LOG_ERROR, "X error: req=%d code=%d (%s)", ee->request_code, ee->error_code, text);
    return 0;
}

int xerror_start(Display *d, XErrorEvent *ee)
{
    (void)d;
    (void)ee;
    die("another window manager is already running");
    return -1;
}

unsigned long get_color(const char *name, unsigned long fallback)
{
    Colormap cmap = DefaultColormap(dpy, screen_num);
    XColor color, exact;
    if (XAllocNamedColor(dpy, cmap, name, &color, &exact)) {
        return color.pixel;
    }
    return fallback;
}
