#ifndef WAYHUD_RENDER_H
#define WAYHUD_RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include "protocols/wlr-layer-shell-unstable-v1-client-protocol.h"

typedef struct {
    uint8_t r, g, b, a;
} wayhud_color_t;

typedef struct {
    char font[128];
    wayhud_color_t fg_color;
    wayhud_color_t bg_color;
    wayhud_color_t special_color;

    uint32_t anchor;
    int margin_top;
    int margin_right;
    int margin_bottom;
    int margin_left;
    int max_width;
    int timeout_ms;
} wayhud_config_t;

typedef struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    wayhud_config_t cfg;

    char current_text[256];
    bool visible;
    int64_t last_keypress_ms;
    bool configured;

    /* Current buffer */
    struct wl_buffer *buffer;
    void *shm_data;
    size_t shm_size;
    int cur_width;
    int cur_height;
} wayhud_render_t;

int wayhud_render_init(wayhud_render_t *r, const wayhud_config_t *cfg);
void wayhud_render_destroy(wayhud_render_t *r);
void wayhud_render_show_text(wayhud_render_t *r, const char *text);
void wayhud_render_tick(wayhud_render_t *r);
int wayhud_render_get_fd(wayhud_render_t *r);
void wayhud_render_dispatch(wayhud_render_t *r);

#endif /* WAYHUD_RENDER_H */
