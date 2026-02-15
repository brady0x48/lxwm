#ifndef LXWM_DECOR_H
#define LXWM_DECOR_H

#include "wm_state.h"

char *window_title(Window w, char *buf, size_t bufsz);
void update_system_stats(void);
void draw_titlebar(Client *c);
void load_wm_font(void);
int text_width_px(const char *s);
void draw_text(Drawable dr, int x, int baseline_y, const char *s);
void apply_template_format(const char *templ, const char *const *keys, const char *const *values,
                           size_t nkeys, char *out, size_t outsz);
void apply_value_format(const char *templ, const char *value, char *out, size_t outsz);

#endif
