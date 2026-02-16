#include "lxwm.h"
#include "wm_internal.h"

Display *dpy;
int screen_num;
Window root;
Atom wm_delete;
Atom net_supported;
Atom net_wm_state;
Atom net_wm_state_fullscreen;
Atom net_active_window;
Atom net_current_desktop;
Atom net_number_of_desktops;
Atom net_client_list;
Atom net_client_list_stacking;
Atom net_workarea;

Monitor monitors[MAX_MONITORS];
int monitor_count;
Monitor *selmon;
Client *clients;
KeyBind keybinds[MAX_KEYBINDS];
int keybind_count;
Rule rules[MAX_RULES];
int rule_count;
ConfigVar config_vars[MAX_VARS];
int config_var_count;
int running = 1;
int wm_restart_requested = 0;
int bar_enabled = 1;
int ipc_enabled = 1;
int ipc_fd = -1;
char ipc_socket_path[PATH_MAX];
int config_error_count = 0;
WmLogLevel wm_log_level = WM_LOG_ERROR;
char wm_log_path[PATH_MAX] = "/tmp/lxwm.log";

unsigned long color_focus;
unsigned long color_unfocus;
unsigned long color_titlebar_focus;
unsigned long color_titlebar_unfocus;
unsigned long color_border_focus;
unsigned long color_border_unfocus;
unsigned long color_bg;
unsigned long color_fg;
unsigned long color_bar_mid;
unsigned long color_ws_active;
unsigned long color_ws_inactive;
unsigned long color_ws_urgent;
GC bar_gc;
XFontStruct *bar_font;
char wm_font_name[256] = "monospace";
int wm_font_size = 12;
unsigned int primary_mod_mask = Mod1Mask;
double cpu_usage_pct;
int mem_usage_pct;
int wifi_strength_pct = -1;
int wifi_link_up;
int ethernet_link_up;
char ethernet_iface[64] = "-";
char wifi_iface[64] = "-";
char ip_addr[64] = "-";
int ip_link_up;
int battery_pct = -1;
int cpu_temp_c = -1;
double disk_used_gb;
double disk_avail_gb;
double disk_total_gb;
unsigned long long last_cpu_total;
unsigned long long last_cpu_idle;
time_t last_stats_update;
int bar_ws_gap;
int bar_ws_pad_x;
int bar_ws_pad_y;
int bar_divider_min_w = 80;
int bar_ws_slot_w = 28;
int bar_right_zone_min_w = 220;
int bar_module_slot_w = 86;
int bar_modules_order[BAR_MOD_COUNT] = {BAR_MOD_CPU, BAR_MOD_MEM, BAR_MOD_TIME};
int bar_modules_count = 2;
char bar_module_sep[32] = "  ";
int bar_module_gap = 10;
char bar_cpu_fmt[96] = "CPU {value}";
char bar_mem_fmt[96] = "MEM {value}";
char bar_time_fmt[96] = "%H:%M";
char bar_wifi_fmt[96] = "WIFI {value}";
char bar_ethernet_fmt[96] = "ETH {value}";
char bar_ip_fmt[96] = "IP {value}";
char bar_battery_fmt[96] = "BAT {value}";
char bar_cputemp_fmt[96] = "TEMP {value}";
char bar_disk_fmt[96] = "DISK {value}";
char bar_disk_mode[16] = "used_total";
char bar_disk_path[128] = "/";
unsigned long bar_cpu_color;
unsigned long bar_mem_color;
unsigned long bar_time_color;
unsigned long bar_wifi_color;
unsigned long bar_wifi_down_color;
unsigned long bar_ethernet_color;
unsigned long bar_ethernet_down_color;
unsigned long bar_ip_color;
unsigned long bar_ip_down_color;
unsigned long bar_battery_color;
unsigned long bar_cputemp_color;
unsigned long bar_disk_color;
unsigned int configured_ws_masks[MAX_MONITORS];
unsigned char configured_ws_mask_set[MAX_MONITORS];
char workspace_names[MAX_WORKSPACES][32];
char autostart_cmds[MAX_AUTOSTART][256];
int autostart_count;
FocusPolicy focus_policy = FOCUS_CLICK;
Monitor *spawn_target_monitor;
time_t spawn_target_time;
XftFont *wm_xft_font;
XftDraw *wm_xft_draw;
XftColor wm_xft_color;
int wm_xft_color_valid;
Client *drag_client;
int drag_mode;
int drag_start_root_x;
int drag_start_root_y;
int drag_start_win_x;
int drag_start_win_y;
int drag_start_win_w;
int drag_start_win_h;
Client *scratchpad_client;

int lxwm_start(void)
{
    signal(SIGCHLD, SIG_IGN);

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        die("cannot open display");
    }

    setup();
    run();
    cleanup();
    XCloseDisplay(dpy);
    dpy = NULL;
    return wm_restart_requested ? 23 : 0;
}

int lxwm_check_config(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open display for config check\n");
        return 1;
    }

    screen_num = DefaultScreen(dpy);
    color_focus = WhitePixel(dpy, screen_num);
    color_unfocus = BlackPixel(dpy, screen_num);
    color_titlebar_focus = color_focus;
    color_titlebar_unfocus = color_unfocus;
    color_border_focus = color_focus;
    color_border_unfocus = color_unfocus;
    color_bg = BlackPixel(dpy, screen_num);
    color_fg = WhitePixel(dpy, screen_num);

    load_keybinds();
    XCloseDisplay(dpy);
    dpy = NULL;

    if (config_error_count > 0) {
        fprintf(stderr, "Config check failed: %d error(s)\n", config_error_count);
        return 1;
    }
    printf("Config check passed\n");
    return 0;
}
