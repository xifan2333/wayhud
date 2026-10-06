#ifndef WAYHUD_RENDER_H
#define WAYHUD_RENDER_H

#include "protocols/wlr-layer-shell-unstable-v1-client-protocol.h"
#include "style.h"
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

typedef struct {
    struct wl_buffer *wl_buf;
    void *shm_data;
    size_t shm_size;
    int width;
    int height;
} wayhud_buffer_t;

typedef struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    wayhud_style_t style;

    int output_width;

    char current_text[256];
    bool visible;
    int64_t last_keypress_ms;
    bool configured;

    /* Current rendered frame */
    wayhud_buffer_t current_buffer;

    /* 1x1 Transparent blank buffer to keep layer mapped during hide */
    wayhud_buffer_t blank_buffer;
} wayhud_render_t;

int wayhud_render_init(wayhud_render_t *r, const wayhud_style_t *style);
void wayhud_render_destroy(wayhud_render_t *r);
void wayhud_render_show_text(wayhud_render_t *r, const char *text);
void wayhud_render_set_style(wayhud_render_t *r, const wayhud_style_t *style);
void wayhud_render_tick(wayhud_render_t *r);
int wayhud_render_get_fd(wayhud_render_t *r);
void wayhud_render_dispatch(wayhud_render_t *r);

#endif /* WAYHUD_RENDER_H */
