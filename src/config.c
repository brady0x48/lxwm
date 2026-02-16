#include "wm_internal.h"

static unsigned int parse_modifiers(char *token)
{
    unsigned int mod = 0;
    for (char *part = strtok(token, "+"); part; part = strtok(NULL, "+")) {
        if (!strcasecmp(part, "Shift")) {
            mod |= ShiftMask;
        } else if (!strcasecmp(part, "Control") || !strcasecmp(part, "Ctrl")) {
            mod |= ControlMask;
        } else if (!strcasecmp(part, "Alt") || !strcasecmp(part, "Mod1")) {
            mod |= Mod1Mask;
        } else if (!strcasecmp(part, "Super") || !strcasecmp(part, "Mod4")) {
            mod |= Mod4Mask;
        } else if (!strcasecmp(part, "Mod")) {
            mod |= primary_mod_mask;
        }
    }
    return mod;
}

static unsigned int mask_for_modifier_index(int idx)
{
    if (idx < 0 || idx > 7) {
        return 0;
    }
    return (unsigned int)(1U << idx);
}

static unsigned int find_mask_for_keysyms(const KeySym *syms, size_t nsyms)
{
    if (!dpy || !syms || nsyms == 0) {
        return 0;
    }

    XModifierKeymap *map = XGetModifierMapping(dpy);
    if (!map) {
        return 0;
    }

    unsigned int mask = 0;
    for (int mod = 0; mod < 8; mod++) {
        for (int k = 0; k < map->max_keypermod; k++) {
            KeyCode code = map->modifiermap[(mod * map->max_keypermod) + k];
            if (!code) {
                continue;
            }
            int keysyms_per_keycode = 0;
            KeySym *ks_list = XGetKeyboardMapping(dpy, code, 1, &keysyms_per_keycode);
            KeySym ks = NoSymbol;
            if (ks_list && keysyms_per_keycode > 0) {
                ks = ks_list[0];
            }
            for (size_t i = 0; i < nsyms; i++) {
                if (ks == syms[i]) {
                    mask |= mask_for_modifier_index(mod);
                }
            }
            if (ks_list) {
                XFree(ks_list);
            }
        }
    }

    XFreeModifiermap(map);
    return mask;
}

static unsigned int parse_primary_mod(const char *name)
{
    if (!name) {
        return primary_mod_mask;
    }
    if (!strcasecmp(name, "Alt") || !strcasecmp(name, "Mod1")) {
        KeySym alt_syms[] = {XK_Alt_L, XK_Alt_R, XK_Meta_L, XK_Meta_R};
        unsigned int mask = find_mask_for_keysyms(alt_syms, sizeof(alt_syms) / sizeof(alt_syms[0]));
        return mask ? mask : Mod1Mask;
    }
    if (!strcasecmp(name, "Super") || !strcasecmp(name, "Mod4")) {
        KeySym super_syms[] = {XK_Super_L, XK_Super_R, XK_Hyper_L, XK_Hyper_R};
        unsigned int mask =
            find_mask_for_keysyms(super_syms, sizeof(super_syms) / sizeof(super_syms[0]));
        return mask ? mask : Mod4Mask;
    }
    if (!strcasecmp(name, "Control") || !strcasecmp(name, "Ctrl")) {
        return ControlMask;
    }
    return primary_mod_mask;
}

static unsigned int parse_workspace_spec(const char *spec)
{
    if (!spec || !*spec) {
        return 0;
    }

    char buf[256];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    unsigned int mask = 0;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok && isspace((unsigned char)*tok)) {
            tok++;
        }
        char *end = tok + strlen(tok) - 1;
        while (end >= tok && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }
        if (!*tok) {
            continue;
        }
        if (!strcasecmp(tok, "all")) {
            return all_workspaces_mask();
        }
        if (!strcasecmp(tok, "odd")) {
            for (int i = 0; i < MAX_WORKSPACES; i += 2) {
                mask |= workspace_bit(i);
            }
            continue;
        }
        if (!strcasecmp(tok, "even")) {
            for (int i = 1; i < MAX_WORKSPACES; i += 2) {
                mask |= workspace_bit(i);
            }
            continue;
        }

        char *dash = strchr(tok, '-');
        if (dash) {
            *dash = '\0';
            int a = atoi(tok);
            int b = atoi(dash + 1);
            if (a > b) {
                int tmp = a;
                a = b;
                b = tmp;
            }
            for (int w = a; w <= b; w++) {
                int idx = w - 1;
                if (idx >= 0 && idx < MAX_WORKSPACES) {
                    mask |= workspace_bit(idx);
                }
            }
            continue;
        }

        int w = atoi(tok);
        int idx = w - 1;
        if (idx >= 0 && idx < MAX_WORKSPACES) {
            mask |= workspace_bit(idx);
        }
    }
    return mask;
}

int clamp_int(int v, int min_v, int max_v)
{
    if (v < min_v) {
        return min_v;
    }
    if (v > max_v) {
        return max_v;
    }
    return v;
}

static int parse_bool_value(const char *tok, int fallback)
{
    if (!tok || !*tok) {
        return fallback;
    }
    if (!strcasecmp(tok, "1") || !strcasecmp(tok, "true") || !strcasecmp(tok, "yes") ||
        !strcasecmp(tok, "on")) {
        return 1;
    }
    if (!strcasecmp(tok, "0") || !strcasecmp(tok, "false") || !strcasecmp(tok, "no") ||
        !strcasecmp(tok, "off")) {
        return 0;
    }
    return fallback;
}

static WmLogLevel parse_log_level(const char *tok, WmLogLevel fallback)
{
    if (!tok || !*tok) {
        return fallback;
    }
    if (!strcasecmp(tok, "off") || !strcasecmp(tok, "0")) {
        return WM_LOG_OFF;
    }
    if (!strcasecmp(tok, "error") || !strcasecmp(tok, "1")) {
        return WM_LOG_ERROR;
    }
    if (!strcasecmp(tok, "info") || !strcasecmp(tok, "2")) {
        return WM_LOG_INFO;
    }
    if (!strcasecmp(tok, "debug") || !strcasecmp(tok, "3")) {
        return WM_LOG_DEBUG;
    }
    return fallback;
}

static BarModule parse_bar_module_name(const char *name)
{
    if (!name) {
        return BAR_MOD_COUNT;
    }
    if (!strcasecmp(name, "cpu")) {
        return BAR_MOD_CPU;
    }
    if (!strcasecmp(name, "mem") || !strcasecmp(name, "memory")) {
        return BAR_MOD_MEM;
    }
    if (!strcasecmp(name, "time") || !strcasecmp(name, "clock")) {
        return BAR_MOD_TIME;
    }
    if (!strcasecmp(name, "wifi")) {
        return BAR_MOD_WIFI;
    }
    if (!strcasecmp(name, "eth") || !strcasecmp(name, "ethernet")) {
        return BAR_MOD_ETHERNET;
    }
    if (!strcasecmp(name, "ip")) {
        return BAR_MOD_IP;
    }
    if (!strcasecmp(name, "battery") || !strcasecmp(name, "batt")) {
        return BAR_MOD_BATTERY;
    }
    if (!strcasecmp(name, "cputemp") || !strcasecmp(name, "temp")) {
        return BAR_MOD_CPUTEMP;
    }
    if (!strcasecmp(name, "disk")) {
        return BAR_MOD_DISK;
    }
    return BAR_MOD_COUNT;
}

static void parse_bar_modules_list(const char *list)
{
    if (!list || !*list) {
        return;
    }
    char buf[256];
    strncpy(buf, list, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int count = 0;
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok && isspace((unsigned char)*tok)) {
            tok++;
        }
        char *end = tok + strlen(tok) - 1;
        while (end >= tok && isspace((unsigned char)*end)) {
            *end = '\0';
            end--;
        }
        BarModule mod = parse_bar_module_name(tok);
        if (mod == BAR_MOD_COUNT) {
            continue;
        }
        bool seen = false;
        for (int i = 0; i < count; i++) {
            if (bar_modules_order[i] == (int)mod) {
                seen = true;
                break;
            }
        }
        if (!seen && count < BAR_MOD_COUNT) {
            bar_modules_order[count++] = (int)mod;
        }
    }
    if (count > 0) {
        bar_modules_count = count;
    }
}

static void copy_trimmed(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    while (*src && isspace((unsigned char)*src)) {
        src++;
    }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
    size_t n = strlen(dst);
    while (n > 0 && isspace((unsigned char)dst[n - 1])) {
        dst[n - 1] = '\0';
        n--;
    }
}

static void copy_preserve_spaces(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    while (*src && (*src == '\r' || *src == '\n')) {
        src++;
    }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
    size_t n = strlen(dst);
    while (n > 0 && (dst[n - 1] == '\r' || dst[n - 1] == '\n')) {
        dst[n - 1] = '\0';
        n--;
    }
}

static void set_bar_color(unsigned long *target, const char *value, unsigned long fallback)
{
    if (!target || !value || !*value) {
        return;
    }
    *target = get_color(value, fallback);
}

static int find_config_var_idx(const char *name)
{
    if (!name || !*name) {
        return -1;
    }
    for (int i = 0; i < config_var_count; i++) {
        if (!strcmp(config_vars[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static void set_config_var(const char *name, const char *value)
{
    if (!name || !*name || !value) {
        return;
    }
    int idx = find_config_var_idx(name);
    if (idx < 0) {
        if (config_var_count >= MAX_VARS) {
            return;
        }
        idx = config_var_count++;
        config_vars[idx].name[0] = '\0';
        config_vars[idx].value[0] = '\0';
        strncpy(config_vars[idx].name, name, sizeof(config_vars[idx].name) - 1);
        config_vars[idx].name[sizeof(config_vars[idx].name) - 1] = '\0';
    }
    strncpy(config_vars[idx].value, value, sizeof(config_vars[idx].value) - 1);
    config_vars[idx].value[sizeof(config_vars[idx].value) - 1] = '\0';
}

static void expand_config_vars(const char *src, char *dst, size_t dstsz)
{
    if (!dst || dstsz == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }

    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 1 < dstsz;) {
        if (src[i] == '$' && (isalnum((unsigned char)src[i + 1]) || src[i + 1] == '_')) {
            char name[64];
            size_t ni = 0;
            i++;
            while (src[i] && (isalnum((unsigned char)src[i]) || src[i] == '_') &&
                   ni + 1 < sizeof(name)) {
                name[ni++] = src[i++];
            }
            name[ni] = '\0';
            int idx = find_config_var_idx(name);
            if (idx >= 0 && config_vars[idx].value[0]) {
                const char *v = config_vars[idx].value;
                for (size_t k = 0; v[k] && di + 1 < dstsz; k++) {
                    dst[di++] = v[k];
                }
                continue;
            }
            if (di + 1 < dstsz) {
                dst[di++] = '$';
            }
            for (size_t k = 0; name[k] && di + 1 < dstsz; k++) {
                dst[di++] = name[k];
            }
            continue;
        }
        dst[di++] = src[i++];
    }
    dst[di] = '\0';
}

static void add_keybind(unsigned int mod, const char *key, ActionType act, const char *arg,
                        int iarg)
{
    KeySym sym = XStringToKeysym(key);
    if (sym == NoSymbol) {
        fprintf(stderr, "invalid key symbol: %s\n", key);
        config_error_count++;
        return;
    }
    KeyCode code = dpy ? XKeysymToKeycode(dpy, sym) : 0;

    for (int i = 0; i < keybind_count; i++) {
        if (keybinds[i].mod == mod && keybinds[i].keycode == code) {
            keybinds[i].keysym = sym;
            keybinds[i].keycode = code;
            keybinds[i].action = act;
            keybinds[i].iarg = iarg;
            keybinds[i].arg[0] = '\0';
            if (arg) {
                strncpy(keybinds[i].arg, arg, sizeof(keybinds[i].arg) - 1);
                keybinds[i].arg[sizeof(keybinds[i].arg) - 1] = '\0';
            }
            return;
        }
    }

    if (keybind_count >= MAX_KEYBINDS) {
        return;
    }

    KeyBind *kb = &keybinds[keybind_count++];
    kb->mod = mod;
    kb->keysym = sym;
    kb->keycode = code;
    kb->action = act;
    kb->iarg = iarg;
    kb->arg[0] = '\0';
    if (arg) {
        strncpy(kb->arg, arg, sizeof(kb->arg) - 1);
        kb->arg[sizeof(kb->arg) - 1] = '\0';
    }
}

static int find_rule_idx(const char *class_name, const char *instance_name,
                         const char *title_substr)
{
    if ((!class_name || !*class_name) && (!instance_name || !*instance_name) &&
        (!title_substr || !*title_substr)) {
        return -1;
    }
    for (int i = 0; i < rule_count; i++) {
        Rule *r = &rules[i];
        if ((!class_name || !*class_name || !strcasecmp(r->class_name, class_name)) &&
            (!instance_name || !*instance_name || !strcasecmp(r->instance_name, instance_name)) &&
            (!title_substr || !*title_substr || !strcasecmp(r->title_substr, title_substr))) {
            return i;
        }
    }
    return -1;
}

static Rule *ensure_rule(const char *class_name, const char *instance_name,
                         const char *title_substr)
{
    int idx = find_rule_idx(class_name, instance_name, title_substr);
    if (idx >= 0) {
        return &rules[idx];
    }
    if (rule_count >= MAX_RULES) {
        return NULL;
    }
    Rule *r = &rules[rule_count++];
    memset(r, 0, sizeof(*r));
    if (class_name) {
        strncpy(r->class_name, class_name, sizeof(r->class_name) - 1);
        r->class_name[sizeof(r->class_name) - 1] = '\0';
    }
    if (instance_name) {
        strncpy(r->instance_name, instance_name, sizeof(r->instance_name) - 1);
        r->instance_name[sizeof(r->instance_name) - 1] = '\0';
    }
    if (title_substr) {
        strncpy(r->title_substr, title_substr, sizeof(r->title_substr) - 1);
        r->title_substr[sizeof(r->title_substr) - 1] = '\0';
    }
    r->workspace = -1;
    r->monitor = -1;
    return r;
}

static void load_default_keybinds(void)
{
    keybind_count = 0;
    add_keybind(primary_mod_mask, "Return", ACT_SPAWN, "xterm", 0);
    add_keybind(primary_mod_mask, "h", ACT_FOCUS_LEFT, NULL, 0);
    add_keybind(primary_mod_mask, "l", ACT_FOCUS_RIGHT, NULL, 0);
    add_keybind(primary_mod_mask, "k", ACT_FOCUS_UP, NULL, 0);
    add_keybind(primary_mod_mask, "j", ACT_FOCUS_DOWN, NULL, 0);
    add_keybind(primary_mod_mask | ShiftMask, "q", ACT_KILL, NULL, 0);
    add_keybind(primary_mod_mask | ControlMask | ShiftMask, "q", ACT_QUIT, NULL, 0);
    add_keybind(primary_mod_mask, "r", ACT_RELOAD, NULL, 0);
    add_keybind(primary_mod_mask | ShiftMask, "r", ACT_RESTART, NULL, 0);
    add_keybind(primary_mod_mask, "f", ACT_TOGGLE_FULLSCREEN, NULL, 0);
    add_keybind(primary_mod_mask, "s", ACT_SPLIT_TOGGLE, NULL, 0);
    add_keybind(primary_mod_mask, "minus", ACT_SCRATCHPAD_TOGGLE, NULL, 0);
    add_keybind(primary_mod_mask | ShiftMask, "s", ACT_ROTATE_TREE, NULL, 0);
    add_keybind(primary_mod_mask | ShiftMask, "Return", ACT_SWAP_NEXT, NULL, 0);
    add_keybind(primary_mod_mask | ShiftMask, "space", ACT_TOGGLE_FLOAT, NULL, 0);
    add_keybind(primary_mod_mask | ControlMask, "h", ACT_MOVE_FLOAT, "-20 0", 0);
    add_keybind(primary_mod_mask | ControlMask, "l", ACT_MOVE_FLOAT, "20 0", 0);
    add_keybind(primary_mod_mask | ControlMask, "k", ACT_MOVE_FLOAT, "0 -20", 0);
    add_keybind(primary_mod_mask | ControlMask, "j", ACT_MOVE_FLOAT, "0 20", 0);
    add_keybind(primary_mod_mask | ControlMask | ShiftMask, "h", ACT_RESIZE_FLOAT, "-20 0", 0);
    add_keybind(primary_mod_mask | ControlMask | ShiftMask, "l", ACT_RESIZE_FLOAT, "20 0", 0);
    add_keybind(primary_mod_mask | ControlMask | ShiftMask, "k", ACT_RESIZE_FLOAT, "0 -20", 0);
    add_keybind(primary_mod_mask | ControlMask | ShiftMask, "j", ACT_RESIZE_FLOAT, "0 20", 0);

    for (int i = 0; i < 9; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", i + 1);
        add_keybind(primary_mod_mask, key, ACT_WS, NULL, i);
        add_keybind(primary_mod_mask | ShiftMask, key, ACT_MOVE_TO_WS, NULL, i);
    }
}

static void normalize_module_name(const char *raw, char *out, size_t outsz)
{
    if (!out || outsz == 0) {
        return;
    }
    out[0] = '\0';
    if (!raw || !*raw) {
        return;
    }

    char tmp[64];
    copy_trimmed(tmp, sizeof(tmp), raw);
    for (size_t i = 0; tmp[i]; i++) {
        tmp[i] = (char)tolower((unsigned char)tmp[i]);
    }

    if (!strcmp(tmp, "memory")) {
        snprintf(out, outsz, "mem");
    } else if (!strcmp(tmp, "clock")) {
        snprintf(out, outsz, "time");
    } else if (!strcmp(tmp, "eth")) {
        snprintf(out, outsz, "ethernet");
    } else if (!strcmp(tmp, "batt")) {
        snprintf(out, outsz, "battery");
    } else if (!strcmp(tmp, "temp")) {
        snprintf(out, outsz, "cputemp");
    } else {
        snprintf(out, outsz, "%s", tmp);
    }
}

static void unquote_value(char *s)
{
    if (!s) {
        return;
    }
    size_t n = strlen(s);
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static int parse_section_header_line(char *line, char *section, size_t section_sz)
{
    if (!line || !section || section_sz == 0) {
        return 0;
    }

    char *s = line;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (*s != '[') {
        return 0;
    }

    char *end = strchr(s, ']');
    if (!end) {
        return 0;
    }
    *end = '\0';

    copy_trimmed(section, section_sz, s + 1);
    for (size_t i = 0; section[i]; i++) {
        section[i] = (char)tolower((unsigned char)section[i]);
    }
    if (!section[0]) {
        snprintf(section, section_sz, "global");
    }
    return 1;
}

static void map_scoped_line(const char *section, char *line, char *out, size_t outsz)
{
    if (!out || outsz == 0) {
        return;
    }
    out[0] = '\0';
    if (!line) {
        return;
    }

    char *s = line;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s || *s == '#') {
        return;
    }

    char raw[512];
    copy_trimmed(raw, sizeof(raw), s);

    const char *active = (section && *section) ? section : "global";
    char *eq = strchr(raw, '=');
    if (!strcmp(active, "variables")) {
        if (!eq) {
            snprintf(out, outsz, "%s", raw);
            return;
        }
        *eq = '\0';
        char key[128];
        char value[384];
        copy_trimmed(key, sizeof(key), raw);
        copy_trimmed(value, sizeof(value), eq + 1);
        unquote_value(value);
        if (key[0] == '$') {
            memmove(key, key + 1, strlen(key));
        }
        if (!key[0]) {
            return;
        }
        snprintf(out, outsz, "set $%s %s", key, value);
        return;
    }

    if (!strcmp(active, "autostart")) {
        if (!eq) {
            snprintf(out, outsz, "%s", raw);
            return;
        }
        char value[384];
        copy_trimmed(value, sizeof(value), eq + 1);
        unquote_value(value);
        snprintf(out, outsz, "autostart %s", value);
        return;
    }

    if (!strcmp(active, "keybinds")) {
        if (!eq) {
            snprintf(out, outsz, "%s", raw);
            return;
        }
        *eq = '\0';
        char keycombo[128];
        char action[384];
        copy_trimmed(keycombo, sizeof(keycombo), raw);
        copy_trimmed(action, sizeof(action), eq + 1);
        unquote_value(action);
        if (!keycombo[0] || !action[0]) {
            return;
        }
        char modtok[96];
        char keytok[64];
        keytok[0] = '\0';
        char combo_copy[128];
        snprintf(combo_copy, sizeof(combo_copy), "%s", keycombo);
        char *last_plus = strrchr(combo_copy, '+');
        if (last_plus) {
            *last_plus = '\0';
            copy_trimmed(modtok, sizeof(modtok), combo_copy);
            copy_trimmed(keytok, sizeof(keytok), last_plus + 1);
        } else {
            snprintf(modtok, sizeof(modtok), "Mod");
            copy_trimmed(keytok, sizeof(keytok), combo_copy);
        }
        if (!keytok[0]) {
            return;
        }
        snprintf(out, outsz, "bind %s %s %s", modtok, keytok, action);
        return;
    }

    if (!strcmp(active, "global") || !strcmp(active, "globals")) {
        if (!eq) {
            snprintf(out, outsz, "%s", raw);
            return;
        }
        *eq = '\0';
        char key[128];
        char value[384];
        copy_trimmed(key, sizeof(key), raw);
        copy_trimmed(value, sizeof(value), eq + 1);
        unquote_value(value);
        snprintf(out, outsz, "%s %s", key, value);
        return;
    }

    if (!eq) {
        snprintf(out, outsz, "%s", raw);
        return;
    }

    *eq = '\0';
    char key[128];
    char value[384];
    copy_trimmed(key, sizeof(key), raw);
    copy_trimmed(value, sizeof(value), eq + 1);
    unquote_value(value);
    for (size_t i = 0; key[i]; i++) {
        key[i] = (char)tolower((unsigned char)key[i]);
    }

    if (!strcmp(active, "core") || !strcmp(active, "wm")) {
        if (!strcmp(key, "mod") || !strcmp(key, "setmod")) {
            snprintf(out, outsz, "setmod %s", value);
        } else if (!strcmp(key, "focus_policy") || !strcmp(key, "bar_enabled") ||
                   !strcmp(key, "ipc_enabled") || !strcmp(key, "log_level") ||
                   !strcmp(key, "log_path") || !strcmp(key, "font") || !strcmp(key, "font_size")) {
            snprintf(out, outsz, "%s %s", key, value);
        } else {
            snprintf(out, outsz, "%s %s", key, value);
        }
        return;
    }

    if (!strcmp(active, "bar")) {
        if (!strcmp(key, "enabled")) {
            snprintf(out, outsz, "bar_enabled %s", value);
        } else if (!strcmp(key, "modules")) {
            snprintf(out, outsz, "bar_modules %s", value);
        } else if (!strcmp(key, "module_sep")) {
            snprintf(out, outsz, "bar_module_sep %s", value);
        } else if (!strcmp(key, "module_gap")) {
            snprintf(out, outsz, "bar_module_gap %s", value);
        } else if (!strcmp(key, "module_slot_w")) {
            snprintf(out, outsz, "bar_module_slot_w %s", value);
        } else if (!strcmp(key, "ws_gap")) {
            snprintf(out, outsz, "bar_ws_gap %s", value);
        } else if (!strcmp(key, "ws_pad_x")) {
            snprintf(out, outsz, "bar_ws_pad_x %s", value);
        } else if (!strcmp(key, "ws_pad_y")) {
            snprintf(out, outsz, "bar_ws_pad_y %s", value);
        } else if (!strcmp(key, "ws_width")) {
            snprintf(out, outsz, "bar_ws_width %s", value);
        } else if (!strcmp(key, "divider_min_w")) {
            snprintf(out, outsz, "bar_divider_min_w %s", value);
        } else if (!strcmp(key, "right_zone_min_w")) {
            snprintf(out, outsz, "bar_right_zone_min_w %s", value);
        } else if (!strcmp(key, "divider_color")) {
            snprintf(out, outsz, "bar_divider_color %s", value);
        } else if (!strcmp(key, "ws_active_color")) {
            snprintf(out, outsz, "bar_ws_active_color %s", value);
        } else if (!strcmp(key, "ws_inactive_color")) {
            snprintf(out, outsz, "bar_ws_inactive_color %s", value);
        } else if (!strcmp(key, "ws_urgent_color")) {
            snprintf(out, outsz, "bar_ws_urgent_color %s", value);
        } else {
            snprintf(out, outsz, "bar_%s %s", key, value);
        }
        return;
    }

    if (!strncmp(active, "bar.", 4)) {
        char module[32];
        normalize_module_name(active + 4, module, sizeof(module));
        if (!module[0]) {
            snprintf(out, outsz, "%s", raw);
            return;
        }

        if (!strcmp(key, "format") || !strcmp(key, "fmt")) {
            snprintf(out, outsz, "bar_%s_fmt %s", module, value);
        } else if (!strcmp(key, "color")) {
            snprintf(out, outsz, "bar_%s_color %s", module, value);
        } else if (!strcmp(key, "down_color")) {
            snprintf(out, outsz, "bar_%s_down_color %s", module, value);
        } else if (!strcmp(module, "disk") && !strcmp(key, "mode")) {
            snprintf(out, outsz, "bar_disk_mode %s", value);
        } else if (!strcmp(module, "disk") && !strcmp(key, "path")) {
            snprintf(out, outsz, "bar_disk_path %s", value);
        } else {
            snprintf(out, outsz, "bar_%s_%s %s", module, key, value);
        }
        return;
    }

    if (!strcmp(active, "workspaces")) {
        bool numeric_key = true;
        for (size_t i = 0; key[i]; i++) {
            if (!isdigit((unsigned char)key[i])) {
                numeric_key = false;
                break;
            }
        }
        if (numeric_key) {
            snprintf(out, outsz, "workspace_name %s %s", key, value);
        } else {
            snprintf(out, outsz, "%s %s", key, value);
        }
        return;
    }

    snprintf(out, outsz, "%s", raw);
}

static void parse_config_line(char *line)
{
    char *s = line;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s || *s == '#') {
        return;
    }

    char *cmd = strtok(s, " \t\n");
    if (!cmd) {
        return;
    }
    if (!strcmp(cmd, "setmod")) {
        char *modname = strtok(NULL, " \t\n");
        if (modname) {
            primary_mod_mask = parse_primary_mod(modname);
        }
        return;
    }
    if (!strcmp(cmd, "focus_policy")) {
        char *mode = strtok(NULL, " \t\n");
        if (mode) {
            if (!strcasecmp(mode, "hover")) {
                focus_policy = FOCUS_HOVER;
            } else if (!strcasecmp(mode, "click")) {
                focus_policy = FOCUS_CLICK;
            }
        }
        return;
    }
    if (!strcmp(cmd, "bar_enabled")) {
        char *tok = strtok(NULL, " \t\n");
        bar_enabled = parse_bool_value(tok, bar_enabled);
        return;
    }
    if (!strcmp(cmd, "ipc_enabled")) {
        char *tok = strtok(NULL, " \t\n");
        ipc_enabled = parse_bool_value(tok, ipc_enabled);
        return;
    }
    if (!strcmp(cmd, "log_level")) {
        char *tok = strtok(NULL, " \t\n");
        wm_log_level = parse_log_level(tok, wm_log_level);
        return;
    }
    if (!strcmp(cmd, "log_path")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(wm_log_path, sizeof(wm_log_path), rest);
        }
        return;
    }
    if (!strcmp(cmd, "titlebar_focus_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            color_titlebar_focus = get_color(tok, color_titlebar_focus);
        }
        return;
    }
    if (!strcmp(cmd, "titlebar_unfocus_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            color_titlebar_unfocus = get_color(tok, color_titlebar_unfocus);
        }
        return;
    }
    if (!strcmp(cmd, "border_focus_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            color_border_focus = get_color(tok, color_border_focus);
        }
        return;
    }
    if (!strcmp(cmd, "border_unfocus_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            color_border_unfocus = get_color(tok, color_border_unfocus);
        }
        return;
    }
    if (!strcmp(cmd, "autostart")) {
        char *rest = strtok(NULL, "\n");
        if (rest && autostart_count < MAX_AUTOSTART) {
            while (*rest && isspace((unsigned char)*rest)) {
                rest++;
            }
            if (*rest) {
                char expanded[256];
                expand_config_vars(rest, expanded, sizeof(expanded));
                snprintf(autostart_cmds[autostart_count], sizeof(autostart_cmds[autostart_count]),
                         "%s", expanded);
                autostart_count++;
            }
        }
        return;
    }
    if (!strcmp(cmd, "setvar")) {
        char *name = strtok(NULL, " \t\n");
        char *value = strtok(NULL, "\n");
        if (!name || !value) {
            return;
        }
        while (*value && isspace((unsigned char)*value)) {
            value++;
        }
        if (*value) {
            char expanded[256];
            expand_config_vars(value, expanded, sizeof(expanded));
            set_config_var(name, expanded);
        }
        return;
    }
    if (!strcmp(cmd, "set")) {
        char *what = strtok(NULL, " \t\n");
        char *value = strtok(NULL, " \t\n");
        if (what && what[0] == '$') {
            char *rest = value;
            if (!rest) {
                rest = strtok(NULL, "\n");
            } else {
                char *tail = strtok(NULL, "\n");
                if (tail) {
                    char combined[512];
                    snprintf(combined, sizeof(combined), "%s %s", rest, tail);
                    rest = combined;
                    char expanded_combined[256];
                    expand_config_vars(rest, expanded_combined, sizeof(expanded_combined));
                    set_config_var(what + 1, expanded_combined);
                    return;
                }
            }
            if (rest) {
                char expanded[256];
                expand_config_vars(rest, expanded, sizeof(expanded));
                set_config_var(what + 1, expanded);
            }
            return;
        }
        if (what && value && !strcmp(what, "mod")) {
            primary_mod_mask = parse_primary_mod(value);
        }
        if (what && value && !strcmp(what, "focus")) {
            if (!strcasecmp(value, "hover")) {
                focus_policy = FOCUS_HOVER;
            } else if (!strcasecmp(value, "click")) {
                focus_policy = FOCUS_CLICK;
            }
            return;
        }
        if (what && !strcmp(what, "font")) {
            char *rest = value;
            if (!rest) {
                rest = strtok(NULL, "\n");
            } else {
                char *tail = strtok(NULL, "\n");
                if (tail) {
                    char combined[512];
                    snprintf(combined, sizeof(combined), "%s %s", rest, tail);
                    copy_trimmed(wm_font_name, sizeof(wm_font_name), combined);
                } else {
                    copy_trimmed(wm_font_name, sizeof(wm_font_name), rest);
                }
            }
            return;
        }
        if (what && value && !strcmp(what, "font_size")) {
            wm_font_size = clamp_int(atoi(value), 6, 72);
            return;
        }
        if (what && !strcmp(what, "bar")) {
            char *sub = value;
            char *rest = strtok(NULL, "\n");
            if (sub && rest) {
                while (*rest && isspace((unsigned char)*rest)) {
                    rest++;
                }
                if (!strcmp(sub, "modules")) {
                    parse_bar_modules_list(rest);
                } else if (!strcmp(sub, "module_sep")) {
                    copy_preserve_spaces(bar_module_sep, sizeof(bar_module_sep), rest);
                } else if (!strcmp(sub, "module_gap")) {
                    bar_module_gap = clamp_int(atoi(rest), 0, 120);
                } else if (!strcmp(sub, "module_slot_w")) {
                    bar_module_slot_w = clamp_int(atoi(rest), 20, 300);
                } else if (!strcmp(sub, "right_zone_min_w")) {
                    bar_right_zone_min_w = clamp_int(atoi(rest), 80, 2000);
                } else if (!strcmp(sub, "cpu_fmt")) {
                    copy_trimmed(bar_cpu_fmt, sizeof(bar_cpu_fmt), rest);
                } else if (!strcmp(sub, "mem_fmt")) {
                    copy_trimmed(bar_mem_fmt, sizeof(bar_mem_fmt), rest);
                } else if (!strcmp(sub, "time_fmt")) {
                    copy_trimmed(bar_time_fmt, sizeof(bar_time_fmt), rest);
                } else if (!strcmp(sub, "wifi_fmt")) {
                    copy_trimmed(bar_wifi_fmt, sizeof(bar_wifi_fmt), rest);
                } else if (!strcmp(sub, "ethernet_fmt")) {
                    copy_trimmed(bar_ethernet_fmt, sizeof(bar_ethernet_fmt), rest);
                } else if (!strcmp(sub, "ip_fmt")) {
                    copy_trimmed(bar_ip_fmt, sizeof(bar_ip_fmt), rest);
                } else if (!strcmp(sub, "battery_fmt")) {
                    copy_trimmed(bar_battery_fmt, sizeof(bar_battery_fmt), rest);
                } else if (!strcmp(sub, "cputemp_fmt")) {
                    copy_trimmed(bar_cputemp_fmt, sizeof(bar_cputemp_fmt), rest);
                } else if (!strcmp(sub, "disk_fmt")) {
                    copy_trimmed(bar_disk_fmt, sizeof(bar_disk_fmt), rest);
                } else if (!strcmp(sub, "disk_mode")) {
                    copy_trimmed(bar_disk_mode, sizeof(bar_disk_mode), rest);
                } else if (!strcmp(sub, "disk_path")) {
                    copy_trimmed(bar_disk_path, sizeof(bar_disk_path), rest);
                } else if (!strcmp(sub, "divider_color")) {
                    set_bar_color(&color_bar_mid, rest, color_bar_mid);
                } else if (!strcmp(sub, "ws_active_color")) {
                    set_bar_color(&color_ws_active, rest, color_ws_active);
                } else if (!strcmp(sub, "ws_inactive_color")) {
                    set_bar_color(&color_ws_inactive, rest, color_ws_inactive);
                } else if (!strcmp(sub, "ws_urgent_color")) {
                    set_bar_color(&color_ws_urgent, rest, color_ws_urgent);
                } else if (!strcmp(sub, "cpu_color")) {
                    set_bar_color(&bar_cpu_color, rest, bar_cpu_color);
                } else if (!strcmp(sub, "mem_color")) {
                    set_bar_color(&bar_mem_color, rest, bar_mem_color);
                } else if (!strcmp(sub, "time_color")) {
                    set_bar_color(&bar_time_color, rest, bar_time_color);
                } else if (!strcmp(sub, "wifi_color")) {
                    set_bar_color(&bar_wifi_color, rest, bar_wifi_color);
                } else if (!strcmp(sub, "wifi_down_color")) {
                    set_bar_color(&bar_wifi_down_color, rest, bar_wifi_down_color);
                } else if (!strcmp(sub, "ethernet_color")) {
                    set_bar_color(&bar_ethernet_color, rest, bar_ethernet_color);
                } else if (!strcmp(sub, "ethernet_down_color")) {
                    set_bar_color(&bar_ethernet_down_color, rest, bar_ethernet_down_color);
                } else if (!strcmp(sub, "ip_color")) {
                    set_bar_color(&bar_ip_color, rest, bar_ip_color);
                } else if (!strcmp(sub, "ip_down_color")) {
                    set_bar_color(&bar_ip_down_color, rest, bar_ip_down_color);
                } else if (!strcmp(sub, "battery_color")) {
                    set_bar_color(&bar_battery_color, rest, bar_battery_color);
                } else if (!strcmp(sub, "cputemp_color")) {
                    set_bar_color(&bar_cputemp_color, rest, bar_cputemp_color);
                } else if (!strcmp(sub, "disk_color")) {
                    set_bar_color(&bar_disk_color, rest, bar_disk_color);
                }
            }
            return;
        }
        return;
    }
    if (!strcmp(cmd, "font")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(wm_font_name, sizeof(wm_font_name), rest);
        }
        return;
    }
    if (!strcmp(cmd, "set")) {
    }
    if (!strcmp(cmd, "font_size")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            wm_font_size = clamp_int(atoi(tok), 6, 72);
        }
        return;
    }
    if (!strcmp(cmd, "bar_modules")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            while (*rest && isspace((unsigned char)*rest)) {
                rest++;
            }
            parse_bar_modules_list(rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_module_sep")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_preserve_spaces(bar_module_sep, sizeof(bar_module_sep), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_module_gap")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            bar_module_gap = clamp_int(atoi(tok), 0, 120);
        }
        return;
    }
    if (!strcmp(cmd, "bar_module_slot_w")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            bar_module_slot_w = clamp_int(atoi(tok), 20, 300);
        }
        return;
    }
    if (!strcmp(cmd, "bar_cpu_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_cpu_fmt, sizeof(bar_cpu_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_mem_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_mem_fmt, sizeof(bar_mem_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_time_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_time_fmt, sizeof(bar_time_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_wifi_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_wifi_fmt, sizeof(bar_wifi_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ethernet_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_ethernet_fmt, sizeof(bar_ethernet_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ip_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_ip_fmt, sizeof(bar_ip_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_battery_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_battery_fmt, sizeof(bar_battery_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_cputemp_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_cputemp_fmt, sizeof(bar_cputemp_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_disk_fmt")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_disk_fmt, sizeof(bar_disk_fmt), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_disk_mode")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            copy_trimmed(bar_disk_mode, sizeof(bar_disk_mode), tok);
        }
        return;
    }
    if (!strcmp(cmd, "bar_disk_path")) {
        char *rest = strtok(NULL, "\n");
        if (rest) {
            copy_trimmed(bar_disk_path, sizeof(bar_disk_path), rest);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ws_gap")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            bar_ws_gap = clamp_int(atoi(tok), 0, 20);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ws_pad_x")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            bar_ws_pad_x = clamp_int(atoi(tok), 0, 40);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ws_pad_y")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            bar_ws_pad_y = clamp_int(atoi(tok), 0, BAR_HEIGHT / 2);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ws_width")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            bar_ws_slot_w = clamp_int(atoi(tok), 16, 80);
        }
        return;
    }
    if (!strcmp(cmd, "bar_divider_min_w")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            bar_divider_min_w = clamp_int(atoi(tok), 0, 800);
        }
        return;
    }
    if (!strcmp(cmd, "bar_right_zone_min_w")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            bar_right_zone_min_w = clamp_int(atoi(tok), 80, 2000);
        }
        return;
    }
    if (!strcmp(cmd, "bar_divider_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&color_bar_mid, tok, color_bar_mid);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ws_active_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&color_ws_active, tok, color_ws_active);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ws_inactive_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&color_ws_inactive, tok, color_ws_inactive);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ws_urgent_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&color_ws_urgent, tok, color_ws_urgent);
        }
        return;
    }
    if (!strcmp(cmd, "workspace_name")) {
        char *idx_tok = strtok(NULL, " \t\n");
        char *name_tok = strtok(NULL, "\n");
        if (!idx_tok || !name_tok) {
            return;
        }
        int idx = atoi(idx_tok) - 1;
        if (idx < 0 || idx >= MAX_WORKSPACES) {
            return;
        }
        while (*name_tok && isspace((unsigned char)*name_tok)) {
            name_tok++;
        }
        if (!*name_tok) {
            return;
        }
        char expanded[64];
        expand_config_vars(name_tok, expanded, sizeof(expanded));
        copy_trimmed(workspace_names[idx], sizeof(workspace_names[idx]), expanded);
        return;
    }
    if (!strcmp(cmd, "bar_cpu_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_cpu_color, tok, bar_cpu_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_mem_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_mem_color, tok, bar_mem_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_time_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_time_color, tok, bar_time_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_wifi_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_wifi_color, tok, bar_wifi_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_wifi_down_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_wifi_down_color, tok, bar_wifi_down_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ethernet_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_ethernet_color, tok, bar_ethernet_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ethernet_down_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_ethernet_down_color, tok, bar_ethernet_down_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ip_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_ip_color, tok, bar_ip_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_ip_down_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_ip_down_color, tok, bar_ip_down_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_battery_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_battery_color, tok, bar_battery_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_cputemp_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_cputemp_color, tok, bar_cputemp_color);
        }
        return;
    }
    if (!strcmp(cmd, "bar_disk_color")) {
        char *tok = strtok(NULL, " \t\n");
        if (tok) {
            set_bar_color(&bar_disk_color, tok, bar_disk_color);
        }
        return;
    }
    if (!strcmp(cmd, "monitor_workspaces")) {
        char *monitor_tok = strtok(NULL, " \t\n");
        char *spec_tok = strtok(NULL, "\n");
        if (!monitor_tok || !spec_tok) {
            return;
        }
        while (*spec_tok && isspace((unsigned char)*spec_tok)) {
            spec_tok++;
        }
        int mon_idx = atoi(monitor_tok) - 1;
        if (mon_idx < 0 || mon_idx >= MAX_MONITORS) {
            return;
        }
        unsigned int mask = parse_workspace_spec(spec_tok);
        if (mask != 0) {
            configured_ws_masks[mon_idx] = mask;
            configured_ws_mask_set[mon_idx] = 1;
        }
        return;
    }
    if (!strcmp(cmd, "rule_class") || !strcmp(cmd, "rule_instance") || !strcmp(cmd, "rule_title")) {
        char *match_tok = strtok(NULL, " \t\n");
        char *rule_tok = strtok(NULL, " \t\n");
        char *arg_tok = strtok(NULL, " \t\n");
        if (!match_tok || !rule_tok) {
            return;
        }
        char expanded_match[128];
        expand_config_vars(match_tok, expanded_match, sizeof(expanded_match));
        Rule *r = NULL;
        if (!strcmp(cmd, "rule_class")) {
            r = ensure_rule(expanded_match, NULL, NULL);
        } else if (!strcmp(cmd, "rule_instance")) {
            r = ensure_rule(NULL, expanded_match, NULL);
        } else {
            r = ensure_rule(NULL, NULL, expanded_match);
        }
        if (!r) {
            return;
        }
        if (!strcasecmp(rule_tok, "float")) {
            r->set_floating = 1;
            r->floating = 1;
        } else if (!strcasecmp(rule_tok, "tile")) {
            r->set_floating = 1;
            r->floating = 0;
        } else if (!strcasecmp(rule_tok, "ws") || !strcasecmp(rule_tok, "workspace")) {
            if (arg_tok) {
                int ws = atoi(arg_tok) - 1;
                if (ws >= 0 && ws < MAX_WORKSPACES) {
                    r->workspace = ws;
                }
            }
        } else if (!strcasecmp(rule_tok, "monitor")) {
            if (arg_tok) {
                int mon = atoi(arg_tok) - 1;
                if (mon >= 0 && mon < MAX_MONITORS) {
                    r->monitor = mon;
                }
            }
        }
        return;
    }
    if (strcmp(cmd, "bind") != 0) {
        return;
    }

    char *modtok = strtok(NULL, " \t\n");
    char *keytok = strtok(NULL, " \t\n");
    char *acttok = strtok(NULL, " \t\n");
    char *argtok = strtok(NULL, "\n");

    if (!modtok || !keytok || !acttok) {
        return;
    }

    char modbuf[128];
    strncpy(modbuf, modtok, sizeof(modbuf) - 1);
    modbuf[sizeof(modbuf) - 1] = '\0';
    unsigned int mod = parse_modifiers(modbuf);

    ActionType act = ACT_NONE;
    int iarg = 0;
    char argbuf[256] = {0};

    if (!strcmp(acttok, "spawn")) {
        act = ACT_SPAWN;
        if (argtok) {
            while (*argtok && isspace((unsigned char)*argtok)) {
                argtok++;
            }
            strncpy(argbuf, argtok, sizeof(argbuf) - 1);
        }
    } else if (!strcmp(acttok, "focus_next")) {
        act = ACT_FOCUS_NEXT;
    } else if (!strcmp(acttok, "focus_prev")) {
        act = ACT_FOCUS_PREV;
    } else if (!strcmp(acttok, "focus_left")) {
        act = ACT_FOCUS_LEFT;
    } else if (!strcmp(acttok, "focus_right")) {
        act = ACT_FOCUS_RIGHT;
    } else if (!strcmp(acttok, "focus_up")) {
        act = ACT_FOCUS_UP;
    } else if (!strcmp(acttok, "focus_down")) {
        act = ACT_FOCUS_DOWN;
    } else if (!strcmp(acttok, "kill")) {
        act = ACT_KILL;
    } else if (!strcmp(acttok, "quit")) {
        act = ACT_QUIT;
    } else if (!strcmp(acttok, "reload")) {
        act = ACT_RELOAD;
    } else if (!strcmp(acttok, "ws")) {
        act = ACT_WS;
        if (argtok) {
            iarg = atoi(argtok) - 1;
        }
    } else if (!strcmp(acttok, "move_to_ws")) {
        act = ACT_MOVE_TO_WS;
        if (argtok) {
            iarg = atoi(argtok) - 1;
        }
    } else if (!strcmp(acttok, "toggle_float")) {
        act = ACT_TOGGLE_FLOAT;
    } else if (!strcmp(acttok, "move_float")) {
        act = ACT_MOVE_FLOAT;
        if (argtok) {
            while (*argtok && isspace((unsigned char)*argtok)) {
                argtok++;
            }
            strncpy(argbuf, argtok, sizeof(argbuf) - 1);
        }
    } else if (!strcmp(acttok, "resize_float")) {
        act = ACT_RESIZE_FLOAT;
        if (argtok) {
            while (*argtok && isspace((unsigned char)*argtok)) {
                argtok++;
            }
            strncpy(argbuf, argtok, sizeof(argbuf) - 1);
        }
    } else if (!strcmp(acttok, "split_toggle")) {
        act = ACT_SPLIT_TOGGLE;
    } else if (!strcmp(acttok, "rotate_tree")) {
        act = ACT_ROTATE_TREE;
    } else if (!strcmp(acttok, "swap_next")) {
        act = ACT_SWAP_NEXT;
    } else if (!strcmp(acttok, "fullscreen")) {
        act = ACT_TOGGLE_FULLSCREEN;
    } else if (!strcmp(acttok, "scratchpad")) {
        act = ACT_SCRATCHPAD_TOGGLE;
    } else if (!strcmp(acttok, "restart")) {
        act = ACT_RESTART;
    }

    if (act == ACT_NONE) {
        fprintf(stderr, "unknown action: %s\n", acttok);
        config_error_count++;
        return;
    }

    if ((act == ACT_WS || act == ACT_MOVE_TO_WS) && (iarg < 0 || iarg >= MAX_WORKSPACES)) {
        return;
    }

    char expanded_arg[256];
    expanded_arg[0] = '\0';
    if (argbuf[0]) {
        expand_config_vars(argbuf, expanded_arg, sizeof(expanded_arg));
    }
    add_keybind(mod, keytok, act, expanded_arg[0] ? expanded_arg : NULL, iarg);
}

static void preparse_primary_mod_line(char *line)
{
    char *s = line;
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s || *s == '#') {
        return;
    }

    char *cmd = strtok(s, " \t\n");
    if (!cmd) {
        return;
    }

    if (!strcmp(cmd, "setmod")) {
        char *modname = strtok(NULL, " \t\n");
        if (modname) {
            primary_mod_mask = parse_primary_mod(modname);
        }
        return;
    }

    if (!strcmp(cmd, "set")) {
        char *what = strtok(NULL, " \t\n");
        char *value = strtok(NULL, " \t\n");
        if (what && value && !strcmp(what, "mod")) {
            primary_mod_mask = parse_primary_mod(value);
        }
    }
}

void load_keybinds(void)
{
    primary_mod_mask = Mod1Mask;
    focus_policy = FOCUS_CLICK;
    bar_enabled = 1;
    ipc_enabled = 1;
    config_error_count = 0;
    wm_log_level = WM_LOG_ERROR;
    snprintf(wm_log_path, sizeof(wm_log_path), "/tmp/lxwm.log");
    spawn_target_monitor = NULL;
    spawn_target_time = 0;
    snprintf(wm_font_name, sizeof(wm_font_name), "monospace");
    wm_font_size = 12;
    bar_ws_gap = 0;
    bar_ws_pad_x = 0;
    bar_ws_pad_y = 0;
    bar_divider_min_w = 80;
    bar_ws_slot_w = 28;
    bar_right_zone_min_w = 220;
    bar_module_slot_w = 86;
    bar_modules_order[0] = BAR_MOD_CPU;
    bar_modules_order[1] = BAR_MOD_MEM;
    bar_modules_order[2] = BAR_MOD_TIME;
    bar_modules_count = 2;
    snprintf(bar_module_sep, sizeof(bar_module_sep), "  ");
    bar_module_gap = 10;
    snprintf(bar_cpu_fmt, sizeof(bar_cpu_fmt), "CPU {value}");
    snprintf(bar_mem_fmt, sizeof(bar_mem_fmt), "MEM {value}");
    snprintf(bar_time_fmt, sizeof(bar_time_fmt), "%%H:%%M");
    snprintf(bar_wifi_fmt, sizeof(bar_wifi_fmt), "WIFI {value}");
    snprintf(bar_ethernet_fmt, sizeof(bar_ethernet_fmt), "ETH {value}");
    snprintf(bar_ip_fmt, sizeof(bar_ip_fmt), "IP {value}");
    snprintf(bar_battery_fmt, sizeof(bar_battery_fmt), "BAT {value}");
    snprintf(bar_cputemp_fmt, sizeof(bar_cputemp_fmt), "TEMP {value}");
    snprintf(bar_disk_fmt, sizeof(bar_disk_fmt), "DISK {value}");
    snprintf(bar_disk_mode, sizeof(bar_disk_mode), "used_total");
    snprintf(bar_disk_path, sizeof(bar_disk_path), "/");
    color_ws_active = get_color("#2f3f3f", color_focus);
    color_ws_inactive = get_color("#232323", color_unfocus);
    color_bar_mid = get_color("#2f3f3f", color_focus);
    color_titlebar_focus = color_focus;
    color_titlebar_unfocus = color_unfocus;
    color_border_focus = color_focus;
    color_border_unfocus = color_unfocus;
    bar_cpu_color = color_fg;
    bar_mem_color = color_fg;
    bar_time_color = color_fg;
    bar_wifi_color = get_color("#0b5f5f", color_fg);
    bar_wifi_down_color = get_color("#a05555", color_fg);
    bar_ethernet_color = get_color("#0b5f5f", color_fg);
    bar_ethernet_down_color = get_color("#a05555", color_fg);
    bar_ip_color = get_color("#0b5f5f", color_fg);
    bar_ip_down_color = get_color("#a05555", color_fg);
    bar_battery_color = color_fg;
    bar_cputemp_color = color_fg;
    bar_disk_color = color_fg;
    color_ws_urgent = get_color("#8c4a4a", color_focus);
    autostart_count = 0;
    rule_count = 0;
    config_var_count = 0;
    scratchpad_client = NULL;
    for (int i = 0; i < MAX_WORKSPACES; i++) {
        snprintf(workspace_names[i], sizeof(workspace_names[i]), "%d", i + 1);
    }
    for (int i = 0; i < MAX_MONITORS; i++) {
        configured_ws_masks[i] = all_workspaces_mask();
        configured_ws_mask_set[i] = 0;
    }

    const char *home = getenv("HOME");
    if (!home) {
        load_default_keybinds();
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.config/lxwm/config", home);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        load_default_keybinds();
        return;
    }

    char line[512];
    char scoped_line[512];
    char section[64];
    snprintf(section, sizeof(section), "global");

    while (fgets(line, sizeof(line), fp)) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%s", line);
        if (parse_section_header_line(probe, section, sizeof(section))) {
            continue;
        }
        map_scoped_line(section, line, scoped_line, sizeof(scoped_line));
        if (scoped_line[0]) {
            preparse_primary_mod_line(scoped_line);
        }
    }
    rewind(fp);
    load_default_keybinds();

    snprintf(section, sizeof(section), "global");

    while (fgets(line, sizeof(line), fp)) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%s", line);
        if (parse_section_header_line(probe, section, sizeof(section))) {
            continue;
        }
        map_scoped_line(section, line, scoped_line, sizeof(scoped_line));
        if (scoped_line[0]) {
            parse_config_line(scoped_line);
        }
    }
    fclose(fp);
}
