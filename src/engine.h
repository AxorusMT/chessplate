#ifndef CHESSPLATE_ENGINE_H
#define CHESSPLATE_ENGINE_H

#include "chess.h"

#define ENGINE_MIN_ELO 600
#define ENGINE_MAX_ELO 2800
#define SNAPSHOT_TOP_MOVES 4

typedef struct {
    int depth;
    uint64_t time_ms;
    uint64_t node_limit;
    int randomness_cp;
} EloProfile;

typedef struct {
    Move mv;
    int score_cp;
} ScoredMove;

typedef struct {
    int depth;
    uint64_t elapsed_ms;
    uint64_t nodes;
    Move best_move;
    int best_score_cp;
    ScoredMove top_moves[SNAPSHOT_TOP_MOVES];
    int top_count;
} SearchSnapshot;

typedef void (*SnapshotCallback)(const SearchSnapshot *snapshot, void *user);

int engine_clamp_elo(int elo);
EloProfile profile_for_elo(int elo);

/* Runs an iterative-deepening search. Calls on_snapshot after every completed
 * depth. `cancel` may be NULL; when it points to a non-zero value the search
 * stops as soon as possible. Returns false when there is no legal move. */
bool engine_best_move(const Position *position, int elo, SnapshotCallback on_snapshot, void *user,
                      const volatile int *cancel, Move *out);

/* Static evaluation from the side-to-move's perspective (centipawns). */
int engine_evaluate(const Position *position);

#endif
