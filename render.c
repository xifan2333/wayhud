#define _GNU_SOURCE
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

static int64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int create_shm_file(size_t size) {
    int fd = memfd_create("wayhud-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        char template[] = "/tmp/wayhud-shm-XXXXXX";
        fd = mkstemp(template);
        if (fd >= 0) unlink(template);
    }
    if (fd >= 0 && ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                   uint32_t serial, uint32_t width, uint32_t height) {
    (void)width; (void)height;
    wayhud_render_t *r = data;
    r->configured = true;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    (void)surface;
    wayhud_render_t *r = data;
    r->configured = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *interface, uint32_t version) {
    (void)version;
    wayhud_render_t *r = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        r->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        r->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        r->layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 4);
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
    (void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int wayhud_render_init(wayhud_render_t *r, const wayhud_config_t *cfg) {
    memset(r, 0, sizeof(*r));
    r->cfg = *cfg;

    r->display = wl_display_connect(NULL);
    if (!r->display) {
        fprintf(stderr, "wayhud: cannot connect to Wayland display\n");
        return -1;
    }

    r->registry = wl_display_get_registry(r->display);
    wl_registry_add_listener(r->registry, &registry_listener, r);
    wl_display_roundtrip(r->display);

    if (!r->compositor || !r->shm || !r->layer_shell) {
        fprintf(stderr, "wayhud: compositor lacks required Wayland protocols (wlr-layer-shell)\n");
        return -1;
    }

    /* Create layer shell surface */
    r->surface = wl_compositor_create_surface(r->compositor);
    r->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        r->layer_shell, r->surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wayhud"
    );

    zwlr_layer_surface_v1_add_listener(r->layer_surface, &layer_surface_listener, r);
    zwlr_layer_surface_v1_set_anchor(r->layer_surface, r->cfg.anchor);
    zwlr_layer_surface_v1_set_margin(
        r->layer_surface,
        r->cfg.margin_top,
        r->cfg.margin_right,
        r->cfg.margin_bottom,
        r->cfg.margin_left
    );
    zwlr_layer_surface_v1_set_exclusive_zone(r->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(r->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    /* 100% Click-through: empty input region */
    struct wl_region *empty_region = wl_compositor_create_region(r->compositor);
    wl_surface_set_input_region(r->surface, empty_region);
    wl_region_destroy(empty_region);

    /* Initial commit with 1x1 to establish mapping */
    zwlr_layer_surface_v1_set_size(r->layer_surface, 1, 1);
    wl_surface_commit(r->surface);
    wl_display_roundtrip(r->display);

    return 0;
}

static void destroy_shm_buffer(wayhud_render_t *r) {
    if (r->buffer) {
        wl_buffer_destroy(r->buffer);
        r->buffer = NULL;
    }
    if (r->shm_data && r->shm_data != MAP_FAILED) {
        munmap(r->shm_data, r->shm_size);
        r->shm_data = NULL;
    }
    r->shm_size = 0;
}

void wayhud_render_destroy(wayhud_render_t *r) {
    destroy_shm_buffer(r);
    if (r->layer_surface) zwlr_layer_surface_v1_destroy(r->layer_surface);
    if (r->surface) wl_surface_destroy(r->surface);
    if (r->layer_shell) zwlr_layer_shell_v1_destroy(r->layer_shell);
    if (r->shm) wl_shm_destroy(r->shm);
    if (r->compositor) wl_compositor_destroy(r->compositor);
    if (r->registry) wl_registry_destroy(r->registry);
    if (r->display) wl_display_disconnect(r->display);
}

void wayhud_render_show_text(wayhud_render_t *r, const char *text) {
    if (!r->surface || !text || !text[0]) return;

    strncpy(r->current_text, text, sizeof(r->current_text) - 1);
    r->last_keypress_ms = get_time_ms();
    r->visible = true;

    /* Measure text layout */
    cairo_surface_t *measure_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *measure_cr = cairo_create(measure_surf);
    PangoLayout *layout = pango_cairo_create_layout(measure_cr);

    PangoFontDescription *desc = pango_font_description_from_string(r->cfg.font);
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    pango_layout_set_text(layout, r->current_text, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_START);
    if (r->cfg.max_width > 0) {
        pango_layout_set_width(layout, r->cfg.max_width * PANGO_SCALE);
    }

    int text_w = 0, text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    int pad_x = 6;
    int pad_y = 4;
    int width = text_w + pad_x * 2;
    int height = text_h + pad_y * 2;

    if (r->cfg.max_width > 0 && width > r->cfg.max_width) {
        width = r->cfg.max_width;
    }
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    g_object_unref(layout);
    cairo_destroy(measure_cr);
    cairo_surface_destroy(measure_surf);

    /* Allocate buffer */
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    size_t size = (size_t)stride * height;

    destroy_shm_buffer(r);

    int fd = create_shm_file(size);
    if (fd < 0) return;

    r->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    r->shm_size = size;

    struct wl_shm_pool *pool = wl_shm_create_pool(r->shm, fd, size);
    r->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    /* Draw with Cairo / Pango */
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        (unsigned char *)r->shm_data, CAIRO_FORMAT_ARGB32, width, height, stride
    );
    cairo_t *cr = cairo_create(surf);

    /* Background: 100% transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (r->cfg.bg_color.a > 0) {
        cairo_set_source_rgba(cr,
            r->cfg.bg_color.r / 255.0,
            r->cfg.bg_color.g / 255.0,
            r->cfg.bg_color.b / 255.0,
            r->cfg.bg_color.a / 255.0
        );
        cairo_paint(cr);
    }

    PangoLayout *draw_layout = pango_cairo_create_layout(cr);
    PangoFontDescription *draw_desc = pango_font_description_from_string(r->cfg.font);
    pango_layout_set_font_description(draw_layout, draw_desc);
    pango_font_description_free(draw_desc);
    pango_layout_set_text(draw_layout, r->current_text, -1);
    pango_layout_set_ellipsize(draw_layout, PANGO_ELLIPSIZE_START);
    if (r->cfg.max_width > 0) {
        pango_layout_set_width(draw_layout, (width - pad_x * 2) * PANGO_SCALE);
    }

    /* 1. Subtle dark drop shadow outline for crisp contrast on any background */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx != 0 || dy != 0) {
                cairo_move_to(cr, pad_x + dx, pad_y + dy);
                pango_cairo_show_layout(cr, draw_layout);
            }
        }
    }

    /* 2. Foreground crisp text */
    cairo_set_source_rgba(cr,
        r->cfg.fg_color.r / 255.0,
        r->cfg.fg_color.g / 255.0,
        r->cfg.fg_color.b / 255.0,
        r->cfg.fg_color.a / 255.0
    );
    cairo_move_to(cr, pad_x, pad_y);
    pango_cairo_show_layout(cr, draw_layout);

    g_object_unref(draw_layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    /* Submit to Wayland Layer Surface */
    r->cur_width = width;
    r->cur_height = height;

    zwlr_layer_surface_v1_set_size(r->layer_surface, width, height);
    wl_surface_attach(r->surface, r->buffer, 0, 0);
    wl_surface_damage_buffer(r->surface, 0, 0, width, height);
    wl_surface_commit(r->surface);
    wl_display_flush(r->display);
}

void wayhud_render_tick(wayhud_render_t *r) {
    if (!r->visible) return;

    int64_t elapsed = get_time_ms() - r->last_keypress_ms;
    if (elapsed >= r->cfg.timeout_ms) {
        /* Disappear cleanly */
        r->visible = false;
        r->current_text[0] = '\0';
        destroy_shm_buffer(r);
        zwlr_layer_surface_v1_set_size(r->layer_surface, 1, 1);
        wl_surface_attach(r->surface, NULL, 0, 0);
        wl_surface_commit(r->surface);
        wl_display_flush(r->display);
    }
}

int wayhud_render_get_fd(wayhud_render_t *r) {
    return wl_display_get_fd(r->display);
}

void wayhud_render_dispatch(wayhud_render_t *r) {
    while (wl_display_prepare_read(r->display) != 0) {
        wl_display_dispatch_pending(r->display);
    }
    wl_display_read_events(r->display);
    wl_display_dispatch_pending(r->display);
}
