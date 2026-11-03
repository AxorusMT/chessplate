/* Chessplate test suite: rules, notation, engine, insights, rendering and a
 * headless drive of the full application UI. Run with `make test`. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "assets.h"
#include "chess.h"
#include "engine.h"
#include "insight.h"
#include "metrics.h"
#include "svg.h"
#include "sys.h"
#include "theme.h"

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_failures++;                                                              \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                              \
    } while (0)

#define CHECK_STR(actual, expected)                                                                  \
    do {                                                                                             \
        g_checks++;                                                                                  \
        if (strcmp((actual), (expected)) != 0) {                                                     \
            g_failures++;                                                                            \
            fprintf(stderr, "  FAIL %s:%d: got \"%s\", expected \"%s\"\n", __FILE__, __LINE__, (actual), \
                    (expected));                                                                     \
        }                                                                                            \
    } while (0)

static bool find_move(const Position *pos, const char *uci, Move *out) {
    MoveList legal;
    position_legal_moves(pos, &legal);
    for (int i = 0; i < legal.count; i++) {
        char coord[6];
        move_to_coord(legal.moves[i], coord);
        if (strcmp(coord, uci) == 0) {
            *out = legal.moves[i];
            return true;
        }
    }
    return false;
}

static void play(Position *pos, const char *uci) {
    Move mv;
    if (!find_move(pos, uci, &mv)) {
        fprintf(stderr, "  move %s not legal\n", uci);
        g_failures++;
        exit(1);
    }
    CHECK(position_make_move(pos, mv));
}

static int square(const char *coord) { return (coord[1] - '1') * 8 + (coord[0] - 'a'); }

static uint64_t perft(const Position *pos, int depth) {
    if (depth == 0) return 1;
    MoveList legal;
    position_legal_moves(pos, &legal);
    if (depth == 1) return (uint64_t)legal.count;
    uint64_t total = 0;
    for (int i = 0; i < legal.count; i++) {
        Position next = *pos;
        position_make_move(&next, legal.moves[i]);
        total += perft(&next, depth - 1);
    }
    return total;
}

/* ---- Ported from the original chess.rs tests --------------------------- */

static void test_start_position_has_20_legal_moves(void) {
    Position pos;
    position_startpos(&pos);
    MoveList legal;
    position_legal_moves(&pos, &legal);
    CHECK(legal.count == 20);
}

static void test_castling_rights_clear_after_king_move(void) {
    Position pos;
    position_startpos(&pos);
    play(&pos, "e2e4");
    play(&pos, "a7a6");
    play(&pos, "e1e2");
    CHECK((pos.castling_rights & (CASTLE_WHITE_KINGSIDE | CASTLE_WHITE_QUEENSIDE)) == 0);
}

static void test_en_passant_capture_works(void) {
    Position pos;
    position_startpos(&pos);
    play(&pos, "e2e4");
    play(&pos, "a7a6");
    play(&pos, "e4e5");
    play(&pos, "d7d5");
    Move ep;
    CHECK(find_move(&pos, "e5d6", &ep));
    CHECK(move_is_en_passant(ep));
    CHECK(position_make_move(&pos, ep));
    CHECK(PIECE_KIND(position_piece_at(&pos, square("d6"))) == KIND_PAWN);
    CHECK(position_piece_at(&pos, square("d5")) == PIECE_NONE);
}

static void test_fools_mate_is_checkmate(void) {
    Position pos;
    position_startpos(&pos);
    play(&pos, "f2f3");
    play(&pos, "e7e5");
    play(&pos, "g2g4");
    play(&pos, "d8h4");
    GameStatus status = position_game_status(&pos);
    CHECK(status.kind == STATUS_CHECKMATE);
    CHECK(status.winner == COLOR_BLACK);
}

static void test_start_position_fen_is_correct(void) {
    Position pos;
    position_startpos(&pos);
    char fen[128];
    position_to_fen(&pos, fen, sizeof(fen));
    CHECK_STR(fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

static void test_san_notation_for_opening_and_mate(void) {
    Position pos;
    position_startpos(&pos);
    char san[16];
    Move mv;

    CHECK(find_move(&pos, "e2e4", &mv));
    move_to_san(&pos, mv, san);
    CHECK_STR(san, "e4");
    position_make_move(&pos, mv);

    CHECK(find_move(&pos, "e7e5", &mv));
    move_to_san(&pos, mv, san);
    CHECK_STR(san, "e5");
    position_make_move(&pos, mv);

    CHECK(find_move(&pos, "g1f3", &mv));
    move_to_san(&pos, mv, san);
    CHECK_STR(san, "Nf3");

    Position mate;
    position_startpos(&mate);
    play(&mate, "f2f3");
    play(&mate, "e7e5");
    play(&mate, "g2g4");
    CHECK(find_move(&mate, "d8h4", &mv));
    move_to_san(&mate, mv, san);
    CHECK_STR(san, "Qh4#");
}

/* ---- Additional rules coverage ------------------------------------------ */

static void test_perft_reference_counts(void) {
    Position pos;
    position_startpos(&pos);
    CHECK(perft(&pos, 1) == 20);
    CHECK(perft(&pos, 2) == 400);
    CHECK(perft(&pos, 3) == 8902);
    CHECK(perft(&pos, 4) == 197281);

    /* "Kiwipete": castling, en passant, promotions, pins. */
    CHECK(position_from_fen(&pos, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));
    CHECK(perft(&pos, 1) == 48);
    CHECK(perft(&pos, 2) == 2039);
    CHECK(perft(&pos, 3) == 97862);

    CHECK(position_from_fen(&pos, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"));
    CHECK(perft(&pos, 4) == 43238);

    CHECK(position_from_fen(&pos, "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"));
    CHECK(perft(&pos, 3) == 9467);
}

static void test_fen_round_trip(void) {
    const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2",
        "8/8/8/8/8/8/8/K6k b - - 42 99",
    };
    for (size_t i = 0; i < sizeof(fens) / sizeof(fens[0]); i++) {
        Position pos;
        char out[128];
        CHECK(position_from_fen(&pos, fens[i]));
        position_to_fen(&pos, out, sizeof(out));
        CHECK_STR(out, fens[i]);
    }
    Position pos;
    CHECK(!position_from_fen(&pos, "not a fen"));
}

static void test_castling_and_promotion_san(void) {
    Position pos;
    char san[16];
    Move mv;
    CHECK(position_from_fen(&pos, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    CHECK(find_move(&pos, "e1g1", &mv));
    CHECK(move_is_castling(mv));
    move_to_san(&pos, mv, san);
    CHECK_STR(san, "O-O");
    CHECK(find_move(&pos, "e1c1", &mv));
    move_to_san(&pos, mv, san);
    CHECK_STR(san, "O-O-O");
    position_make_move(&pos, mv);
    CHECK(PIECE_KIND(position_piece_at(&pos, square("d1"))) == KIND_ROOK);
    CHECK(position_piece_at(&pos, square("a1")) == PIECE_NONE);

    CHECK(position_from_fen(&pos, "8/P6k/8/8/8/8/8/K7 w - - 0 1"));
    CHECK(find_move(&pos, "a7a8q", &mv));
    move_to_san(&pos, mv, san);
    CHECK_STR(san, "a8=Q");
    CHECK(find_move(&pos, "a7a8n", &mv));
    move_to_san(&pos, mv, san);
    CHECK_STR(san, "a8=N");

    /* Disambiguation: two knights can reach d2. */
    CHECK(position_from_fen(&pos, "4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1"));
    CHECK(find_move(&pos, "b1d2", &mv));
    move_to_san(&pos, mv, san);
    CHECK_STR(san, "Nbd2");
}

static void test_stalemate_and_fifty_move(void) {
    Position pos;
    CHECK(position_from_fen(&pos, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
    CHECK(position_game_status(&pos).kind == STATUS_STALEMATE);
    CHECK(position_from_fen(&pos, "7k/8/6K1/8/8/8/8/R7 w - - 100 80"));
    CHECK(position_game_status(&pos).kind == STATUS_DRAW_FIFTY_MOVE);
}

/* ---- Engine --------------------------------------------------------------- */

static int g_snapshot_calls;
static void count_snapshot(const SearchSnapshot *s, void *user) {
    (void)user;
    (void)s;
    g_snapshot_calls++;
}

static void test_engine_profiles(void) {
    EloProfile p = profile_for_elo(600);
    CHECK(p.depth == 2 && p.time_ms == 150 && p.node_limit == 35000 && p.randomness_cp == 275);
    p = profile_for_elo(1600);
    CHECK(p.depth == 5 && p.time_ms == 600 && p.node_limit == 300000 && p.randomness_cp == 150);
    p = profile_for_elo(2800);
    CHECK(p.depth == 8 && p.time_ms == 1800 && p.node_limit == 2200000 && p.randomness_cp == 0);
    p = profile_for_elo(100);
    CHECK(p.depth == 2);
    p = profile_for_elo(9999);
    CHECK(p.depth == 8 && p.randomness_cp == 0);
}

static void test_engine_finds_mate_in_one(void) {
    Position pos;
    position_startpos(&pos);
    play(&pos, "e2e4");
    play(&pos, "e7e5");
    play(&pos, "f1c4");
    play(&pos, "b8c6");
    play(&pos, "d1h5");
    play(&pos, "g8f6");
    Move best;
    g_snapshot_calls = 0;
    CHECK(engine_best_move(&pos, 2800, count_snapshot, NULL, NULL, &best));
    char coord[6];
    move_to_coord(best, coord);
    CHECK_STR(coord, "h5f7");
    CHECK(g_snapshot_calls >= 1);
}

static void test_engine_no_moves_and_cancel(void) {
    Position pos;
    position_startpos(&pos);
    play(&pos, "f2f3");
    play(&pos, "e7e5");
    play(&pos, "g2g4");
    play(&pos, "d8h4");
    Move best;
    CHECK(!engine_best_move(&pos, 1600, NULL, NULL, NULL, &best));

    position_startpos(&pos);
    volatile int cancel = 1;
    uint64_t start = sys_time_ms();
    CHECK(engine_best_move(&pos, 2800, NULL, NULL, &cancel, &best));
    CHECK(sys_time_ms() - start < 500);
}

static void test_evaluation_symmetry(void) {
    Position pos;
    position_startpos(&pos);
    CHECK(engine_evaluate(&pos) == 10); /* side-to-move initiative */
}

/* ---- Insights, metrics, themes ------------------------------------------- */

static PhaseNames names_after(const char **moves, int n, Position *pos) {
    static char log[64][6];
    position_startpos(pos);
    for (int i = 0; i < n; i++) {
        play(pos, moves[i]);
        snprintf(log[i], sizeof(log[i]), "%s", moves[i]);
    }
    return phase_names(pos, (const char(*)[6])log, n);
}

static void test_phase_names(void) {
    Position pos;
    PhaseNames n = names_after(NULL, 0, &pos);
    CHECK_STR(n.current_phase, "Opening");
    CHECK_STR(n.opening_name, "Unclassified Opening");
    CHECK_STR(n.middlegame_name, "Not reached yet");

    const char *italian[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4"};
    n = names_after(italian, 5, &pos);
    CHECK_STR(n.opening_name, "Italian Game");

    const char *qgd[] = {"d2d4", "d7d5", "c2c4", "e7e6"};
    n = names_after(qgd, 4, &pos);
    CHECK_STR(n.opening_name, "Queen's Gambit Declined");

    static const char empty_log[1][6] = {""};
    CHECK(position_from_fen(&pos, "8/8/8/4k3/8/8/8/4K3 w - - 0 1"));
    n = phase_names(&pos, empty_log, 0);
    CHECK_STR(n.current_phase, "Endgame");
    CHECK_STR(n.endgame_name, "Bare Kings");

    CHECK(position_from_fen(&pos, "8/8/8/4k3/8/8/4P3/4K3 w - - 0 1"));
    n = phase_names(&pos, empty_log, 0);
    CHECK_STR(n.endgame_name, "King and Pawn Endgame");

    CHECK(position_from_fen(&pos, "8/8/8/4k3/8/8/4P3/R3K3 w - - 0 1"));
    n = phase_names(&pos, empty_log, 0);
    CHECK_STR(n.endgame_name, "Rook Endgame");
}

static void test_metrics(void) {
    char buf[32];
    format_big_number(999, buf, sizeof(buf));
    CHECK_STR(buf, "999");
    format_big_number(1234, buf, sizeof(buf));
    CHECK_STR(buf, "1.2K");
    format_big_number(2345678, buf, sizeof(buf));
    CHECK_STR(buf, "2.35M");
    format_big_number(3000000000ULL, buf, sizeof(buf));
    CHECK_STR(buf, "3.00B");

    SearchSnapshot h[3];
    memset(h, 0, sizeof(h));
    h[0].best_move = move_new(12, 28);
    h[1].best_move = move_new(6, 21);
    h[2].best_move = move_new(6, 21);
    h[0].best_score_cp = 10;
    h[1].best_score_cp = 30;
    h[2].best_score_cp = 20;
    CHECK(best_move_stability(h, 1) == 1.0f);
    float st = best_move_stability(h, 3);
    CHECK(st > 0.66f && st < 0.67f);
    CHECK(eval_volatility_cp(h, 3) == 15.0f);
}

static void test_themes(void) {
    CHECK(theme_count() == 90);
    CHECK_STR(theme_get(0)->name, "Aurora Circuit Prime");
    CHECK_STR(theme_get(1)->name, "Aurora Circuit Radiant");
    CHECK_STR(theme_get(89)->name, "Golden Reef Noir");
    Rgba light = theme_get(0)->palette.board_light;
    CHECK(light.r == 237 && light.g == 244 && light.b == 255 && light.a == 255);
    Rgba arrow = theme_get(0)->palette.arrow;
    CHECK(arrow.r == 84 && arrow.g == 255 && arrow.b == 219 && arrow.a == 210);
}

/* ---- Rendering ----------------------------------------------------------- */

static void test_svg_rendering(void) {
    Image img;
    CHECK(svg_render(asset_wN_svg, asset_wN_svg_len, 64, 64, &img));
    int opaque = 0, transparent = 0;
    for (int i = 0; i < img.w * img.h; i++) {
        uint32_t a = img.pixels[i] >> 24;
        if (a == 255) opaque++;
        if (a == 0) transparent++;
    }
    CHECK(opaque > 400);
    CHECK(transparent > 1000);
    /* Corner is empty, body of the knight is filled white. */
    CHECK(img.pixels[0] == 0);
    image_free(&img);

    CHECK(svg_render(asset_logo_svg, asset_logo_svg_len, 128, 128, &img));
    uint32_t center = img.pixels[64 * 128 + 64];
    CHECK((center >> 24) == 255);
    image_free(&img);
}

/* ---- Headless application drive ------------------------------------------ */

static char g_clipboard[4096];
static void capture_clipboard(const char *text) { snprintf(g_clipboard, sizeof(g_clipboard), "%s", text); }

typedef struct {
    App *app;
    Canvas canvas;
    uint32_t *pixels;
    InputState in;
} Harness;

static void frame(Harness *h) {
    app_frame(h->app, &h->canvas, &h->in);
    input_begin_frame(&h->in);
}

static void click(Harness *h, float x, float y) {
    h->in.mouse_x = x;
    h->in.mouse_y = y;
    h->in.mouse_inside = true;
    h->in.pressed[MOUSE_LEFT] = true;
    h->in.down[MOUSE_LEFT] = true;
    frame(h);
    h->in.released[MOUSE_LEFT] = true;
    h->in.down[MOUSE_LEFT] = false;
    frame(h);
}

/* Board geometry for the default 920x680 window (mirrors app.c layout). */
static void square_xy(const char *coord, float *x, float *y) {
    const float left = 244.0f + 1.0f + 8.0f, top = 8.0f;
    const float total_w = 920.0f - left - 8.0f;
    float sq = (float)(int)((total_w - 260.0f - 12.0f) / 8.0f);
    int file = coord[0] - 'a', rank = coord[1] - '1';
    *x = left + ((float)file + 0.5f) * sq;
    *y = top + ((float)(7 - rank) + 0.5f) * sq;
}

static void click_square(Harness *h, const char *coord) {
    float x, y;
    square_xy(coord, &x, &y);
    click(h, x, y);
}

static void test_app_headless(void) {
    Harness h;
    memset(&h, 0, sizeof(h));
    h.pixels = (uint32_t *)calloc((size_t)APP_WINDOW_WIDTH * APP_WINDOW_HEIGHT, sizeof(uint32_t));
    canvas_init(&h.canvas, h.pixels, APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT);
    h.app = app_create(capture_clipboard);
    CHECK(h.app != NULL);
    frame(&h);
    CHECK(app_move_count(h.app) == 0);

    /* 1. e4 by clicking source then destination. */
    click_square(&h, "e2");
    click_square(&h, "e4");
    CHECK(app_move_count(h.app) == 1);
    CHECK(app_is_thinking(h.app));

    app_wait_for_engine(h.app);
    frame(&h);
    CHECK(app_move_count(h.app) == 2);
    CHECK(!app_is_thinking(h.app));

    /* Copy State / Copy Game buttons at their layout positions. */
    g_clipboard[0] = '\0';
    click(&h, 40.0f, 428.0f); /* "Copy State" */
    {
        /* Round-trips through the FEN parser and matches the side to move. */
        Position parsed;
        CHECK(position_from_fen(&parsed, g_clipboard));
        CHECK(parsed.side_to_move == COLOR_WHITE);
        CHECK(parsed.fullmove_number == 2);
        CHECK(PIECE_KIND(position_piece_at(&parsed, square("e4"))) == KIND_PAWN);
    }
    g_clipboard[0] = '\0';
    click(&h, 145.0f, 428.0f); /* "Copy Game" */
    CHECK(strncmp(g_clipboard, "[Event \"Chessplate\"]\n[Site \"Local\"]\n[Result \"*\"]\n\n1. e4 ", 52) == 0);

    /* Pixels were actually painted: panel background and board squares. */
    CHECK((h.pixels[5 * APP_WINDOW_WIDTH + 5] & 0xffffff) == 0x1b1b1b);

    app_destroy(h.app);
    free(h.pixels);
}

int main(void) {
    struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        {"start_position_has_20_legal_moves", test_start_position_has_20_legal_moves},
        {"castling_rights_clear_after_king_move", test_castling_rights_clear_after_king_move},
        {"en_passant_capture_works", test_en_passant_capture_works},
        {"fools_mate_is_checkmate", test_fools_mate_is_checkmate},
        {"start_position_fen_is_correct", test_start_position_fen_is_correct},
        {"san_notation_for_opening_and_mate", test_san_notation_for_opening_and_mate},
        {"perft_reference_counts", test_perft_reference_counts},
        {"fen_round_trip", test_fen_round_trip},
        {"castling_and_promotion_san", test_castling_and_promotion_san},
        {"stalemate_and_fifty_move", test_stalemate_and_fifty_move},
        {"engine_profiles", test_engine_profiles},
        {"engine_finds_mate_in_one", test_engine_finds_mate_in_one},
        {"engine_no_moves_and_cancel", test_engine_no_moves_and_cancel},
        {"evaluation_symmetry", test_evaluation_symmetry},
        {"phase_names", test_phase_names},
        {"metrics", test_metrics},
        {"themes", test_themes},
        {"svg_rendering", test_svg_rendering},
        {"app_headless", test_app_headless},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    for (int i = 0; i < n; i++) {
        int before = g_failures;
        tests[i].fn();
        printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d tests, %d checks, %d failures\n", n, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
