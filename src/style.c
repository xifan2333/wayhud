#define _GNU_SOURCE
#include "style.h"
#include "protocols/wlr-layer-shell-unstable-v1-client-protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RULES 32
#define MAX_SELECTOR_LEN 64
#define MAX_DECL_LEN 1024

typedef struct {
    char selector[MAX_SELECTOR_LEN];
    char declarations[MAX_DECL_LEN];
} css_rule_t;

typedef void (*prop_handler_t)(wayhud_style_t *style, const char *val);

typedef struct {
    const char *name;
    prop_handler_t handler;
} css_prop_spec_t;

/* --- String Utilities --- */

static void str_trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static void str_unquote(char *s) {
    str_trim(s);
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '"' && s[len - 1] == '"') || (s[0] == '\'' && s[len - 1] == '\''))) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
        str_trim(s);
    }
}

static void strip_comments(char *s) {
    char *p = s;
    while ((p = strstr(p, "/*")) != NULL) {
        char *end = strstr(p + 2, "*/");
        if (end) {
            memset(p, ' ', (size_t)(end + 2 - p));
            p = end + 2;
        } else {
            memset(p, ' ', strlen(p));
            break;
        }
    }
}

/* --- Value Parsers --- */

static int parse_px(const char *val, int fallback) {
    if (!val || !val[0]) return fallback;
    char *end = NULL;
    long n = strtol(val, &end, 10);
    return end != val ? (int)n : fallback;
}

static int parse_duration_ms(const char *val, int fallback) {
    if (!val || !val[0]) return fallback;
    if (strcasecmp(val, "none") == 0 || strcasecmp(val, "0") == 0 || strcasecmp(val, "0s") == 0 ||
        strcasecmp(val, "0ms") == 0) {
        return 0;
    }
    char *end = NULL;
    double d = strtod(val, &end);
    if (end == val) return fallback;

    while (*end && isspace((unsigned char)*end))
        end++;
    if (strcasecmp(end, "s") == 0) {
        return (int)(d * 1000.0);
    }
    return (int)d;
}

static wayhud_color_t parse_color(const char *val, wayhud_color_t fallback) {
    if (!val || !val[0]) return fallback;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", val);
    str_trim(buf);

    if (strcasecmp(buf, "transparent") == 0) return (wayhud_color_t){0, 0, 0, 0};
    if (strcasecmp(buf, "white") == 0) return (wayhud_color_t){255, 255, 255, 255};
    if (strcasecmp(buf, "black") == 0) return (wayhud_color_t){0, 0, 0, 255};

    if (buf[0] == '#') {
        const char *hex = buf + 1;
        size_t len = strlen(hex);
        unsigned int r = 0, g = 0, b = 0, a = 255;
        if (len == 3 && sscanf(hex, "%1x%1x%1x", &r, &g, &b) == 3) {
            return (wayhud_color_t){(uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(b * 17), 255};
        }
        if (len == 4 && sscanf(hex, "%1x%1x%1x%1x", &r, &g, &b, &a) == 4) {
            return (wayhud_color_t){(uint8_t)(r * 17), (uint8_t)(g * 17), (uint8_t)(b * 17),
                                    (uint8_t)(a * 17)};
        }
        if (len == 6 && sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
            return (wayhud_color_t){(uint8_t)r, (uint8_t)g, (uint8_t)b, 255};
        }
        if (len == 8 && sscanf(hex, "%02x%02x%02x%02x", &r, &g, &b, &a) == 4) {
            return (wayhud_color_t){(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a};
        }
        return fallback;
    }

    if (strncasecmp(buf, "rgba", 4) == 0 || strncasecmp(buf, "rgb", 3) == 0) {
        const char *p = strchr(buf, '(');
        if (p) {
            p++;
            int r = 0, g = 0, b = 0;
            float a = 1.0f;
            if (sscanf(p, "%d , %d , %d , %f", &r, &g, &b, &a) >= 3 ||
                sscanf(p, "%d %d %d / %f", &r, &g, &b, &a) >= 3 ||
                sscanf(p, "%d %d %d", &r, &g, &b) == 3) {
                if (a < 0.0f) a = 0.0f;
                if (a > 1.0f) a = 1.0f;
                return (wayhud_color_t){(uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r)),
                                        (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g)),
                                        (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b)),
                                        (uint8_t)(a * 255.0f + 0.5f)};
            }
        }
    }

    return fallback;
}

static void parse_box_model_values(const char *val, int *t, int *r, int *b, int *l) {
    char copy[128];
    snprintf(copy, sizeof(copy), "%s", val);

    int vals[4] = {0};
    int count = 0;

    char *saveptr = NULL;
    char *tok = strtok_r(copy, " \t,", &saveptr);
    while (tok && count < 4) {
        str_trim(tok);
        if (tok[0]) {
            vals[count++] = parse_px(tok, 0);
        }
        tok = strtok_r(NULL, " \t,", &saveptr);
    }

    switch (count) {
    case 1:
        *t = *r = *b = *l = vals[0];
        break;
    case 2:
        *t = *b = vals[0];
        *r = *l = vals[1];
        break;
    case 3:
        *t = vals[0];
        *r = *l = vals[1];
        *b = vals[2];
        break;
    case 4:
        *t = vals[0];
        *r = *l = vals[1];
        *b = vals[2];
        *l = vals[3];
        break;
    default:
        break;
    }
}

static void recompute_anchor(wayhud_style_t *st) {
    uint32_t a = 0;
    if (st->has_margin_top) a |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    if (st->has_margin_bottom) a |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    if (st->has_margin_left) a |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (st->has_margin_right) a |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

    st->anchor = a ? a : (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
}

static void reset_margins(wayhud_style_t *style) {
    style->margin_top = 0;
    style->margin_bottom = 0;
    style->margin_left = 0;
    style->margin_right = 0;
    style->has_margin_top = false;
    style->has_margin_bottom = false;
    style->has_margin_left = false;
    style->has_margin_right = false;
}

/* --- Table-Driven Property Handlers (Window Scope) --- */

static void handle_win_margin_top(wayhud_style_t *st, const char *val) {
    st->margin_top = parse_px(val, st->margin_top);
    st->has_margin_top = true;
    recompute_anchor(st);
}

static void handle_win_margin_bottom(wayhud_style_t *st, const char *val) {
    st->margin_bottom = parse_px(val, st->margin_bottom);
    st->has_margin_bottom = true;
    recompute_anchor(st);
}

static void handle_win_margin_left(wayhud_style_t *st, const char *val) {
    st->margin_left = parse_px(val, st->margin_left);
    st->has_margin_left = true;
    recompute_anchor(st);
}

static void handle_win_margin_right(wayhud_style_t *st, const char *val) {
    st->margin_right = parse_px(val, st->margin_right);
    st->has_margin_right = true;
    recompute_anchor(st);
}

static void handle_win_margin(wayhud_style_t *st, const char *val) {
    reset_margins(st);
    int t = 0, r = 0, b = 0, l = 0;
    parse_box_model_values(val, &t, &r, &b, &l);
    st->margin_top = t;
    st->margin_right = r;
    st->margin_bottom = b;
    st->margin_left = l;
    st->has_margin_top = (t > 0);
    st->has_margin_right = (r > 0);
    st->has_margin_bottom = (b > 0);
    st->has_margin_left = (l > 0);
    recompute_anchor(st);
}

static void handle_win_padding_top(wayhud_style_t *st, const char *val) {
    st->padding_top = parse_px(val, st->padding_top);
}

static void handle_win_padding_bottom(wayhud_style_t *st, const char *val) {
    st->padding_bottom = parse_px(val, st->padding_bottom);
}

static void handle_win_padding_left(wayhud_style_t *st, const char *val) {
    st->padding_left = parse_px(val, st->padding_left);
}

static void handle_win_padding_right(wayhud_style_t *st, const char *val) {
    st->padding_right = parse_px(val, st->padding_right);
}

static void handle_win_padding(wayhud_style_t *st, const char *val) {
    parse_box_model_values(val, &st->padding_top, &st->padding_right, &st->padding_bottom,
                           &st->padding_left);
}

static void handle_win_background(wayhud_style_t *st, const char *val) {
    st->background_color = parse_color(val, st->background_color);
}

static void handle_win_border_radius(wayhud_style_t *st, const char *val) {
    st->border_radius = parse_px(val, st->border_radius);
}

static void handle_win_border(wayhud_style_t *st, const char *val) {
    int w = 0;
    if (sscanf(val, "%d", &w) == 1 && w > 0) {
        st->border.width = w;
        st->border.enabled = true;
        const char *c_pos = strchr(val, '#');
        if (!c_pos) c_pos = strstr(val, "rgb");
        if (!c_pos) c_pos = strstr(val, "rgba");
        st->border.color = parse_color(c_pos ? c_pos : "", (wayhud_color_t){255, 255, 255, 40});
    }
}

static void handle_win_border_width(wayhud_style_t *st, const char *val) {
    st->border.width = parse_px(val, st->border.width);
    st->border.enabled = (st->border.width > 0);
}

static void handle_win_border_color(wayhud_style_t *st, const char *val) {
    st->border.color = parse_color(val, st->border.color);
}

static void handle_win_width(wayhud_style_t *st, const char *val) {
    st->width = parse_px(val, st->width);
}

static void handle_win_min_width(wayhud_style_t *st, const char *val) {
    st->min_width = parse_px(val, st->min_width);
}

static void handle_win_max_width(wayhud_style_t *st, const char *val) {
    st->max_width = parse_px(val, st->max_width);
}

static void handle_win_transition_duration(wayhud_style_t *st, const char *val) {
    st->transition_duration_ms = parse_duration_ms(val, st->transition_duration_ms);
}

static const css_prop_spec_t WINDOW_PROPERTIES[] = {
    {"margin-top", handle_win_margin_top},
    {"margin-bottom", handle_win_margin_bottom},
    {"margin-left", handle_win_margin_left},
    {"margin-right", handle_win_margin_right},
    {"margin", handle_win_margin},
    {"padding-top", handle_win_padding_top},
    {"padding-bottom", handle_win_padding_bottom},
    {"padding-left", handle_win_padding_left},
    {"padding-right", handle_win_padding_right},
    {"padding", handle_win_padding},
    {"background-color", handle_win_background},
    {"background", handle_win_background},
    {"border-radius", handle_win_border_radius},
    {"border", handle_win_border},
    {"border-width", handle_win_border_width},
    {"border-color", handle_win_border_color},
    {"width", handle_win_width},
    {"min-width", handle_win_min_width},
    {"max-width", handle_win_max_width},
    {"transition-duration", handle_win_transition_duration},
    {"animation-duration", handle_win_transition_duration},
    {NULL, NULL}};

/* --- Table-Driven Property Handlers (Label Scope) --- */

static void handle_lbl_color(wayhud_style_t *st, const char *val) {
    st->text_color = parse_color(val, st->text_color);
}

static void handle_lbl_font_family(wayhud_style_t *st, const char *val) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", val);
    char *comma = strchr(buf, ',');
    if (comma) *comma = '\0';
    str_unquote(buf);
    if (buf[0]) snprintf(st->font_family, sizeof(st->font_family), "%s", buf);
}

static void handle_lbl_font_size(wayhud_style_t *st, const char *val) {
    st->font_size = parse_px(val, st->font_size);
}

static void handle_lbl_font_weight(wayhud_style_t *st, const char *val) {
    if (strcasecmp(val, "bold") == 0 || strcasecmp(val, "700") == 0) {
        st->font_weight = 700;
    } else if (strcasecmp(val, "normal") == 0 || strcasecmp(val, "400") == 0) {
        st->font_weight = 400;
    }
}

static void handle_lbl_text_align(wayhud_style_t *st, const char *val) {
    if (strcasecmp(val, "center") == 0) {
        st->text_align = 1;
    } else if (strcasecmp(val, "right") == 0) {
        st->text_align = 2;
    } else if (strcasecmp(val, "left") == 0) {
        st->text_align = 0;
    }
}

static void handle_lbl_font(wayhud_style_t *st, const char *val) {
    if (strstr(val, "bold")) st->font_weight = 700;
    const char *px_pos = strstr(val, "px");
    if (px_pos) {
        const char *start = px_pos;
        while (start > val && isdigit((unsigned char)*(start - 1)))
            start--;
        st->font_size = parse_px(start, st->font_size);
    }
    const char *quote = strchr(val, '"');
    if (!quote) quote = strchr(val, '\'');
    if (quote) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", quote);
        str_unquote(buf);
        if (buf[0]) snprintf(st->font_family, sizeof(st->font_family), "%s", buf);
    }
}

static void handle_lbl_text_shadow(wayhud_style_t *st, const char *val) {
    if (strcasecmp(val, "none") == 0 || strcasecmp(val, "0") == 0) {
        st->text_shadow.enabled = false;
        return;
    }
    int dx = 0, dy = 0, blur = 0;
    char rest[128] = {0};
    if (sscanf(val, "%dpx %dpx %dpx %127s", &dx, &dy, &blur, rest) >= 3 ||
        sscanf(val, "%d %d %d %127s", &dx, &dy, &blur, rest) >= 3 ||
        sscanf(val, "%dpx %dpx %127s", &dx, &dy, rest) >= 2 ||
        sscanf(val, "%d %d %127s", &dx, &dy, rest) >= 2) {
        st->text_shadow.dx = dx;
        st->text_shadow.dy = dy;
        st->text_shadow.blur_radius = blur;
        st->text_shadow.enabled = true;
        const char *c_start = strchr(val, '#');
        if (!c_start) c_start = strstr(val, "rgb");
        if (!c_start) c_start = strstr(val, "rgba");
        st->text_shadow.color = parse_color(c_start ? c_start : rest, st->text_shadow.color);
    }
}

static const css_prop_spec_t LABEL_PROPERTIES[] = {{"color", handle_lbl_color},
                                                   {"font-family", handle_lbl_font_family},
                                                   {"font-size", handle_lbl_font_size},
                                                   {"font-weight", handle_lbl_font_weight},
                                                   {"font", handle_lbl_font},
                                                   {"text-align", handle_lbl_text_align},
                                                   {"text-shadow", handle_lbl_text_shadow},
                                                   {NULL, NULL}};

/* --- Declarative Property Dispatcher --- */

static void dispatch_property(const css_prop_spec_t *table, wayhud_style_t *style, const char *key,
                              const char *val) {
    for (const css_prop_spec_t *spec = table; spec->name; spec++) {
        if (strcasecmp(key, spec->name) == 0) {
            spec->handler(style, val);
            return;
        }
    }
}

static void apply_declaration_block(wayhud_style_t *style, const char *decls, int target_node) {
    char copy[MAX_DECL_LEN];
    snprintf(copy, sizeof(copy), "%s", decls);

    char *saveptr = NULL;
    char *decl = strtok_r(copy, ";", &saveptr);
    while (decl) {
        char *colon = strchr(decl, ':');
        if (colon) {
            *colon = '\0';
            char *k = decl;
            char *v = colon + 1;
            str_trim(k);
            str_trim(v);
            if (target_node == 1) {
                dispatch_property(WINDOW_PROPERTIES, style, k, v);
            } else if (target_node == 2) {
                dispatch_property(LABEL_PROPERTIES, style, k, v);
            } else {
                dispatch_property(WINDOW_PROPERTIES, style, k, v);
                dispatch_property(LABEL_PROPERTIES, style, k, v);
            }
        }
        decl = strtok_r(NULL, ";", &saveptr);
    }
}

/* --- Public API --- */

void wayhud_style_init_default(wayhud_style_t *style) {
    memset(style, 0, sizeof(*style));
    style->margin_bottom = 195;
    style->margin_left = 4;
    style->has_margin_bottom = true;
    style->has_margin_left = true;
    style->anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;

    style->padding_top = 4;
    style->padding_bottom = 4;
    style->padding_left = 8;
    style->padding_right = 8;

    style->background_color = (wayhud_color_t){0, 0, 0, 0};
    style->border_radius = 8;
    style->width = 0;
    style->min_width = 0;
    style->max_width = 185;
    style->transition_duration_ms = 1200;

    snprintf(style->font_family, sizeof(style->font_family), "JetBrainsMono Nerd Font");
    style->font_size = 16;
    style->font_weight = 400;
    style->text_align = 0;
    style->text_color = (wayhud_color_t){255, 255, 255, 255};

    style->text_shadow.enabled = true;
    style->text_shadow.dx = 0;
    style->text_shadow.dy = 0;
    style->text_shadow.blur_radius = 2;
    style->text_shadow.color = (wayhud_color_t){0, 0, 0, 215};
}

int wayhud_style_parse(wayhud_style_t *style, const char *css_text, const char *instance_name) {
    if (!style || !css_text) return -1;

    char *buf = strdup(css_text);
    if (!buf) return -1;

    strip_comments(buf);

    css_rule_t rules[MAX_RULES];
    memset(rules, 0, sizeof(rules));
    int rule_count = 0;

    /* 1. Extract rules into css_rule_t array */
    char *saveptr = NULL;
    char *block = strtok_r(buf, "}", &saveptr);
    while (block && rule_count < MAX_RULES) {
        char *brace = strchr(block, '{');
        if (brace) {
            *brace = '\0';
            char *sel = block;
            char *decl_list = brace + 1;
            str_trim(sel);
            str_trim(decl_list);
            if (sel[0] && decl_list[0]) {
                snprintf(rules[rule_count].selector, sizeof(rules[0].selector), "%s", sel);
                snprintf(rules[rule_count].declarations, sizeof(rules[0].declarations), "%s",
                         decl_list);
                rule_count++;
            }
        }
        block = strtok_r(NULL, "}", &saveptr);
    }
    free(buf);

    /* 2. Apply general rules (window, label, *) */
    for (int i = 0; i < rule_count; i++) {
        const char *sel = rules[i].selector;
        int target = 0;
        if (strcasecmp(sel, "window") == 0 || strcasecmp(sel, "window#wayhud") == 0 ||
            strcasecmp(sel, ".window") == 0 || strcasecmp(sel, "*") == 0) {
            target = 1;
        } else if (strcasecmp(sel, "label") == 0 || strcasecmp(sel, ".label") == 0) {
            target = 2;
        }
        if (target > 0) {
            apply_declaration_block(style, rules[i].declarations, target);
        }
    }

    /* 3. Apply instance-specific override rules (window#id, label#id, #id) */
    if (instance_name && instance_name[0]) {
        char win_id[MAX_SELECTOR_LEN];
        char lbl_id[MAX_SELECTOR_LEN];
        char pure_id[MAX_SELECTOR_LEN];
        snprintf(win_id, sizeof(win_id), "window#%s", instance_name);
        snprintf(lbl_id, sizeof(lbl_id), "label#%s", instance_name);
        snprintf(pure_id, sizeof(pure_id), "#%s", instance_name);

        for (int i = 0; i < rule_count; i++) {
            const char *sel = rules[i].selector;
            int target = 0;
            if (strcasecmp(sel, win_id) == 0 || strcasecmp(sel, pure_id) == 0) {
                target = 1;
                if (strstr(rules[i].declarations, "margin")) {
                    reset_margins(style);
                }
            } else if (strcasecmp(sel, lbl_id) == 0) {
                target = 2;
            }
            if (target > 0) {
                apply_declaration_block(style, rules[i].declarations, target);
            }
        }
    }

    return 0;
}

int wayhud_style_load_file(wayhud_style_t *style, const char *file_path,
                           const char *instance_name) {
    char path_buf[1024];
    const char *target_path = file_path;

    if (!target_path) {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        const char *home = getenv("HOME");
        if (xdg && xdg[0]) {
            snprintf(path_buf, sizeof(path_buf), "%s/wayhud/style.css", xdg);
        } else if (home && home[0]) {
            snprintf(path_buf, sizeof(path_buf), "%s/.config/wayhud/style.css", home);
        } else {
            return -1;
        }
        target_path = path_buf;
    }

    FILE *f = fopen(target_path, "re");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1024L * 1024L) {
        fclose(f);
        return -1;
    }

    char *buf = calloc((size_t)sz + 1, 1);
    if (!buf) {
        fclose(f);
        return -1;
    }

    size_t n = fread(buf, 1, (size_t)sz, f);
    (void)n;
    fclose(f);

    int ret = wayhud_style_parse(style, buf, instance_name);
    free(buf);
    return ret;
}
