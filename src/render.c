#define _GNU_SOURCE
#include "render.h"
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* --- Time Utility --- */

static int64_t get_time_ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* --- Shared Memory Buffer Subsystem --- */

static int create_shm_fd(size_t size) {
    int fd = memfd_create("wayhud-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        char template[] = "/tmp/wayhud-shm-XXXXXX";
        fd = mkstemp(template);
        if (fd >= 0) unlink(template);
    }
    if (fd >= 0 && ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int wayhud_buffer_create(struct wl_shm *shm, int width, int height, wayhud_buffer_t *buf) {
    memset(buf, 0, sizeof(*buf));
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    size_t size = (size_t)stride * height;

    int fd = create_shm_fd(size);
    if (fd < 0) return -1;

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return -1;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer *wl_buf =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    if (!wl_buf) {
        munmap(data, size);
        return -1;
    }

    buf->wl_buf = wl_buf;
    buf->shm_data = data;
    buf->shm_size = size;
    buf->width = width;
    buf->height = height;
    return 0;
}

static void wayhud_buffer_destroy(wayhud_buffer_t *buf) {
    if (buf->wl_buf) {
        wl_buffer_destroy(buf->wl_buf);
        buf->wl_buf = NULL;
    }
    if (buf->shm_data && buf->shm_data != MAP_FAILED) {
        munmap(buf->shm_data, buf->shm_size);
        buf->shm_data = NULL;
    }
    buf->shm_size = 0;
    buf->width = 0;
    buf->height = 0;
}

/* --- Cairo Drawing Subsystem --- */

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius) {
    if (radius <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    if (radius > w / 2.0) radius = w / 2.0;
    if (radius > h / 2.0) radius = h / 2.0;
    double deg = M_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius, radius, -90.0 * deg, 0.0 * deg);
    cairo_arc(cr, x + w - radius, y + h - radius, radius, 0.0 * deg, 90.0 * deg);
    cairo_arc(cr, x + radius, y + h - radius, radius, 90.0 * deg, 180.0 * deg);
    cairo_arc(cr, x + radius, y + radius, radius, 180.0 * deg, 270.0 * deg);
    cairo_close_path(cr);
}

static void draw_box_decorations(cairo_t *cr, const wayhud_style_t *st, int width, int height) {
    /* 1. Background Fill */
    if (st->background_color.a > 0) {
        draw_rounded_rect(cr, 0, 0, width, height, st->border_radius);
        cairo_set_source_rgba(cr, st->background_color.r / 255.0, st->background_color.g / 255.0,
                              st->background_color.b / 255.0, st->background_color.a / 255.0);
        cairo_fill(cr);
    }

    /* 2. Border */
    if (st->border.enabled && st->border.width > 0) {
        double bw = st->border.width;
        draw_rounded_rect(cr, bw / 2.0, bw / 2.0, width - bw, height - bw, st->border_radius);
        cairo_set_line_width(cr, bw);
        cairo_set_source_rgba(cr, st->border.color.r / 255.0, st->border.color.g / 255.0,
                              st->border.color.b / 255.0, st->border.color.a / 255.0);
        cairo_stroke(cr);
    }
}

static void draw_text_with_shadow(cairo_t *cr, const wayhud_style_t *st, PangoLayout *layout,
                                  double tx, double ty) {
    /* 1. Shadow */
    if (st->text_shadow.enabled) {
        cairo_set_source_rgba(cr, st->text_shadow.color.r / 255.0, st->text_shadow.color.g / 255.0,
                              st->text_shadow.color.b / 255.0, st->text_shadow.color.a / 255.0);
        int blur = st->text_shadow.blur_radius;
        if (blur <= 1) {
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx != 0 || dy != 0) {
                        cairo_move_to(cr, tx + st->text_shadow.dx + dx,
                                      ty + st->text_shadow.dy + dy);
                        pango_cairo_show_layout(cr, layout);
                    }
                }
            }
        } else {
            for (int dx = -blur; dx <= blur; dx++) {
                for (int dy = -blur; dy <= blur; dy++) {
                    if (dx * dx + dy * dy <= blur * blur) {
                        cairo_move_to(cr, tx + st->text_shadow.dx + dx,
                                      ty + st->text_shadow.dy + dy);
                        pango_cairo_show_layout(cr, layout);
                    }
                }
            }
        }
    }

    /* 2. Text Foreground */
    cairo_set_source_rgba(cr, st->text_color.r / 255.0, st->text_color.g / 255.0,
                          st->text_color.b / 255.0, st->text_color.a / 255.0);
    cairo_move_to(cr, tx, ty);
    pango_cairo_show_layout(cr, layout);
}

/* --- Wayland Layer Shell Handlers --- */

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t width, uint32_t height) {
    (void)width;
    (void)height;
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
    (void)data;
    (void)reg;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* --- Public Rendering API --- */

int wayhud_render_init(wayhud_render_t *r, const wayhud_style_t *style) {
    memset(r, 0, sizeof(*r));
    r->style = *style;

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

    /* 1. Setup Layer Surface */
    r->surface = wl_compositor_create_surface(r->compositor);
    r->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        r->layer_shell, r->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wayhud");

    zwlr_layer_surface_v1_add_listener(r->layer_surface, &layer_surface_listener, r);
    zwlr_layer_surface_v1_set_anchor(r->layer_surface, r->style.anchor);
    zwlr_layer_surface_v1_set_margin(r->layer_surface, r->style.margin_top, r->style.margin_right,
                                     r->style.margin_bottom, r->style.margin_left);
    zwlr_layer_surface_v1_set_exclusive_zone(r->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        r->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    /* 2. 100% Click-through Input Region */
    struct wl_region *empty_region = wl_compositor_create_region(r->compositor);
    wl_surface_set_input_region(r->surface, empty_region);
    wl_region_destroy(empty_region);

    /* 3. Initial Configure Handshake */
    zwlr_layer_surface_v1_set_size(r->layer_surface, 1, 1);
    wl_surface_commit(r->surface);
    while (!r->configured) {
        if (wl_display_dispatch(r->display) < 0) {
            fprintf(stderr, "wayhud: failed to configure layer surface\n");
            return -1;
        }
    }

    /* 4. Pre-create 1x1 Transparent Blank Buffer to keep mapped during hide */
    if (wayhud_buffer_create(r->shm, 1, 1, &r->blank_buffer) == 0) {
        memset(r->blank_buffer.shm_data, 0, r->blank_buffer.shm_size);
        wl_surface_attach(r->surface, r->blank_buffer.wl_buf, 0, 0);
        wl_surface_damage_buffer(r->surface, 0, 0, 1, 1);
        wl_surface_commit(r->surface);
        wl_display_flush(r->display);
    }

    return 0;
}

void wayhud_render_destroy(wayhud_render_t *r) {
    wayhud_buffer_destroy(&r->current_buffer);
    wayhud_buffer_destroy(&r->blank_buffer);

    if (r->layer_surface) zwlr_layer_surface_v1_destroy(r->layer_surface);
    if (r->surface) wl_surface_destroy(r->surface);
    if (r->layer_shell) zwlr_layer_shell_v1_destroy(r->layer_shell);
    if (r->shm) wl_shm_destroy(r->shm);
    if (r->compositor) wl_compositor_destroy(r->compositor);
    if (r->registry) wl_registry_destroy(r->registry);
    if (r->display) wl_display_disconnect(r->display);
}

void wayhud_render_set_style(wayhud_render_t *r, const wayhud_style_t *style) {
    r->style = *style;
    pango_cairo_font_map_set_default(NULL);
    zwlr_layer_surface_v1_set_anchor(r->layer_surface, style->anchor);
    zwlr_layer_surface_v1_set_margin(r->layer_surface, style->margin_top, style->margin_right,
                                     style->margin_bottom, style->margin_left);
    if (r->visible) {
        char text[sizeof(r->current_text)];
        snprintf(text, sizeof(text), "%s", r->current_text);
        int64_t last_keypress_ms = r->last_keypress_ms;
        wayhud_render_show_text(r, text);
        r->last_keypress_ms = last_keypress_ms;
    } else {
        wl_surface_commit(r->surface);
        wl_display_flush(r->display);
    }
}

void wayhud_render_show_text(wayhud_render_t *r, const char *text) {
    if (!r->surface || !text || !text[0]) return;

    snprintf(r->current_text, sizeof(r->current_text), "%s", text);
    r->last_keypress_ms = get_time_ms_now();
    r->visible = true;

    /* Measure text layout */
    cairo_surface_t *measure_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *measure_cr = cairo_create(measure_surf);
    PangoLayout *layout = pango_cairo_create_layout(measure_cr);

    PangoFontDescription *desc = pango_font_description_new();
    pango_font_description_set_family(desc, r->style.font_family);
    pango_font_description_set_size(desc, r->style.font_size * PANGO_SCALE);
    if (r->style.font_weight > 0) {
        pango_font_description_set_weight(desc, (PangoWeight)r->style.font_weight);
    }
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    pango_layout_set_text(layout, r->current_text, -1);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(layout, (PangoAlignment)r->style.text_align);

    if (r->style.max_width > 0) {
        int avail_w = r->style.max_width - r->style.padding_left - r->style.padding_right;
        if (avail_w > 0) pango_layout_set_width(layout, avail_w * PANGO_SCALE);
    }

    int text_w = 0, text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    /* Standard GTK CSS Box Sizing: width, min-width, max-width */
    int width = (r->style.width > 0) ? r->style.width
                                     : (text_w + r->style.padding_left + r->style.padding_right);
    if (r->style.min_width > 0 && width < r->style.min_width) {
        width = r->style.min_width;
    }
    if (r->style.max_width > 0 && width > r->style.max_width) {
        width = r->style.max_width;
    }
    if (width < 1) width = 1;

    int height = text_h + r->style.padding_top + r->style.padding_bottom;
    if (height < 1) height = 1;

    g_object_unref(layout);
    cairo_destroy(measure_cr);
    cairo_surface_destroy(measure_surf);

    /* Allocate or recreate SHM frame buffer */
    wayhud_buffer_destroy(&r->current_buffer);
    if (wayhud_buffer_create(r->shm, width, height, &r->current_buffer) != 0) {
        return;
    }

    /* Draw Content via Cairo */
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        (unsigned char *)r->current_buffer.shm_data, CAIRO_FORMAT_ARGB32, width, height, stride);
    cairo_t *cr = cairo_create(surf);

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_box_decorations(cr, &r->style, width, height);

    PangoLayout *draw_layout = pango_cairo_create_layout(cr);
    PangoFontDescription *draw_desc = pango_font_description_new();
    pango_font_description_set_family(draw_desc, r->style.font_family);
    pango_font_description_set_size(draw_desc, r->style.font_size * PANGO_SCALE);
    if (r->style.font_weight > 0) {
        pango_font_description_set_weight(draw_desc, (PangoWeight)r->style.font_weight);
    }
    pango_layout_set_font_description(draw_layout, draw_desc);
    pango_font_description_free(draw_desc);

    pango_layout_set_text(draw_layout, r->current_text, -1);
    pango_layout_set_wrap(draw_layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_alignment(draw_layout, (PangoAlignment)r->style.text_align);

    int content_w = width - r->style.padding_left - r->style.padding_right;
    if (content_w > 0) {
        pango_layout_set_width(draw_layout, content_w * PANGO_SCALE);
    }

    draw_text_with_shadow(cr, &r->style, draw_layout, r->style.padding_left, r->style.padding_top);

    g_object_unref(draw_layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    /* Present to Wayland Layer Surface */
    zwlr_layer_surface_v1_set_size(r->layer_surface, width, height);
    wl_surface_attach(r->surface, r->current_buffer.wl_buf, 0, 0);
    wl_surface_damage_buffer(r->surface, 0, 0, width, height);
    wl_surface_commit(r->surface);
    wl_display_flush(r->display);
}

void wayhud_render_tick(wayhud_render_t *r) {
    if (!r->visible) return;

    /* If transition-duration is 0, stay permanently resident without hiding */
    if (r->style.transition_duration_ms > 0) {
        int64_t elapsed = get_time_ms_now() - r->last_keypress_ms;
        if (elapsed >= r->style.transition_duration_ms) {
            r->visible = false;
            r->current_text[0] = '\0';
            wayhud_buffer_destroy(&r->current_buffer);

            zwlr_layer_surface_v1_set_size(r->layer_surface, 1, 1);
            if (r->blank_buffer.wl_buf) {
                wl_surface_attach(r->surface, r->blank_buffer.wl_buf, 0, 0);
                wl_surface_damage_buffer(r->surface, 0, 0, 1, 1);
            }
            wl_surface_commit(r->surface);
            wl_display_flush(r->display);
        }
    }
}

int wayhud_render_get_fd(wayhud_render_t *r) {
    return wl_display_get_fd(r->display);
}

void wayhud_render_dispatch(wayhud_render_t *r) {
    if (wl_display_dispatch(r->display) < 0) {
        r->configured = false;
    }
}
