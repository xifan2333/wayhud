#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include "input.h"
#include "render.h"

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static wayhud_color_t parse_hex_color(const char *hex, wayhud_color_t fallback) {
    if (!hex) return fallback;
    if (hex[0] == '#') hex++;
    size_t len = strlen(hex);
    if (len != 6 && len != 8) return fallback;

    unsigned int r, g, b, a = 255;
    if (len == 6) {
        if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3) return fallback;
    } else {
        if (sscanf(hex, "%02x%02x%02x%02x", &r, &g, &b, &a) != 4) return fallback;
    }
    return (wayhud_color_t){ (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
}

static uint32_t parse_anchor(const char *str) {
    if (!str) return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (strcmp(str, "bottom-left") == 0 || strcmp(str, "left-bottom") == 0)
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (strcmp(str, "bottom-right") == 0 || strcmp(str, "right-bottom") == 0)
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    if (strcmp(str, "top-left") == 0 || strcmp(str, "left-top") == 0)
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (strcmp(str, "top-right") == 0 || strcmp(str, "right-top") == 0)
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    if (strcmp(str, "bottom") == 0)
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    if (strcmp(str, "top") == 0)
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    if (strcmp(str, "left") == 0)
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
    if (strcmp(str, "right") == 0)
        return ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    return 0; /* center */
}

static void print_usage(const char *prog) {
    printf("wayhud — universal modern suckless Wayland on-screen HUD\n\n"
           "Usage: %s [options]\n\n"
           "Pipeline Behavior (POSIX isatty):\n"
           "  - When stdin is piped (... | wayhud): automatically displays piped text lines.\n"
           "  - When stdout is piped (wayhud | ...): automatically streams captured keys to stdout.\n"
           "  - When run standalone: captures keyboard hardware and displays on-screen HUD.\n\n"
           "Options:\n"
           "  -a, --anchor <pos>       Anchor position (bottom-left, bottom, top-right, etc.) [default: bottom-left]\n"
           "  -m, --margin <b[,l]>     Margin in pixels: bottom,left or uniform margin [default: 195,4]\n"
           "  -w, --width <px>         Max width in pixels (truncates overflow) [default: 185]\n"
           "  -t, --timeout <ms>       Fade timeout in milliseconds [default: 1200]\n"
           "  -f, --font <font>        Font description [default: JetBrainsMono Nerd Font 16]\n"
           "  -c, --color <hex>        Text color in hex (#ffffff) [default: #ffffff]\n"
           "  -b, --bg <hex>           Background color (#00000000 for pure transparent) [default: #00000000]\n"
           "  -h, --help               Show this help message and exit\n"
           "  -v, --version            Show version information\n",
           prog);
}

typedef struct {
    wayhud_render_t *render;
    bool has_pipe_out;
} dispatch_ctx_t;

static void on_input_key(const char *combo, void *userdata) {
    dispatch_ctx_t *ctx = userdata;
    if (ctx->has_pipe_out) {
        printf("%s\n", combo);
        fflush(stdout);
    }
    wayhud_render_show_text(ctx->render, combo);
}

int main(int argc, char *argv[]) {
    wayhud_config_t cfg = {
        .font = "JetBrainsMono Nerd Font 16",
        .fg_color = { 255, 255, 255, 255 },
        .bg_color = { 0, 0, 0, 0 }, /* 100% pure transparent */
        .special_color = { 122, 162, 247, 255 },
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
        .margin_top = 0,
        .margin_right = 0,
        .margin_bottom = 195,
        .margin_left = 4,
        .max_width = 185,
        .timeout_ms = 1200,
    };

    static struct option long_opts[] = {
        {"anchor", required_argument, NULL, 'a'},
        {"margin", required_argument, NULL, 'm'},
        {"width", required_argument, NULL, 'w'},
        {"timeout", required_argument, NULL, 't'},
        {"font", required_argument, NULL, 'f'},
        {"color", required_argument, NULL, 'c'},
        {"bg", required_argument, NULL, 'b'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "a:m:w:t:f:c:b:hv", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'a':
                cfg.anchor = parse_anchor(optarg);
                break;
            case 'm': {
                int b = 0, l = 0;
                if (sscanf(optarg, "%d,%d", &b, &l) == 2) {
                    cfg.margin_bottom = b;
                    cfg.margin_left = l;
                } else {
                    cfg.margin_bottom = cfg.margin_left = atoi(optarg);
                }
                break;
            }
            case 'w':
                cfg.max_width = atoi(optarg);
                break;
            case 't':
                cfg.timeout_ms = atoi(optarg);
                break;
            case 'f':
                strncpy(cfg.font, optarg, sizeof(cfg.font) - 1);
                break;
            case 'c':
                cfg.fg_color = parse_hex_color(optarg, cfg.fg_color);
                break;
            case 'b':
                cfg.bg_color = parse_hex_color(optarg, cfg.bg_color);
                break;
            case 'v':
                printf("wayhud 0.1.0 (universal suckless Wayland on-screen HUD)\n");
                return 0;
            case 'h':
            default:
                print_usage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }

    /* POSIX isatty automatic pipeline detection */
    bool has_pipe_in = !isatty(STDIN_FILENO);
    bool has_pipe_out = !isatty(STDOUT_FILENO);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    wayhud_render_t render;
    if (wayhud_render_init(&render, &cfg) != 0) {
        return 1;
    }

    dispatch_ctx_t ctx = {
        .render = &render,
        .has_pipe_out = has_pipe_out,
    };

    wayhud_input_t input;
    if (!has_pipe_in) {
        if (wayhud_input_init(&input, on_input_key, &ctx) != 0) {
            fprintf(stderr, "wayhud: cannot access keyboard devices under /dev/input\n"
                            "Hint: Ensure your user is in the 'input' group or run with appropriate permissions.\n");
            wayhud_render_destroy(&render);
            return 1;
        }
    }

    struct pollfd pfds[MAX_DEVICES + 2];
    while (g_running) {
        int pfd_count = 0;

        /* 1. Wayland display fd */
        int wl_fd = wayhud_render_get_fd(&render);
        pfds[pfd_count].fd = wl_fd;
        pfds[pfd_count].events = POLLIN;
        pfd_count++;

        /* 2. Pipe input or /dev/input fds */
        int kbd_start_idx = pfd_count;
        if (has_pipe_in) {
            pfds[pfd_count].fd = STDIN_FILENO;
            pfds[pfd_count].events = POLLIN;
            pfd_count++;
        } else {
            int in_fds[MAX_DEVICES];
            int n = wayhud_input_poll_fds(&input, in_fds, MAX_DEVICES);
            for (int i = 0; i < n; i++) {
                pfds[pfd_count].fd = in_fds[i];
                pfds[pfd_count].events = POLLIN;
                pfd_count++;
            }
        }

        int timeout = 50; /* 50ms tick interval for smooth fade out */
        int ret = poll(pfds, pfd_count, timeout);
        if (ret < 0) {
            if (g_running) continue;
            break;
        }

        /* Check Wayland events */
        if (pfds[0].revents & POLLIN) {
            wayhud_render_dispatch(&render);
        }

        /* Check input events */
        if (has_pipe_in) {
            if (pfds[kbd_start_idx].revents & POLLIN) {
                char line[256];
                if (fgets(line, sizeof(line), stdin)) {
                    line[strcspn(line, "\r\n")] = '\0';
                    if (line[0]) on_input_key(line, &ctx);
                } else {
                    break; /* EOF on stdin */
                }
            }
        } else {
            for (int i = kbd_start_idx; i < pfd_count; i++) {
                if (pfds[i].revents & POLLIN) {
                    wayhud_input_process_fd(&input, pfds[i].fd);
                }
            }
        }

        /* Tick render timer */
        wayhud_render_tick(&render);
    }

    if (!has_pipe_in) {
        wayhud_input_destroy(&input);
    }
    wayhud_render_destroy(&render);

    return 0;
}
