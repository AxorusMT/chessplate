#include "engine.h"

#include <limits.h>

#include "sys.h"

#define INF 1000000
#define MATE_SCORE 100000

typedef struct {
    int max_depth;
    uint64_t deadline_ms;
    uint64_t node_limit;
    int randomness_cp;
} SearchLimits;

typedef struct {
    SearchLimits limits;
    uint64_t nodes;
    bool stopped;
    uint64_t started_at;
    const volatile int *cancel;
    uint64_t rng;
} Searcher;

int engine_clamp_elo(int elo) {
    if (elo < ENGINE_MIN_ELO) return ENGINE_MIN_ELO;
    if (elo > ENGINE_MAX_ELO) return ENGINE_MAX_ELO;
    return elo;
}

EloProfile profile_for_elo(int elo) {
    int c = engine_clamp_elo(elo);
    EloProfile p;
    if (c < 900) {
        p.depth = 2, p.time_ms = 150, p.node_limit = 35000;
    } else if (c < 1200) {
        p.depth = 3, p.time_ms = 250, p.node_limit = 70000;
    } else if (c < 1500) {
        p.depth = 4, p.time_ms = 400, p.node_limit = 150000;
    } else if (c < 1800) {
        p.depth = 5, p.time_ms = 600, p.node_limit = 300000;
    } else if (c < 2100) {
        p.depth = 6, p.time_ms = 900, p.node_limit = 700000;
    } else if (c < 2400) {
        p.depth = 7, p.time_ms = 1300, p.node_limit = 1300000;
    } else {
        p.depth = 8, p.time_ms = 1800, p.node_limit = 2200000;
    }
    p.randomness_cp = (2800 - c) / 8;
    if (p.randomness_cp < 0) p.randomness_cp = 0;
    return p;
}

/* ------------------------------------------------------------------------ */
/* Evaluation                                                               */
/* ------------------------------------------------------------------------ */

static const int PAWN_PSQ[64] = {
    0,  0,  0,  0,   0,   0,  0,  0,  50, 50, 50,  50, 50, 50,  50, 50, 10, 10, 20, 30, 30, 20,
    10, 10, 5,  5,   10,  27, 27, 10, 5,  5,  0,   0,  0,  25,  25, 0,  0,  0,  5,  -5, -10, 0,
    0,  -10, -5, 5,  5,   10, 10, -25, -25, 10, 10, 5,  0,   0,  0,  0,  0,  0,  0,  0,
};

static const int KNIGHT_PSQ[64] = {
    -50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   0,   0,   0,   -20, -40,
    -30, 0,   10,  15,  15,  10,  0,   -30, -30, 5,   15,  20,  20,  15,  5,   -30,
    -30, 0,   15,  20,  20,  15,  0,   -30, -30, 5,   10,  15,  15,  10,  5,   -30,
    -40, -20, 0,   5,   5,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50,
};

static const int BISHOP_PSQ[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20, -10, 0,   0,   0,   0,   0,   0,   -10,
    -10, 0,   5,   10,  10,  5,   0,   -10, -10, 5,   5,   10,  10,  5,   5,   -10,
    -10, 0,   10,  10,  10,  10,  0,   -10, -10, 10,  10,  10,  10,  10,  10,  -10,
    -10, 5,   0,   0,   0,   0,   5,   -10, -20, -10, -10, -10, -10, -10, -10, -20,
};

static const int ROOK_PSQ[64] = {
    0,  0, 0, 0, 0, 0, 0, 0,  5,  10, 10, 10, 10, 10, 10, 5,  -5, 0, 0, 0, 0, 0,
    0,  -5, -5, 0, 0, 0, 0, 0, 0,  -5, -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0, 0, 0,
    0,  0, 0, -5, -5, 0, 0, 0, 0,  0,  0,  -5, 0,  0,  0,  5,  5,  0,  0,  0,
};

static const int QUEEN_PSQ[64] = {
    -20, -10, -10, -5,  -5,  -10, -10, -20, -10, 0,   0,   0,  0,  0,   0,   -10,
    -10, 0,   5,   5,   5,   5,   0,   -10, -5,  0,   5,   5,  5,  5,   0,   -5,
    0,   0,   5,   5,   5,   5,   0,   -5,  -10, 5,   5,   5,  5,  5,   0,   -10,
    -10, 0,   5,   0,   0,   0,   0,   -10, -20, -10, -10, -5, -5, -10, -10, -20,
};

static const int KING_PSQ[64] = {
    -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10,
    20,  20,  0,   0,   0,   0,   20,  20,  20,  30,  10,  0,   0,   10,  30,  20,
};

static int piece_value(int kind) {
    switch (kind) {
    case KIND_PAWN: return 100;
    case KIND_KNIGHT: return 320;
    case KIND_BISHOP: return 330;
    case KIND_ROOK: return 500;
    case KIND_QUEEN: return 900;
    case KIND_KING: return 20000;
    default: return 0;
    }
}

static int mirror_square(int square) { return (7 - square / 8) * 8 + square % 8; }

static int piece_square_value(int kind, Color color, int square) {
    int index = color == COLOR_WHITE ? square : mirror_square(square);
    switch (kind) {
    case KIND_PAWN: return PAWN_PSQ[index];
    case KIND_KNIGHT: return KNIGHT_PSQ[index];
    case KIND_BISHOP: return BISHOP_PSQ[index];
    case KIND_ROOK: return ROOK_PSQ[index];
    case KIND_QUEEN: return QUEEN_PSQ[index];
    case KIND_KING: return KING_PSQ[index];
    default: return 0;
    }
}

int engine_evaluate(const Position *position) {
    int white_score = 0, black_score = 0;
    int white_bishops = 0, black_bishops = 0;

    for (int sq = 0; sq < 64; sq++) {
        Piece p = position->board[sq];
        if (p == PIECE_NONE) continue;
        int kind = PIECE_KIND(p);
        Color color = PIECE_COLOR(p);
        int score = piece_value(kind) + piece_square_value(kind, color, sq);
        if (color == COLOR_WHITE) {
            white_score += score;
            if (kind == KIND_BISHOP) white_bishops++;
        } else {
            black_score += score;
            if (kind == KIND_BISHOP) black_bishops++;
        }
    }

    if (white_bishops >= 2) white_score += 30;
    if (black_bishops >= 2) black_score += 30;

    int score = white_score - black_score;
    if (position->side_to_move == COLOR_WHITE) {
        return score + 10;
    }
    return -(score - 10);
}

/* ------------------------------------------------------------------------ */
/* Search                                                                   */
/* ------------------------------------------------------------------------ */

static int move_score(const Position *position, Move mv) {
    int score = 0;
    if (mv.promotion != KIND_NONE) score += 20000 + piece_value(mv.promotion);
    if (move_is_castling(mv)) score += 500;
    if (position_is_capture(position, mv)) {
        Piece attacker_piece = position->board[mv.from];
        int attacker = attacker_piece != PIECE_NONE ? PIECE_KIND(attacker_piece) : KIND_PAWN;
        int captured = KIND_PAWN;
        if (!move_is_en_passant(mv)) {
            Piece cp = position->board[mv.to];
            captured = cp != PIECE_NONE ? PIECE_KIND(cp) : KIND_PAWN;
        }
        score += 10000 + piece_value(captured) - piece_value(attacker) / 10;
    }
    return score;
}

/* Stable descending sort by move_score (insertion sort keeps generation order
 * for ties, matching a stable sort). */
static void order_moves(const Position *position, MoveList *list) {
    int scores[MAX_MOVES];
    for (int i = 0; i < list->count; i++) scores[i] = move_score(position, list->moves[i]);
    for (int i = 1; i < list->count; i++) {
        Move m = list->moves[i];
        int s = scores[i];
        int j = i - 1;
        while (j >= 0 && scores[j] < s) {
            list->moves[j + 1] = list->moves[j];
            scores[j + 1] = scores[j];
            j--;
        }
        list->moves[j + 1] = m;
        scores[j + 1] = s;
    }
}

static void touch_node(Searcher *s) {
    if (s->nodes < UINT64_MAX) s->nodes++;
    if (s->nodes >= s->limits.node_limit) {
        s->stopped = true;
        return;
    }
    if ((s->nodes & 0x3ff) == 0) {
        s->stopped = sys_time_ms() >= s->limits.deadline_ms || (s->cancel && *s->cancel);
    }
}

static bool should_stop(Searcher *s) {
    if (s->stopped) return true;
    if (s->nodes >= s->limits.node_limit || sys_time_ms() >= s->limits.deadline_ms || (s->cancel && *s->cancel)) {
        s->stopped = true;
        return true;
    }
    return false;
}

static int quiescence(Searcher *s, const Position *position, int alpha, int beta, int ply) {
    touch_node(s);
    if (s->stopped) return engine_evaluate(position);

    bool in_check = position_is_in_check(position, position->side_to_move);
    if (!in_check) {
        int stand_pat = engine_evaluate(position);
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;
    }

    MoveList moves;
    if (in_check) position_legal_moves(position, &moves);
    else position_tactical_moves(position, &moves);

    if (moves.count == 0) {
        if (in_check) return -MATE_SCORE + ply;
        return alpha;
    }

    order_moves(position, &moves);

    for (int i = 0; i < moves.count; i++) {
        if (should_stop(s)) break;
        Position next = *position;
        position_make_move(&next, moves.moves[i]);
        int score = -quiescence(s, &next, -beta, -alpha, ply < 255 ? ply + 1 : 255);
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

static int negamax(Searcher *s, const Position *position, int depth, int alpha, int beta, int ply) {
    touch_node(s);
    if (s->stopped) return engine_evaluate(position);

    if (depth == 0) return quiescence(s, position, alpha, beta, ply);

    MoveList moves;
    position_legal_moves(position, &moves);
    if (moves.count == 0) {
        if (position_is_in_check(position, position->side_to_move)) return -MATE_SCORE + ply;
        return 0;
    }

    order_moves(position, &moves);

    int best_score = -INF;
    for (int i = 0; i < moves.count; i++) {
        if (should_stop(s)) break;
        Position next = *position;
        position_make_move(&next, moves.moves[i]);
        int score = -negamax(s, &next, depth - 1, -beta, -alpha, ply < 255 ? ply + 1 : 255);
        if (score > best_score) best_score = score;
        if (score > alpha) alpha = score;
        if (alpha >= beta) break;
    }

    return best_score == -INF ? engine_evaluate(position) : best_score;
}

static uint64_t rng_next(uint64_t *state) {
    /* xorshift64* */
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

/* Uniform integer in [-r, r]. */
static int rng_symmetric(uint64_t *state, int r) {
    uint64_t span = (uint64_t)(2 * r + 1);
    return (int)(rng_next(state) % span) - r;
}

static Move pick_with_noise(Searcher *s, const ScoredMove *scored, int count) {
    int randomness = s->limits.randomness_cp;
    if (randomness <= 0) return scored[0].mv;

    int candidates = count < 5 ? count : 5;
    Move best_move = scored[0].mv;
    int best_adjusted = INT_MIN;
    for (int i = 0; i < candidates; i++) {
        int adjusted = scored[i].score_cp + rng_symmetric(&s->rng, randomness);
        if (adjusted > best_adjusted) {
            best_adjusted = adjusted;
            best_move = scored[i].mv;
        }
    }
    return best_move;
}

static void sort_scored_desc(ScoredMove *list, int count) {
    for (int i = 1; i < count; i++) {
        ScoredMove m = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].score_cp < m.score_cp) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = m;
    }
}

bool engine_best_move(const Position *position, int elo, SnapshotCallback on_snapshot, void *user,
                      const volatile int *cancel, Move *out) {
    EloProfile profile = profile_for_elo(elo);
    Searcher s;
    s.started_at = sys_time_ms();
    s.limits.max_depth = profile.depth;
    s.limits.deadline_ms = s.started_at + profile.time_ms;
    s.limits.node_limit = profile.node_limit;
    s.limits.randomness_cp = profile.randomness_cp;
    s.nodes = 0;
    s.stopped = false;
    s.cancel = cancel;
    s.rng = sys_entropy() | 1u;

    MoveList root;
    position_legal_moves(position, &root);
    if (root.count == 0) return false;
    order_moves(position, &root);

    ScoredMove completed[MAX_MOVES];
    int completed_count = 0;

    for (int depth = 1; depth <= s.limits.max_depth; depth++) {
        if (should_stop(&s)) break;

        ScoredMove depth_scores[MAX_MOVES];
        int depth_count = 0;
        bool aborted = false;
        int alpha = -INF;
        const int beta = INF;

        for (int i = 0; i < root.count; i++) {
            if (should_stop(&s)) {
                aborted = true;
                break;
            }
            Position next = *position;
            position_make_move(&next, root.moves[i]);
            int score = -negamax(&s, &next, depth - 1, -beta, -alpha, 1);
            depth_scores[depth_count].mv = root.moves[i];
            depth_scores[depth_count].score_cp = score;
            depth_count++;
            if (score > alpha) alpha = score;
        }

        if (aborted) break;

        sort_scored_desc(depth_scores, depth_count);
        if (depth_count > 0 && on_snapshot) {
            SearchSnapshot snap;
            snap.depth = depth;
            snap.elapsed_ms = sys_time_ms() - s.started_at;
            snap.nodes = s.nodes;
            snap.best_move = depth_scores[0].mv;
            snap.best_score_cp = depth_scores[0].score_cp;
            snap.top_count = depth_count < SNAPSHOT_TOP_MOVES ? depth_count : SNAPSHOT_TOP_MOVES;
            for (int i = 0; i < snap.top_count; i++) snap.top_moves[i] = depth_scores[i];
            on_snapshot(&snap, user);
        }

        for (int i = 0; i < depth_count; i++) {
            root.moves[i] = depth_scores[i].mv;
            completed[i] = depth_scores[i];
        }
        root.count = depth_count;
        completed_count = depth_count;
    }

    *out = completed_count > 0 ? pick_with_noise(&s, completed, completed_count) : root.moves[0];
    return true;
}
