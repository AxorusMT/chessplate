#include "insight.h"

#include <string.h>

typedef enum { PHASE_OPENING, PHASE_MIDDLEGAME, PHASE_ENDGAME } GamePhase;

typedef struct {
    const char *name;
    const char *uci_moves[7]; /* NULL-terminated */
} OpeningLine;

typedef struct {
    int white_queens, black_queens;
    int rooks, bishops, knights, pawns;
    int non_pawn_material;
    int total_piece_count;
} MaterialSnapshot;

static const OpeningLine OPENING_BOOK[] = {
    {"King's Pawn Game", {"e2e4", "e7e5", NULL}},
    {"Italian Game", {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", NULL}},
    {"Ruy Lopez", {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", NULL}},
    {"Scotch Game", {"e2e4", "e7e5", "g1f3", "b8c6", "d2d4", NULL}},
    {"Petrov Defense", {"e2e4", "e7e5", "g1f3", "g8f6", NULL}},
    {"Sicilian Defense", {"e2e4", "c7c5", NULL}},
    {"French Defense", {"e2e4", "e7e6", NULL}},
    {"Caro-Kann Defense", {"e2e4", "c7c6", NULL}},
    {"Pirc Defense", {"e2e4", "d7d6", NULL}},
    {"Scandinavian Defense", {"e2e4", "d7d5", NULL}},
    {"Queen's Gambit", {"d2d4", "d7d5", "c2c4", NULL}},
    {"Queen's Gambit Accepted", {"d2d4", "d7d5", "c2c4", "d5c4", NULL}},
    {"Queen's Gambit Declined", {"d2d4", "d7d5", "c2c4", "e7e6", NULL}},
    {"Slav Defense", {"d2d4", "d7d5", "c2c4", "c7c6", NULL}},
    {"King's Indian Defense", {"d2d4", "g8f6", "c2c4", "g7g6", NULL}},
    {"Nimzo-Indian Defense", {"d2d4", "g8f6", "c2c4", "e7e6", "b1c3", "f8b4", NULL}},
    {"Dutch Defense", {"d2d4", "f7f5", NULL}},
    {"English Opening", {"c2c4", NULL}},
    {"Reti Opening", {"g1f3", NULL}},
    {"London System", {"d2d4", "d7d5", "g1f3", "g8f6", "c1f4", NULL}},
};

static const char *phase_name(GamePhase phase) {
    switch (phase) {
    case PHASE_OPENING: return "Opening";
    case PHASE_MIDDLEGAME: return "Middlegame";
    default: return "Endgame";
    }
}

static MaterialSnapshot material_snapshot(const Position *position) {
    MaterialSnapshot m;
    memset(&m, 0, sizeof(m));
    for (int sq = 0; sq < 64; sq++) {
        Piece p = position->board[sq];
        if (p == PIECE_NONE) continue;
        m.total_piece_count++;
        switch (PIECE_KIND(p)) {
        case KIND_PAWN: m.pawns++; break;
        case KIND_KNIGHT:
            m.knights++;
            m.non_pawn_material += 320;
            break;
        case KIND_BISHOP:
            m.bishops++;
            m.non_pawn_material += 330;
            break;
        case KIND_ROOK:
            m.rooks++;
            m.non_pawn_material += 500;
            break;
        case KIND_QUEEN:
            if (PIECE_COLOR(p) == COLOR_WHITE) m.white_queens++;
            else m.black_queens++;
            m.non_pawn_material += 900;
            break;
        default: break;
        }
    }
    return m;
}

static const char *detect_opening(const char (*move_log)[6], int move_count) {
    const OpeningLine *best = NULL;
    int best_len = 0;
    for (size_t i = 0; i < sizeof(OPENING_BOOK) / sizeof(OPENING_BOOK[0]); i++) {
        const OpeningLine *line = &OPENING_BOOK[i];
        int len = 0;
        while (line->uci_moves[len]) len++;
        if (move_count < len) continue;

        bool matches = true;
        for (int j = 0; j < len; j++) {
            if (strcmp(line->uci_moves[j], move_log[j]) != 0) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        if (!best || len > best_len) {
            best = line;
            best_len = len;
        }
    }
    return best ? best->name : NULL;
}

static bool is_endgame(MaterialSnapshot m) {
    int queen_count = m.white_queens + m.black_queens;
    int minor_count = m.bishops + m.knights;
    if (m.non_pawn_material <= 2400) return true;
    if (queen_count == 0 && m.rooks <= 2 && minor_count <= 4) return true;
    if (queen_count <= 1 && m.rooks <= 1 && minor_count <= 2) return true;
    return false;
}

static GamePhase classify_phase(const Position *position, int move_count) {
    MaterialSnapshot m = material_snapshot(position);
    if (is_endgame(m)) return PHASE_ENDGAME;
    if (move_count <= 20 && m.non_pawn_material >= 6200 && m.total_piece_count >= 24) return PHASE_OPENING;
    return PHASE_MIDDLEGAME;
}

/* +1 kingside, -1 queenside, 0 not castled. */
static int castled_side(int king_square, Color color) {
    if ((color == COLOR_WHITE && king_square == 6) || (color == COLOR_BLACK && king_square == 62)) return 1;
    if ((color == COLOR_WHITE && king_square == 2) || (color == COLOR_BLACK && king_square == 58)) return -1;
    return 0;
}

static bool has_opposite_side_castling(const Position *position) {
    int wk = position_king_square(position, COLOR_WHITE);
    int bk = position_king_square(position, COLOR_BLACK);
    if (wk < 0 || bk < 0) return false;
    int ws = castled_side(wk, COLOR_WHITE);
    int bs = castled_side(bk, COLOR_BLACK);
    return ws != 0 && bs != 0 && ws != bs;
}

static bool center_is_open(const Position *position) {
    int central_pawns = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = position->board[sq];
        if (p != PIECE_NONE && PIECE_KIND(p) == KIND_PAWN && (sq % 8 == 3 || sq % 8 == 4)) central_pawns++;
    }
    return central_pawns <= 1;
}

static bool is_pawn_at(const Position *position, int square, Color color) {
    return position->board[square] == MAKE_PIECE(color, KIND_PAWN);
}

static bool center_is_closed(const Position *position) {
    bool d4_locked = is_pawn_at(position, 27, COLOR_WHITE) && is_pawn_at(position, 35, COLOR_BLACK);
    bool e4_locked = is_pawn_at(position, 28, COLOR_WHITE) && is_pawn_at(position, 36, COLOR_BLACK);
    return d4_locked || e4_locked;
}

static const char *middlegame_theme(const Position *position) {
    MaterialSnapshot m = material_snapshot(position);
    int queen_count = m.white_queens + m.black_queens;
    if (has_opposite_side_castling(position)) return "Opposite-Side Castling Attack";
    if (queen_count == 0) return "Queenless Middlegame";
    if (center_is_closed(position)) return "Closed Center Middlegame";
    if (center_is_open(position)) return "Open Center Middlegame";
    return "Classical Piece Middlegame";
}

static int square_color(int square) { return (square % 8 + square / 8) % 2; }

static bool is_opposite_colored_bishop_endgame(const Position *position) {
    MaterialSnapshot m = material_snapshot(position);
    if (m.rooks > 0 || m.white_queens + m.black_queens > 0 || m.knights > 0) return false;

    int white_sq = -1, black_sq = -1;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = position->board[sq];
        if (p == PIECE_NONE || PIECE_KIND(p) != KIND_BISHOP) continue;
        if (PIECE_COLOR(p) == COLOR_WHITE) {
            if (white_sq >= 0) return false;
            white_sq = sq;
        } else {
            if (black_sq >= 0) return false;
            black_sq = sq;
        }
    }
    if (white_sq < 0 || black_sq < 0) return false;
    return square_color(white_sq) != square_color(black_sq);
}

static const char *endgame_theme(const Position *position) {
    MaterialSnapshot m = material_snapshot(position);
    int queen_count = m.white_queens + m.black_queens;
    int minor_count = m.bishops + m.knights;

    if (m.total_piece_count == 2) return "Bare Kings";
    if (m.non_pawn_material <= 200 && m.pawns > 0) return "King and Pawn Endgame";
    if (is_opposite_colored_bishop_endgame(position)) return "Opposite-Colored Bishops Endgame";
    if (m.rooks > 0 && queen_count == 0 && minor_count <= 2) return "Rook Endgame";
    if (queen_count > 0 && m.rooks <= 1 && minor_count <= 2) return "Queen Endgame";
    if (minor_count > 0 && m.rooks == 0 && queen_count == 0) return "Minor-Piece Endgame";
    return "Technical Endgame";
}

PhaseNames phase_names(const Position *position, const char (*move_log)[6], int move_count) {
    PhaseNames names;
    GamePhase phase = classify_phase(position, move_count);
    const char *opening = detect_opening(move_log, move_count);

    names.current_phase = phase_name(phase);
    names.opening_name = opening ? opening : "Unclassified Opening";
    names.middlegame_name = phase == PHASE_OPENING ? "Not reached yet" : middlegame_theme(position);
    names.endgame_name = phase == PHASE_ENDGAME ? endgame_theme(position) : "Not reached yet";
    return names;
}
