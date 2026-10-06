#ifndef WAYHUD_STYLE_H
#define WAYHUD_STYLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t r, g, b, a;
} wayhud_color_t;

typedef struct {
    int dx;
    int dy;
    int blur_radius;
    wayhud_color_t color;
    bool enabled;
} wayhud_shadow_t;

typedef struct {
    int width;
    wayhud_color_t color;
    bool enabled;
} wayhud_border_t;

typedef struct {
    /* Box Model Margins */
    int margin_top;
    int margin_right;
    int margin_bottom;
    int margin_left;
    bool has_margin_top;
    bool has_margin_right;
    bool has_margin_bottom;
    bool has_margin_left;

    /* Box Model Paddings */
    int padding_top;
    int padding_right;
    int padding_bottom;
    int padding_left;

    /* Positioning (Wayland layer-shell anchor mask) */
    uint32_t anchor;

    /* Background & Border */
    wayhud_color_t background_color;
    int border_radius;
    wayhud_border_t border;

    /* Sizing & Timing (GTK CSS standard box sizing in pixels) */
    int width;     /* Explicit fixed width (0 = content adaptive) */
    int min_width; /* Minimum width lower bound */
    int max_width; /* Maximum width upper bound */
    int transition_duration_ms;

    /* Typography & Text */
    char font_family[512];
    int font_size;
    int font_weight; /* 400 = normal, 700 = bold */
    int text_align;  /* 0: left, 1: center, 2: right */
    wayhud_color_t text_color;
    wayhud_shadow_t text_shadow;
} wayhud_style_t;

void wayhud_style_init_default(wayhud_style_t *style);
int wayhud_style_parse(wayhud_style_t *style, const char *css_text, const char *instance_name);
/* out must have room for PATH_MAX bytes. Resolves symlinks to their source. */
int wayhud_style_resolve_path(const char *file_path, char *out);
int wayhud_style_load_file(wayhud_style_t *style, const char *file_path, const char *instance_name);

#endif /* WAYHUD_STYLE_H */
