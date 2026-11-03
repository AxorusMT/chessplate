#include "metrics.h"

#include <math.h>
#include <stdio.h>

float best_move_stability(const SearchSnapshot *history, int count) {
    if (count <= 1) return 1.0f;
    int take = count < 12 ? count : 12;
    const SearchSnapshot *tail = history + (count - take);
    Move reference = tail[take - 1].best_move;
    int matches = 0;
    for (int i = 0; i < take; i++) {
        if (move_equal(tail[i].best_move, reference)) matches++;
    }
    return (float)matches / (float)take;
}

float eval_volatility_cp(const SearchSnapshot *history, int count) {
    int take = count < 14 ? count : 14;
    if (take < 2) return 0.0f;
    const SearchSnapshot *tail = history + (count - take);
    float total = 0.0f;
    for (int i = 0; i + 1 < take; i++) {
        total += fabsf((float)tail[i].best_score_cp - (float)tail[i + 1].best_score_cp);
    }
    return total / (float)(take - 1);
}

void format_big_number(uint64_t value, char *buf, size_t size) {
    if (value >= 1000000000ULL) snprintf(buf, size, "%.2fB", (double)value / 1e9);
    else if (value >= 1000000ULL) snprintf(buf, size, "%.2fM", (double)value / 1e6);
    else if (value >= 1000ULL) snprintf(buf, size, "%.1fK", (double)value / 1e3);
    else snprintf(buf, size, "%llu", (unsigned long long)value);
}
