#ifndef CHESSPLATE_CHESS_H
#define CHESSPLATE_CHESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CASTLE_WHITE_KINGSIDE 0x1
#define CASTLE_WHITE_QUEENSIDE 0x2
#define CASTLE_BLACK_KINGSIDE 0x4
#define CASTLE_BLACK_QUEENSIDE 0x8

typedef enum { COLOR_WHITE = 0, COLOR_BLACK = 1 } Color;

typedef enum {
    KIND_NONE = 0,
    KIND_PAWN,
    KIND_KNIGHT,
    KIND_BISHOP,
    KIND_ROOK,
    KIND_QUEEN,
    KIND_KING
} PieceKind;

/* A piece is packed into one byte: bits 0-2 kind, bit 3 color. 0 = empty. */
typedef uint8_t Piece;

#define PIECE_NONE ((Piece)0)
#define MAKE_PIECE(color, kind) ((Piece)(((color) << 3) | (kind)))
#define PIECE_KIND(p) ((PieceKind)((p) & 7))
#define PIECE_COLOR(p) ((Color)(((p) >> 3) & 1))

#define MOVE_FLAG_EN_PASSANT 0x1
#define MOVE_FLAG_CASTLING 0x2

typedef struct {
    uint8_t from;
    uint8_t to;
    uint8_t promotion; /* PieceKind, KIND_NONE when not a promotion */
    uint8_t flags;
} Move;

#define MAX_MOVES 256

typedef struct {
    Move moves[MAX_MOVES];
    int count;
} MoveList;

typedef enum {
    STATUS_ONGOING,
    STATUS_CHECKMATE,
    STATUS_STALEMATE,
    STATUS_DRAW_FIFTY_MOVE
} GameStatusKind;

typedef struct {
    GameStatusKind kind;
    Color winner; /* valid for STATUS_CHECKMATE */
} GameStatus;

typedef struct {
    Piece board[64];
    Color side_to_move;
    uint8_t castling_rights;
    int8_t en_passant; /* -1 when none */
    uint16_t halfmove_clock;
    uint16_t fullmove_number;
} Position;

static inline Color color_opposite(Color c) { return c == COLOR_WHITE ? COLOR_BLACK : COLOR_WHITE; }
const char *color_name(Color c);

static inline bool move_is_en_passant(Move m) { return (m.flags & MOVE_FLAG_EN_PASSANT) != 0; }
static inline bool move_is_castling(Move m) { return (m.flags & MOVE_FLAG_CASTLING) != 0; }
static inline bool move_equal(Move a, Move b) {
    return a.from == b.from && a.to == b.to && a.promotion == b.promotion && a.flags == b.flags;
}
Move move_new(int from, int to);

void position_startpos(Position *pos);
static inline Piece position_piece_at(const Position *pos, int square) { return pos->board[square]; }

void position_legal_moves(const Position *pos, MoveList *out);
void position_tactical_moves(const Position *pos, MoveList *out);
bool position_is_capture(const Position *pos, Move mv);
bool position_is_in_check(const Position *pos, Color color);
bool position_square_attacked(const Position *pos, int square, Color by_color);
GameStatus position_game_status(const Position *pos);
bool position_make_move(Position *pos, Move mv);
int position_king_square(const Position *pos, Color color); /* -1 if absent */

/* Parses a FEN string. Returns false on malformed input. */
bool position_from_fen(Position *pos, const char *fen);
/* Writes the FEN string into buf (at least 96 bytes recommended). */
void position_to_fen(const Position *pos, char *buf, size_t size);

/* buf must hold at least 3 bytes. */
void square_name(int square, char *buf);
/* buf must hold at least 6 bytes. */
void move_to_coord(Move mv, char *buf);
/* buf must hold at least 16 bytes. */
void move_to_san(const Position *pos, Move mv, char *buf);

static inline int file_of(int square) { return square % 8; }
static inline int rank_of(int square) { return square / 8; }
/* Returns -1 when off board. */
static inline int to_square(int file, int rank) {
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1;
    return rank * 8 + file;
}

#endif
