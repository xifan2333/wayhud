#include "input.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x) - 1) / BITS_PER_LONG) + 1)
#define OFF(x) ((x) % BITS_PER_LONG)
#define BIT(x) (1UL << OFF(x))
#define LONG(x) ((x) / BITS_PER_LONG)
#define test_bit(bit, array) ((((array)[LONG(bit)] >> OFF(bit)) & 1))

static const char *clean_key_name(xkb_keysym_t sym, char *buf, size_t buf_size) {
    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        return "Enter";
    case XKB_KEY_Escape:
        return "Esc";
    case XKB_KEY_BackSpace:
        return "Backspace";
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab:
        return "Tab";
    case XKB_KEY_space:
        return "Space";
    case XKB_KEY_Up:
        return "↑";
    case XKB_KEY_Down:
        return "↓";
    case XKB_KEY_Left:
        return "←";
    case XKB_KEY_Right:
        return "→";
    default:
        break;
    }

    int len = xkb_keysym_to_utf8(sym, buf, buf_size);
    if (len > 0 && (unsigned char)buf[0] > ' ') {
        buf[len] = '\0';
        return buf;
    }

    xkb_keysym_get_name(sym, buf, buf_size);
    return buf;
}

int wayhud_input_open_devices(wayhud_input_t *in) {
    for (int i = 0; i < MAX_DEVICES; i++) {
        in->fds[i] = -1;
    }
    in->count = 0;

    DIR *dir = opendir("/dev/input");
    if (!dir) {
        fprintf(stderr, "wayhud: cannot open /dev/input: %s\n", strerror(errno));
        return -1;
    }

    struct dirent *de;
    while ((de = readdir(dir)) && in->count < MAX_DEVICES) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;

        char path[512];
        int plen = snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        if (plen <= 0 || (size_t)plen >= sizeof(path)) continue;

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        unsigned long ev_bits[NBITS(EV_MAX)] = {0};
        unsigned long key_bits[NBITS(KEY_MAX)] = {0};

        if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) >= 0 &&
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) {
            if (test_bit(EV_KEY, ev_bits) &&
                (test_bit(KEY_A, key_bits) || test_bit(KEY_SPACE, key_bits))) {
                char dev_name[128] = "Keyboard";
                ioctl(fd, EVIOCGNAME(sizeof(dev_name)), dev_name);

                in->fds[in->count] = fd;
                memcpy(in->paths[in->count], path, sizeof(path));
                strncpy(in->names[in->count], dev_name, sizeof(in->names[0]) - 1);
                in->count++;
                continue;
            }
        }
        close(fd);
    }
    closedir(dir);

    return in->count > 0 ? 0 : -1;
}

int wayhud_input_setup_xkb(wayhud_input_t *in, wayhud_callback_t cb, void *userdata) {
    in->on_key = cb;
    in->userdata = userdata;

    in->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!in->xkb_ctx) {
        fprintf(stderr, "wayhud: failed to create xkb context\n");
        return -1;
    }

    in->xkb_map = xkb_keymap_new_from_names(in->xkb_ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!in->xkb_map) {
        fprintf(stderr, "wayhud: failed to compile system keymap\n");
        xkb_context_unref(in->xkb_ctx);
        in->xkb_ctx = NULL;
        return -1;
    }

    in->xkb_st = xkb_state_new(in->xkb_map);
    if (!in->xkb_st) {
        fprintf(stderr, "wayhud: failed to create xkb state\n");
        xkb_keymap_unref(in->xkb_map);
        in->xkb_map = NULL;
        xkb_context_unref(in->xkb_ctx);
        in->xkb_ctx = NULL;
        return -1;
    }

    return 0;
}

int wayhud_input_init(wayhud_input_t *in, wayhud_callback_t cb, void *userdata) {
    memset(in, 0, sizeof(*in));
    if (wayhud_input_open_devices(in) != 0) {
        return -1;
    }
    return wayhud_input_setup_xkb(in, cb, userdata);
}

void wayhud_input_destroy(wayhud_input_t *in) {
    for (int i = 0; i < in->count; i++) {
        if (in->fds[i] >= 0) {
            close(in->fds[i]);
            in->fds[i] = -1;
        }
    }
    in->count = 0;

    if (in->xkb_st) {
        xkb_state_unref(in->xkb_st);
        in->xkb_st = NULL;
    }
    if (in->xkb_map) {
        xkb_keymap_unref(in->xkb_map);
        in->xkb_map = NULL;
    }
    if (in->xkb_ctx) {
        xkb_context_unref(in->xkb_ctx);
        in->xkb_ctx = NULL;
    }
}

int wayhud_input_poll_fds(wayhud_input_t *in, int *out_fds, int max_fds) {
    int n = in->count < max_fds ? in->count : max_fds;
    for (int i = 0; i < n; i++) {
        out_fds[i] = in->fds[i];
    }
    return n;
}

void wayhud_input_process_fd(wayhud_input_t *in, int fd) {
    struct input_event evs[64];
    ssize_t bytes = read(fd, evs, sizeof(evs));
    if (bytes <= 0) return;

    size_t count = (size_t)bytes / sizeof(struct input_event);
    for (size_t i = 0; i < count; i++) {
        if (evs[i].type != EV_KEY) continue;

        uint32_t code = evs[i].code;
        int value = evs[i].value;

        if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
            in->mod_ctrl = (value != 0);
            continue;
        }
        if (code == KEY_LEFTALT || code == KEY_RIGHTALT) {
            in->mod_alt = (value != 0);
            continue;
        }
        if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
            in->mod_shift = (value != 0);
            continue;
        }
        if (code == KEY_LEFTMETA || code == KEY_RIGHTMETA) {
            in->mod_super = (value != 0);
            continue;
        }

        if (value == 0) continue;

        uint32_t xkb_keycode = code + 8;
        xkb_state_update_key(in->xkb_st, xkb_keycode, XKB_KEY_DOWN);
        xkb_keysym_t sym = xkb_state_key_get_one_sym(in->xkb_st, xkb_keycode);

        char name_buf[64] = {0};
        const char *key_str = clean_key_name(sym, name_buf, sizeof(name_buf));

        char combo[MAX_COMBO_LEN] = {0};
        if (in->mod_super) strcat(combo, "Super + ");
        if (in->mod_ctrl) strcat(combo, "Ctrl + ");
        if (in->mod_alt) strcat(combo, "Alt + ");
        if (in->mod_shift) strcat(combo, "Shift + ");
        strncat(combo, key_str, sizeof(combo) - strlen(combo) - 1);

        if (in->on_key) {
            in->on_key(combo, in->userdata);
        }
    }
}
