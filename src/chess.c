#include "chess.h"

#include <stdio.h>
#include <string.h>

static const int KNIGHT_JUMPS[8][2] = {
    {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
};
static const int BISHOP_DIRS[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
static const int ROOK_DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static const int QUEEN_DIRS[8][2] = {
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1},
};
static const int KING_DIRS[8][2] = {
    {1, 1}, {1, 0}, {1, -1}, {0, 1}, {0, -1}, {-1, 1}, {-1, 0}, {-1, -1},
};

const char *color_name(Color c) { return c == COLOR_WHITE ? "White" : "Black"; }

Move move_new(int from, int to) {
    Move m;
    m.from = (uint8_t)from;
    m.to = (uint8_t)to;
    m.promotion = KIND_NONE;
    m.flags = 0;
    return m;
}

static void push_move(MoveList *list, Move m) {
    if (list->count < MAX_MOVES) list->moves[list->count++] = m;
}

void position_startpos(Position *pos) {
    static const PieceKind back_rank[8] = {
        KIND_ROOK, KIND_KNIGHT, KIND_BISHOP, KIND_QUEEN, KIND_KING, KIND_BISHOP, KIND_KNIGHT, KIND_ROOK,
    };
    memset(pos, 0, sizeof(*pos));
    for (int file = 0; file < 8; file++) {
        pos->board[file] = MAKE_PIECE(COLOR_WHITE, back_rank[file]);
        pos->board[8 + file] = MAKE_PIECE(COLOR_WHITE, KIND_PAWN);
        pos->board[56 + file] = MAKE_PIECE(COLOR_BLACK, back_rank[file]);
        pos->board[48 + file] = MAKE_PIECE(COLOR_BLACK, KIND_PAWN);
    }
    pos->side_to_move = COLOR_WHITE;
    pos->castling_rights =
        CASTLE_WHITE_KINGSIDE | CASTLE_WHITE_QUEENSIDE | CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE;
    pos->en_passant = -1;
    pos->halfmove_clock = 0;
    pos->fullmove_number = 1;
}

int position_king_square(const Position *pos, Color color) {
    Piece king = MAKE_PIECE(color, KIND_KING);
    for (int sq = 0; sq < 64; sq++) {
        if (pos->board[sq] == king) return sq;
    }
    return -1;
}

bool position_square_attacked(const Position *pos, int square, Color by_color) {
    int file = file_of(square);
    int rank = rank_of(square);

    int pawn_source_rank = by_color == COLOR_WHITE ? rank - 1 : rank + 1;
    for (int i = 0; i < 2; i++) {
        int src = to_square(i == 0 ? file - 1 : file + 1, pawn_source_rank);
        if (src >= 0 && pos->board[src] == MAKE_PIECE(by_color, KIND_PAWN)) return true;
    }

    for (int i = 0; i < 8; i++) {
        int src = to_square(file + KNIGHT_JUMPS[i][0], rank + KNIGHT_JUMPS[i][1]);
        if (src >= 0 && pos->board[src] == MAKE_PIECE(by_color, KIND_KNIGHT)) return true;
    }

    for (int i = 0; i < 8; i++) {
        int src = to_square(file + KING_DIRS[i][0], rank + KING_DIRS[i][1]);
        if (src >= 0 && pos->board[src] == MAKE_PIECE(by_color, KIND_KING)) return true;
    }

    for (int i = 0; i < 4; i++) {
        int df = BISHOP_DIRS[i][0], dr = BISHOP_DIRS[i][1];
        int f = file + df, r = rank + dr, src;
        while ((src = to_square(f, r)) >= 0) {
            Piece p = pos->board[src];
            if (p != PIECE_NONE) {
                if (PIECE_COLOR(p) == by_color && (PIECE_KIND(p) == KIND_BISHOP || PIECE_KIND(p) == KIND_QUEEN))
                    return true;
                break;
            }
            f += df;
            r += dr;
        }
    }

    for (int i = 0; i < 4; i++) {
        int df = ROOK_DIRS[i][0], dr = ROOK_DIRS[i][1];
        int f = file + df, r = rank + dr, src;
        while ((src = to_square(f, r)) >= 0) {
            Piece p = pos->board[src];
            if (p != PIECE_NONE) {
                if (PIECE_COLOR(p) == by_color && (PIECE_KIND(p) == KIND_ROOK || PIECE_KIND(p) == KIND_QUEEN))
                    return true;
                break;
            }
            f += df;
            r += dr;
        }
    }

    return false;
}

bool position_is_in_check(const Position *pos, Color color) {
    int king = position_king_square(pos, color);
    if (king < 0) return false;
    return position_square_attacked(pos, king, color_opposite(color));
}

bool position_is_capture(const Position *pos, Move mv) {
    if (move_is_en_passant(mv)) return true;
    return pos->board[mv.to] != PIECE_NONE;
}

static void push_promotions(int from, int to, MoveList *moves) {
    static const PieceKind kinds[4] = {KIND_QUEEN, KIND_ROOK, KIND_BISHOP, KIND_KNIGHT};
    for (int i = 0; i < 4; i++) {
        Move m = move_new(from, to);
        m.promotion = (uint8_t)kinds[i];
        push_move(moves, m);
    }
}

static void gen_pawn_moves(const Position *pos, int from, Color color, MoveList *moves) {
    int file = file_of(from);
    int rank = rank_of(from);
    int direction = color == COLOR_WHITE ? 1 : -1;
    int start_rank = color == COLOR_WHITE ? 1 : 6;
    int promotion_rank = color == COLOR_WHITE ? 7 : 0;

    int one_step_rank = rank + direction;
    int one_step = to_square(file, one_step_rank);
    if (one_step >= 0 && pos->board[one_step] == PIECE_NONE) {
        if (one_step_rank == promotion_rank) {
            push_promotions(from, one_step, moves);
        } else {
            push_move(moves, move_new(from, one_step));
            if (rank == start_rank) {
                int two_step = to_square(file, rank + 2 * direction);
                if (two_step >= 0 && pos->board[two_step] == PIECE_NONE) {
                    push_move(moves, move_new(from, two_step));
                }
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        int target_file = file + (i == 0 ? -1 : 1);
        int target_rank = rank + direction;
        int target = to_square(target_file, target_rank);
        if (target < 0) continue;

        Piece tp = pos->board[target];
        if (tp != PIECE_NONE) {
            if (PIECE_COLOR(tp) != color) {
                if (target_rank == promotion_rank) {
                    push_promotions(from, target, moves);
                } else {
                    push_move(moves, move_new(from, target));
                }
            }
            continue;
        }

        if (pos->en_passant == target) {
            Move m = move_new(from, target);
            m.flags |= MOVE_FLAG_EN_PASSANT;
            push_move(moves, m);
        }
    }
}

static void gen_step_moves(const Position *pos, int from, Color color, const int (*offsets)[2], int count,
                           MoveList *moves) {
    int file = file_of(from);
    int rank = rank_of(from);
    for (int i = 0; i < count; i++) {
        int target = to_square(file + offsets[i][0], rank + offsets[i][1]);
        if (target < 0) continue;
        Piece tp = pos->board[target];
        if (tp != PIECE_NONE && PIECE_COLOR(tp) == color) continue;
        push_move(moves, move_new(from, target));
    }
}

static void gen_slider_moves(const Position *pos, int from, Color color, const int (*dirs)[2], int count,
                             MoveList *moves) {
    int file = file_of(from);
    int rank = rank_of(from);
    for (int i = 0; i < count; i++) {
        int df = dirs[i][0], dr = dirs[i][1];
        int f = file + df, r = rank + dr, target;
        while ((target = to_square(f, r)) >= 0) {
            Piece tp = pos->board[target];
            if (tp != PIECE_NONE) {
                if (PIECE_COLOR(tp) != color) push_move(moves, move_new(from, target));
                break;
            }
            push_move(moves, move_new(from, target));
            f += df;
            r += dr;
        }
    }
}

static void gen_castling_moves(const Position *pos, int from, Color color, MoveList *moves) {
    if (position_is_in_check(pos, color)) return;

    if (color == COLOR_WHITE) {
        if (from != 4) return;
        if ((pos->castling_rights & CASTLE_WHITE_KINGSIDE) && pos->board[5] == PIECE_NONE &&
            pos->board[6] == PIECE_NONE && pos->board[7] == MAKE_PIECE(color, KIND_ROOK) &&
            !position_square_attacked(pos, 5, COLOR_BLACK) && !position_square_attacked(pos, 6, COLOR_BLACK)) {
            Move m = move_new(4, 6);
            m.flags |= MOVE_FLAG_CASTLING;
            push_move(moves, m);
        }
        if ((pos->castling_rights & CASTLE_WHITE_QUEENSIDE) && pos->board[1] == PIECE_NONE &&
            pos->board[2] == PIECE_NONE && pos->board[3] == PIECE_NONE &&
            pos->board[0] == MAKE_PIECE(color, KIND_ROOK) && !position_square_attacked(pos, 3, COLOR_BLACK) &&
            !position_square_attacked(pos, 2, COLOR_BLACK)) {
            Move m = move_new(4, 2);
            m.flags |= MOVE_FLAG_CASTLING;
            push_move(moves, m);
        }
    } else {
        if (from != 60) return;
        if ((pos->castling_rights & CASTLE_BLACK_KINGSIDE) && pos->board[61] == PIECE_NONE &&
            pos->board[62] == PIECE_NONE && pos->board[63] == MAKE_PIECE(color, KIND_ROOK) &&
            !position_square_attacked(pos, 61, COLOR_WHITE) && !position_square_attacked(pos, 62, COLOR_WHITE)) {
            Move m = move_new(60, 62);
            m.flags |= MOVE_FLAG_CASTLING;
            push_move(moves, m);
        }
        if ((pos->castling_rights & CASTLE_BLACK_QUEENSIDE) && pos->board[57] == PIECE_NONE &&
            pos->board[58] == PIECE_NONE && pos->board[59] == PIECE_NONE &&
            pos->board[56] == MAKE_PIECE(color, KIND_ROOK) && !position_square_attacked(pos, 59, COLOR_WHITE) &&
            !position_square_attacked(pos, 58, COLOR_WHITE)) {
            Move m = move_new(60, 58);
            m.flags |= MOVE_FLAG_CASTLING;
            push_move(moves, m);
        }
    }
}

static void gen_pseudo_moves(const Position *pos, MoveList *moves) {
    moves->count = 0;
    for (int sq = 0; sq < 64; sq++) {
        Piece p = pos->board[sq];
        if (p == PIECE_NONE || PIECE_COLOR(p) != pos->side_to_move) continue;
        Color c = PIECE_COLOR(p);
        switch (PIECE_KIND(p)) {
        case KIND_PAWN: gen_pawn_moves(pos, sq, c, moves); break;
        case KIND_KNIGHT: gen_step_moves(pos, sq, c, KNIGHT_JUMPS, 8, moves); break;
        case KIND_BISHOP: gen_slider_moves(pos, sq, c, BISHOP_DIRS, 4, moves); break;
        case KIND_ROOK: gen_slider_moves(pos, sq, c, ROOK_DIRS, 4, moves); break;
        case KIND_QUEEN: gen_slider_moves(pos, sq, c, QUEEN_DIRS, 8, moves); break;
        case KIND_KING:
            /* Same ordering as the king-move loop: df -1..1, dr -1..1. */
            {
                int file = file_of(sq), rank = rank_of(sq);
                for (int df = -1; df <= 1; df++) {
                    for (int dr = -1; dr <= 1; dr++) {
                        if (df == 0 && dr == 0) continue;
                        int target = to_square(file + df, rank + dr);
                        if (target < 0) continue;
                        Piece tp = pos->board[target];
                        if (tp != PIECE_NONE && PIECE_COLOR(tp) == c) continue;
                        push_move(moves, move_new(sq, target));
                    }
                }
            }
            gen_castling_moves(pos, sq, c, moves);
            break;
        default: break;
        }
    }
}

void position_legal_moves(const Position *pos, MoveList *out) {
    MoveList pseudo;
    gen_pseudo_moves(pos, &pseudo);
    out->count = 0;
    for (int i = 0; i < pseudo.count; i++) {
        Position next = *pos;
        if (!position_make_move(&next, pseudo.moves[i])) continue;
        if (!position_is_in_check(&next, pos->side_to_move)) push_move(out, pseudo.moves[i]);
    }
}

void position_tactical_moves(const Position *pos, MoveList *out) {
    MoveList legal;
    position_legal_moves(pos, &legal);
    out->count = 0;
    for (int i = 0; i < legal.count; i++) {
        Move m = legal.moves[i];
        if (position_is_capture(pos, m) || m.promotion != KIND_NONE) push_move(out, m);
    }
}

GameStatus position_game_status(const Position *pos) {
    GameStatus status;
    status.winner = COLOR_WHITE;
    if (pos->halfmove_clock >= 100) {
        status.kind = STATUS_DRAW_FIFTY_MOVE;
        return status;
    }
    MoveList legal;
    position_legal_moves(pos, &legal);
    if (legal.count == 0) {
        if (position_is_in_check(pos, pos->side_to_move)) {
            status.kind = STATUS_CHECKMATE;
            status.winner = color_opposite(pos->side_to_move);
        } else {
            status.kind = STATUS_STALEMATE;
        }
    } else {
        status.kind = STATUS_ONGOING;
    }
    return status;
}

static void clear_castling_right_by_rook_square(Position *pos, int square) {
    switch (square) {
    case 0: pos->castling_rights &= (uint8_t)~CASTLE_WHITE_QUEENSIDE; break;
    case 7: pos->castling_rights &= (uint8_t)~CASTLE_WHITE_KINGSIDE; break;
    case 56: pos->castling_rights &= (uint8_t)~CASTLE_BLACK_QUEENSIDE; break;
    case 63: pos->castling_rights &= (uint8_t)~CASTLE_BLACK_KINGSIDE; break;
    default: break;
    }
}

static void move_rook(Position *pos, int from, int to) {
    Piece rook = pos->board[from];
    pos->board[from] = PIECE_NONE;
    pos->board[to] = rook;
}

bool position_make_move(Position *pos, Move mv) {
    Piece moving = pos->board[mv.from];
    if (moving == PIECE_NONE) return false;
    if (PIECE_COLOR(moving) != pos->side_to_move) return false;

    Color mover = PIECE_COLOR(moving);
    Piece captured;

    if (move_is_en_passant(mv)) {
        int cap_sq = mover == COLOR_WHITE ? mv.to - 8 : mv.to + 8;
        if (cap_sq < 0) cap_sq = 0;
        if (cap_sq > 63) cap_sq = 63;
        captured = pos->board[cap_sq];
        pos->board[cap_sq] = PIECE_NONE;
    } else {
        captured = pos->board[mv.to];
    }

    if (captured != PIECE_NONE && PIECE_KIND(captured) == KIND_ROOK) {
        clear_castling_right_by_rook_square(pos, mv.to);
    }
    if (PIECE_KIND(moving) == KIND_KING) {
        if (mover == COLOR_WHITE)
            pos->castling_rights &= (uint8_t)~(CASTLE_WHITE_KINGSIDE | CASTLE_WHITE_QUEENSIDE);
        else
            pos->castling_rights &= (uint8_t)~(CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE);
    }
    if (PIECE_KIND(moving) == KIND_ROOK) {
        clear_castling_right_by_rook_square(pos, mv.from);
    }

    pos->board[mv.from] = PIECE_NONE;

    if (move_is_castling(mv)) {
        if (mv.from == 4 && mv.to == 6) move_rook(pos, 7, 5);
        else if (mv.from == 4 && mv.to == 2) move_rook(pos, 0, 3);
        else if (mv.from == 60 && mv.to == 62) move_rook(pos, 63, 61);
        else if (mv.from == 60 && mv.to == 58) move_rook(pos, 56, 59);
    }

    if (mv.promotion != KIND_NONE) moving = MAKE_PIECE(mover, mv.promotion);
    pos->board[mv.to] = moving;

    pos->en_passant = -1;
    if (PIECE_KIND(moving) == KIND_PAWN) {
        int distance = (int)mv.to - (int)mv.from;
        if (distance == 16 || distance == -16) {
            pos->en_passant = (int8_t)(mover == COLOR_WHITE ? mv.from + 8 : mv.from - 8);
        }
    }

    if (PIECE_KIND(moving) == KIND_PAWN || captured != PIECE_NONE) {
        pos->halfmove_clock = 0;
    } else if (pos->halfmove_clock < UINT16_MAX) {
        pos->halfmove_clock++;
    }

    if (mover == COLOR_BLACK && pos->fullmove_number < UINT16_MAX) pos->fullmove_number++;

    pos->side_to_move = color_opposite(pos->side_to_move);
    return true;
}

static char piece_to_fen_char(Piece p) {
    char ch;
    switch (PIECE_KIND(p)) {
    case KIND_PAWN: ch = 'p'; break;
    case KIND_KNIGHT: ch = 'n'; break;
    case KIND_BISHOP: ch = 'b'; break;
    case KIND_ROOK: ch = 'r'; break;
    case KIND_QUEEN: ch = 'q'; break;
    case KIND_KING: ch = 'k'; break;
    default: ch = '?'; break;
    }
    if (PIECE_COLOR(p) == COLOR_WHITE) ch = (char)(ch - 'a' + 'A');
    return ch;
}

void position_to_fen(const Position *pos, char *buf, size_t size) {
    char board[80];
    int n = 0;
    for (int rank = 7; rank >= 0; rank--) {
        int empty = 0;
        for (int file = 0; file < 8; file++) {
            Piece p = pos->board[rank * 8 + file];
            if (p != PIECE_NONE) {
                if (empty > 0) {
                    board[n++] = (char)('0' + empty);
                    empty = 0;
                }
                board[n++] = piece_to_fen_char(p);
            } else {
                empty++;
            }
        }
        if (empty > 0) board[n++] = (char)('0' + empty);
        if (rank > 0) board[n++] = '/';
    }
    board[n] = '\0';

    char castling[5];
    int c = 0;
    if (pos->castling_rights & CASTLE_WHITE_KINGSIDE) castling[c++] = 'K';
    if (pos->castling_rights & CASTLE_WHITE_QUEENSIDE) castling[c++] = 'Q';
    if (pos->castling_rights & CASTLE_BLACK_KINGSIDE) castling[c++] = 'k';
    if (pos->castling_rights & CASTLE_BLACK_QUEENSIDE) castling[c++] = 'q';
    if (c == 0) castling[c++] = '-';
    castling[c] = '\0';

    char ep[3];
    if (pos->en_passant >= 0) {
        square_name(pos->en_passant, ep);
    } else {
        ep[0] = '-';
        ep[1] = '\0';
    }

    snprintf(buf, size, "%s %c %s %s %u %u", board, pos->side_to_move == COLOR_WHITE ? 'w' : 'b', castling, ep,
             (unsigned)pos->halfmove_clock, (unsigned)pos->fullmove_number);
}

bool position_from_fen(Position *pos, const char *fen) {
    Position p;
    memset(&p, 0, sizeof(p));
    p.en_passant = -1;
    p.fullmove_number = 1;

    const char *s = fen;
    int rank = 7, file = 0;
    for (; *s && *s != ' '; s++) {
        char ch = *s;
        if (ch == '/') {
            if (file != 8) return false;
            rank--;
            file = 0;
            if (rank < 0) return false;
        } else if (ch >= '1' && ch <= '8') {
            file += ch - '0';
            if (file > 8) return false;
        } else {
            Color color = (ch >= 'a' && ch <= 'z') ? COLOR_BLACK : COLOR_WHITE;
            char lower = (char)(color == COLOR_WHITE ? ch - 'A' + 'a' : ch);
            PieceKind kind;
            switch (lower) {
            case 'p': kind = KIND_PAWN; break;
            case 'n': kind = KIND_KNIGHT; break;
            case 'b': kind = KIND_BISHOP; break;
            case 'r': kind = KIND_ROOK; break;
            case 'q': kind = KIND_QUEEN; break;
            case 'k': kind = KIND_KING; break;
            default: return false;
            }
            if (file > 7) return false;
            p.board[rank * 8 + file] = MAKE_PIECE(color, kind);
            file++;
        }
    }
    if (rank != 0 || file != 8) return false;

    while (*s == ' ') s++;
    if (*s == 'w') p.side_to_move = COLOR_WHITE;
    else if (*s == 'b') p.side_to_move = COLOR_BLACK;
    else return false;
    s++;

    while (*s == ' ') s++;
    for (; *s && *s != ' '; s++) {
        switch (*s) {
        case 'K': p.castling_rights |= CASTLE_WHITE_KINGSIDE; break;
        case 'Q': p.castling_rights |= CASTLE_WHITE_QUEENSIDE; break;
        case 'k': p.castling_rights |= CASTLE_BLACK_KINGSIDE; break;
        case 'q': p.castling_rights |= CASTLE_BLACK_QUEENSIDE; break;
        case '-': break;
        default: return false;
        }
    }

    while (*s == ' ') s++;
    if (*s >= 'a' && *s <= 'h' && s[1] >= '1' && s[1] <= '8') {
        p.en_passant = (int8_t)((s[1] - '1') * 8 + (s[0] - 'a'));
        s += 2;
    } else if (*s == '-') {
        s++;
    }

    while (*s == ' ') s++;
    if (*s) {
        unsigned half = 0, full = 1;
        if (sscanf(s, "%u %u", &half, &full) >= 1) {
            p.halfmove_clock = (uint16_t)half;
            p.fullmove_number = (uint16_t)(full ? full : 1);
        }
    }

    *pos = p;
    return true;
}

void square_name(int square, char *buf) {
    buf[0] = (char)('a' + square % 8);
    buf[1] = (char)('1' + square / 8);
    buf[2] = '\0';
}

void move_to_coord(Move mv, char *buf) {
    square_name(mv.from, buf);
    square_name(mv.to, buf + 2);
    if (mv.promotion != KIND_NONE) {
        char ch;
        switch (mv.promotion) {
        case KIND_ROOK: ch = 'r'; break;
        case KIND_BISHOP: ch = 'b'; break;
        case KIND_KNIGHT: ch = 'n'; break;
        default: ch = 'q'; break;
        }
        buf[4] = ch;
        buf[5] = '\0';
    }
}

static char piece_kind_letter(int kind) {
    switch (kind) {
    case KIND_KNIGHT: return 'N';
    case KIND_BISHOP: return 'B';
    case KIND_ROOK: return 'R';
    case KIND_QUEEN: return 'Q';
    case KIND_KING: return 'K';
    default: return 'P';
    }
}

void move_to_san(const Position *pos, Move mv, char *buf) {
    Piece moving = pos->board[mv.from];
    if (moving == PIECE_NONE) {
        move_to_coord(mv, buf);
        return;
    }

    int n = 0;
    char sq[3];
    if (move_is_castling(mv)) {
        const char *text = mv.to > mv.from ? "O-O" : "O-O-O";
        n = (int)strlen(text);
        memcpy(buf, text, (size_t)n);
    } else {
        bool is_capture = position_is_capture(pos, mv);
        if (PIECE_KIND(moving) == KIND_PAWN) {
            if (is_capture) {
                buf[n++] = (char)('a' + mv.from % 8);
                buf[n++] = 'x';
            }
            square_name(mv.to, sq);
            buf[n++] = sq[0];
            buf[n++] = sq[1];
            if (mv.promotion != KIND_NONE) {
                buf[n++] = '=';
                buf[n++] = piece_kind_letter(mv.promotion);
            }
        } else {
            buf[n++] = piece_kind_letter(PIECE_KIND(moving));

            int from_file = mv.from % 8, from_rank = mv.from / 8;
            bool needs_file = false, needs_rank = false;
            MoveList legal;
            position_legal_moves(pos, &legal);
            for (int i = 0; i < legal.count; i++) {
                Move other = legal.moves[i];
                if (other.to != mv.to || other.from == mv.from) continue;
                Piece op = pos->board[other.from];
                if (op != PIECE_NONE && PIECE_KIND(op) == PIECE_KIND(moving)) {
                    if (other.from % 8 == from_file) needs_rank = true;
                    else needs_file = true;
                }
            }
            if (needs_file) buf[n++] = (char)('a' + from_file);
            if (needs_rank) buf[n++] = (char)('1' + from_rank);

            if (is_capture) buf[n++] = 'x';
            square_name(mv.to, sq);
            buf[n++] = sq[0];
            buf[n++] = sq[1];
        }
    }

    Position next = *pos;
    if (position_make_move(&next, mv)) {
        if (position_game_status(&next).kind == STATUS_CHECKMATE) {
            buf[n++] = '#';
        } else if (position_is_in_check(&next, next.side_to_move)) {
            buf[n++] = '+';
        }
    }
    buf[n] = '\0';
}
