#define _GNU_SOURCE
#include "filter.h"
#include "input.h"
#include "render.h"
#include "style.h"
#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef VERSION
#define VERSION "0.1.1"
#endif

#define MAX_EVENT_SOURCES (MAX_DEVICES + 4)

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

/* --- Application Architecture --- */

typedef struct wayhud_app wayhud_app_t;

typedef void (*event_handler_t)(wayhud_app_t *app, int fd, uint32_t revents);

typedef struct {
    int fd;
    short events;
    event_handler_t handler;
} event_source_t;

struct wayhud_app {
    wayhud_style_t style;
    wayhud_render_t render;
    wayhud_input_t input;
    wayhud_multiplier_t multiplier; /* Inline collapse filter for hardware events */

    bool has_pipe_in;
    bool has_pipe_out;
    bool stdin_closed;
    bool waiting_for_fadeout;
};

static void print_usage(const char *prog) {
    printf(
        "wayhud — universal modern suckless Wayland on-screen HUD (GTK CSS styled)\n\n"
        "Usage: %s [options]\n\n"
        "Pipeline Behavior (POSIX isatty):\n"
        "  - When stdin is piped (... | wayhud): automatically displays piped text lines.\n"
        "  - When stdout is piped (wayhud | ...): automatically streams captured keys to stdout.\n"
        "  - When run standalone: captures keyboard hardware and displays on-screen HUD.\n\n"
        "Options:\n"
        "  -s, --style <file|css>   GTK CSS stylesheet file path or inline CSS string\n"
        "                           [default: $XDG_CONFIG_HOME/wayhud/style.css]\n"
        "  -n, --name <name>        Instance name matching window#<name> and label#<name>\n"
        "                           [default: keys]\n"
        "  -h, --help               Show this help message and exit\n"
        "  -v, --version            Show version information\n\n"
        "GTK CSS Example ($XDG_CONFIG_HOME/wayhud/style.css):\n"
        "  window {\n"
        "      margin-bottom: 195px;\n"
        "      margin-left: 4px;\n"
        "      padding: 6px 12px;\n"
        "      background-color: rgba(24, 24, 37, 0.85);\n"
        "      border-radius: 8px;\n"
        "      border: 1px solid rgba(255, 255, 255, 0.1);\n"
        "      transition-duration: 1.2s;\n"
        "  }\n"
        "  label {\n"
        "      color: #cdd6f4;\n"
        "      font-family: \"JetBrainsMono Nerd Font\";\n"
        "      font-size: 16px;\n"
        "      text-shadow: 0 0 2px rgba(0, 0, 0, 0.85);\n"
        "  }\n"
        "  /* Multi-scene instance overrides */\n"
        "  window#title { margin-top: 36px; min-width: 600px; transition-duration: 0; }\n"
        "  window#captions { margin-bottom: 80px; min-width: 500px; padding: 8px 16px; }\n"
        "  label#captions { font-size: 20px; text-align: center; }\n",
        prog);
}

/* --- Presentation Sink (Pure Output) --- */

static void app_present(wayhud_app_t *app, const char *text) {
    if (!text || !text[0]) return;

    if (app->has_pipe_out) {
        printf("%s\n", text);
        fflush(stdout);
    }
    wayhud_render_show_text(&app->render, text);
}

/* --- Pipeline Source Handlers --- */

/* Hardware pipeline: Raw Key -> Multiplier Filter -> Presentation Sink */
static void on_hardware_key_event(const char *raw_combo, void *userdata) {
    wayhud_app_t *app = userdata;
    char filtered_buf[256];
    const char *text = wayhud_multiplier_feed(&app->multiplier, raw_combo,
                                              app->render.style.transition_duration_ms,
                                              filtered_buf, sizeof(filtered_buf));
    app_present(app, text);
}

/* Stdin pipeline: Raw Text Stream -> Presentation Sink (Direct Passthrough) */
static void on_pipe_input_event(wayhud_app_t *app, int fd, uint32_t revents) {
    (void)fd;
    (void)revents;
    char line[2048];
    if (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) {
            app_present(app, line);
        }
    } else {
        app->stdin_closed = true;
        if (app->render.style.transition_duration_ms > 0) {
            app->waiting_for_fadeout = true;
        }
    }
}

static void on_device_fd_event(wayhud_app_t *app, int fd, uint32_t revents) {
    (void)revents;
    wayhud_input_process_fd(&app->input, fd);
}

static void on_wayland_socket_event(wayhud_app_t *app, int fd, uint32_t revents) {
    (void)fd;
    (void)revents;
    wayhud_render_dispatch(&app->render);
}

static int drop_privileges(void) {
    uid_t real_uid = getuid();
    gid_t real_gid = getgid();

    /* If running unprivileged (euid == uid), nothing to drop */
    if (geteuid() == real_uid && getegid() == real_gid) {
        return 0;
    }

    /* Drop supplementary groups */
    if (setgroups(1, &real_gid) != 0) {
        /* Non-fatal if we lack CAP_SETGID */
    }

    /* Permanently drop GID */
    if (setresgid(real_gid, real_gid, real_gid) != 0) {
        fprintf(stderr, "wayhud: failed to drop GID privileges: %s\n", strerror(errno));
        return -1;
    }

    /* Permanently drop UID */
    if (setresuid(real_uid, real_uid, real_uid) != 0) {
        fprintf(stderr, "wayhud: failed to drop UID privileges: %s\n", strerror(errno));
        return -1;
    }

    /* Verify privileges have been permanently dropped */
    if (geteuid() != real_uid || getegid() != real_gid) {
        fprintf(stderr, "wayhud: security check failed: unable to permanently drop privileges\n");
        return -1;
    }

    return 0;
}

/* --- Application Lifecycle --- */

static int app_init(wayhud_app_t *app, int argc, char *argv[]) {
    memset(app, 0, sizeof(*app));
    wayhud_style_init_default(&app->style);
    wayhud_multiplier_init(&app->multiplier);

    const char *style_arg = NULL;
    const char *instance_name = "keys";

    static struct option long_opts[] = {{"style", required_argument, NULL, 's'},
                                        {"name", required_argument, NULL, 'n'},
                                        {"help", no_argument, NULL, 'h'},
                                        {"version", no_argument, NULL, 'v'},
                                        {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "s:n:hv", long_opts, NULL)) != -1) {
        switch (opt) {
        case 's':
            style_arg = optarg;
            break;
        case 'n':
            instance_name = optarg;
            break;
        case 'v':
            printf("wayhud %s (universal suckless Wayland on-screen HUD, GTK CSS styled)\n",
                   VERSION);
            exit(0);
        case 'h':
        default:
            print_usage(argv[0]);
            exit(opt == 'h' ? 0 : 1);
        }
    }

    struct stat in_stat;
    app->has_pipe_in = (fstat(STDIN_FILENO, &in_stat) == 0 &&
                        (S_ISFIFO(in_stat.st_mode) || S_ISREG(in_stat.st_mode)));
    app->has_pipe_out = !isatty(STDOUT_FILENO);

    /* 1. Open hardware input devices early while elevated privileges (if any) are active */
    if (!app->has_pipe_in) {
        if (wayhud_input_open_devices(&app->input) != 0) {
            /* Open may fail if unprivileged; drop_privileges is called regardless */
        }
    }

    /* 2. Permanently drop all elevated privileges immediately after opening devices */
    if (drop_privileges() != 0) {
        if (!app->has_pipe_in) {
            wayhud_input_destroy(&app->input);
        }
        return -1;
    }

    /* 3. Validate input devices if in standalone keycaster mode */
    if (!app->has_pipe_in) {
        if (app->input.count == 0) {
            fprintf(stderr, "wayhud: cannot access keyboard devices under /dev/input\n"
                            "Hint: Install with SUID (sudo make install-suid), file capabilities,\n"
                            "or add user to the 'input' group.\n");
            return -1;
        }
        if (wayhud_input_setup_xkb(&app->input, on_hardware_key_event, app) != 0) {
            wayhud_input_destroy(&app->input);
            return -1;
        }
    }

    /* 4. Load stylesheet as unprivileged user */
    if (style_arg) {
        if (strchr(style_arg, '{')) {
            wayhud_style_parse(&app->style, style_arg, instance_name);
        } else {
            wayhud_style_load_file(&app->style, style_arg, instance_name);
        }
    } else {
        wayhud_style_load_file(&app->style, NULL, instance_name);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 5. Connect to Wayland and initialize renderer */
    if (wayhud_render_init(&app->render, &app->style) != 0) {
        if (!app->has_pipe_in) {
            wayhud_input_destroy(&app->input);
        }
        return -1;
    }

    return 0;
}

static void app_destroy(wayhud_app_t *app) {
    if (!app->has_pipe_in) {
        wayhud_input_destroy(&app->input);
    }
    wayhud_render_destroy(&app->render);
}

static int app_collect_sources(wayhud_app_t *app, event_source_t *sources) {
    int count = 0;

    /* 1. Wayland display socket */
    sources[count++] = (event_source_t){
        .fd = wayhud_render_get_fd(&app->render),
        .events = POLLIN,
        .handler = on_wayland_socket_event,
    };

    /* 2. Stdin pipe input stream */
    if (app->has_pipe_in && !app->stdin_closed) {
        sources[count++] = (event_source_t){
            .fd = STDIN_FILENO,
            .events = POLLIN | POLLHUP,
            .handler = on_pipe_input_event,
        };
    }

    /* 3. Evdev hardware devices */
    if (!app->has_pipe_in) {
        int in_fds[MAX_DEVICES];
        int n = wayhud_input_poll_fds(&app->input, in_fds, MAX_DEVICES);
        for (int i = 0; i < n && count < MAX_EVENT_SOURCES; i++) {
            sources[count++] = (event_source_t){
                .fd = in_fds[i],
                .events = POLLIN,
                .handler = on_device_fd_event,
            };
        }
    }

    return count;
}

static int app_run(wayhud_app_t *app) {
    while (g_running) {
        event_source_t sources[MAX_EVENT_SOURCES];
        int count = app_collect_sources(app, sources);

        struct pollfd pfds[MAX_EVENT_SOURCES];
        for (int i = 0; i < count; i++) {
            pfds[i].fd = sources[i].fd;
            pfds[i].events = sources[i].events;
            pfds[i].revents = 0;
        }

        int ret = poll(pfds, count, 50);
        if (ret < 0) {
            if (g_running) continue;
            break;
        }

        /* Polymorphic event dispatch */
        for (int i = 0; i < count; i++) {
            if (pfds[i].revents) {
                sources[i].handler(app, sources[i].fd, pfds[i].revents);
            }
        }

        wayhud_render_tick(&app->render);

        if (app->waiting_for_fadeout && !app->render.visible) {
            break;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    wayhud_app_t app;
    if (app_init(&app, argc, argv) != 0) {
        return 1;
    }
    int ret = app_run(&app);
    app_destroy(&app);
    return ret;
}
