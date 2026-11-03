#ifndef CHESSPLATE_METRICS_H
#define CHESSPLATE_METRICS_H

#include <stddef.h>
#include <stdint.h>

#include "engine.h"

/* Fraction of the last 12 snapshots whose best move matches the newest one. */
float best_move_stability(const SearchSnapshot *history, int count);
/* Mean absolute centipawn swing across the last 14 snapshots. */
float eval_volatility_cp(const SearchSnapshot *history, int count);
/* 1234 -> "1.2K", 1234567 -> "1.23M", ... */
void format_big_number(uint64_t value, char *buf, size_t size);

#endif
