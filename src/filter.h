#ifndef WAYHUD_FILTER_H
#define WAYHUD_FILTER_H

#include <stddef.h>
#include <stdint.h>

/*
 * Key multiplier / collapse filter (inline uniq -c state machine).
 * Collapses consecutive identical events within a time window into "Token × N".
 */
typedef struct {
    char last_text[128];
    int count;
    int64_t last_time_ms;
} wayhud_multiplier_t;

void wayhud_multiplier_init(wayhud_multiplier_t *m);
void wayhud_multiplier_reset(wayhud_multiplier_t *m);

/*
 * Feeds a raw text token through the multiplier filter.
 * Writes the transformed presentation text to out_buf and returns out_buf.
 */
const char *wayhud_multiplier_feed(wayhud_multiplier_t *m, const char *raw_text, int timeout_ms,
                                   char *out_buf, size_t out_sz);

#endif /* WAYHUD_FILTER_H */
