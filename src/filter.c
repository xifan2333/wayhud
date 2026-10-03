#define _GNU_SOURCE
#include "filter.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static int64_t get_time_ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void wayhud_multiplier_init(wayhud_multiplier_t *m) {
    memset(m, 0, sizeof(*m));
}

void wayhud_multiplier_reset(wayhud_multiplier_t *m) {
    memset(m, 0, sizeof(*m));
}

const char *wayhud_multiplier_feed(wayhud_multiplier_t *m, const char *raw_text, int timeout_ms,
                                   char *out_buf, size_t out_sz) {
    if (!raw_text || !raw_text[0] || !out_buf || out_sz == 0) {
        if (out_buf && out_sz > 0) out_buf[0] = '\0';
        return out_buf;
    }

    int64_t now = get_time_ms_now();
    int64_t elapsed = now - m->last_time_ms;

    int window = (timeout_ms > 0) ? timeout_ms : 1500;

    if (m->last_text[0] && strcmp(m->last_text, raw_text) == 0 && elapsed < window) {
        m->count++;
    } else {
        snprintf(m->last_text, sizeof(m->last_text), "%s", raw_text);
        m->count = 1;
    }
    m->last_time_ms = now;

    if (m->count > 1) {
        snprintf(out_buf, out_sz, "%s × %d", m->last_text, m->count);
    } else {
        snprintf(out_buf, out_sz, "%s", m->last_text);
    }

    return out_buf;
}
