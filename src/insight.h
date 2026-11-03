#ifndef CHESSPLATE_INSIGHT_H
#define CHESSPLATE_INSIGHT_H

#include "chess.h"

typedef struct {
    const char *current_phase;
    const char *opening_name;
    const char *middlegame_name;
    const char *endgame_name;
} PhaseNames;

/* move_log holds coordinate notation strings ("e2e4") for every move played. */
PhaseNames phase_names(const Position *position, const char (*move_log)[6], int move_count);

#endif
