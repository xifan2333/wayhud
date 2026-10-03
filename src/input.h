#ifndef WAYHUD_INPUT_H
#define WAYHUD_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_DEVICES 32
#define MAX_COMBO_LEN 256

typedef void (*wayhud_callback_t)(const char *combo, void *userdata);

typedef struct {
    int fds[MAX_DEVICES];
    char paths[MAX_DEVICES][512];
    char names[MAX_DEVICES][256];
    int count;

    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_map;
    struct xkb_state *xkb_st;

    bool mod_ctrl;
    bool mod_alt;
    bool mod_shift;
    bool mod_super;

    wayhud_callback_t on_key;
    void *userdata;
} wayhud_input_t;

int wayhud_input_open_devices(wayhud_input_t *in);
int wayhud_input_setup_xkb(wayhud_input_t *in, wayhud_callback_t cb, void *userdata);
int wayhud_input_init(wayhud_input_t *in, wayhud_callback_t cb, void *userdata);
void wayhud_input_destroy(wayhud_input_t *in);
int wayhud_input_poll_fds(wayhud_input_t *in, int *out_fds, int max_fds);
void wayhud_input_process_fd(wayhud_input_t *in, int fd);

#endif /* WAYHUD_INPUT_H */
