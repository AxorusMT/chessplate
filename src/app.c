#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "chess.h"
#include "engine.h"
#include "insight.h"
#include "metrics.h"
#include "svg.h"
#include "sys.h"
#include "text.h"
#include "theme.h"
#include "ui.h"

#define SEARCH_SNAPSHOT_LIMIT 256
#define COPY_FEEDBACK_MS 1800
#define MAX_ARROWS 128
#define PANEL_WIDTH 244.0f
#define PANEL_MARGIN 8.0f
#define JOB_QUEUE 32

enum {
    ID_NEW_GAME = 1,
    ID_PLAY_WHITE,
    ID_PLAY_BLACK,
    ID_COPY_STATE,
    ID_COPY_GAME,
    ID_CLEAR_PREMOVE,
    ID_ELO_SLIDER,
    ID_THEME_COMBO,
    ID_MOVE_SCROLL,
};

/* ------------------------------------------------------------------------ */
/* Engine job shared with the search thread                                 */
/* ------------------------------------------------------------------------ */

typedef struct {
    SysMutex *mutex;
    int refs;
    Position position;
    int elo;
    volatile int cancel;
    SearchSnapshot queue[JOB_QUEUE];
    int queue_count;
    bool done;
    bool has_move;
    Move result;
} EngineJob;

static void job_release(EngineJob *job) {
    sys_mutex_lock(job->mutex);
    int refs = --job->refs;
    sys_mutex_unlock(job->mutex);
    if (refs == 0) {
        sys_mutex_destroy(job->mutex);
        free(job);
    }
}

static void job_on_snapshot(const SearchSnapshot *snapshot, void *user) {
    EngineJob *job = (EngineJob *)user;
    sys_mutex_lock(job->mutex);
    if (job->queue_count < JOB_QUEUE) job->queue[job->queue_count++] = *snapshot;
    sys_mutex_unlock(job->mutex);
}

static void job_run(void *arg) {
    EngineJob *job = (EngineJob *)arg;
    Move mv = move_new(0, 0);
    bool ok = engine_best_move(&job->position, job->elo, job_on_snapshot, job, &job->cancel, &mv);
    sys_mutex_lock(job->mutex);
    job->done = true;
    job->has_move = ok;
    job->result = mv;
    sys_mutex_unlock(job->mutex);
    job_release(job);
}

/* ------------------------------------------------------------------------ */
/* Application state                                                        */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint8_t from, to;
} DrawnArrow;

struct App {
    Position position;
    int selected_square; /* -1 = none */
    MoveList selected_moves;
    int theme_index;
    Color human_color;
    int bot_elo;
    GameStatus status;

    char (*move_log)[6];
    Move *move_history;
    int move_count, move_cap;

    bool thinking;
    EngineJob *job;

    Image pieces[2][7];
    int piece_size;

    DrawnArrow arrows[MAX_ARROWS];
    int arrow_count;
    int arrow_drag_start, arrow_drag_current; /* -1 = none */

    SearchSnapshot snapshots[SEARCH_SNAPSHOT_LIMIT];
    int snapshot_count;
    Position search_root;
    bool has_search_root;

    char copy_feedback[64];
    uint64_t copy_feedback_at;
    bool has_copy_feedback;

    Move queued_premove;
    bool has_premove;

    /* UI state */
    Ui ui;
    ClipboardFn set_clipboard;
    int board_press_square; /* square under the left press, -1 = none */
    float theme_scroll;
    float move_scroll;
    bool move_scroll_stick;
    Rect theme_combo_rect;
};

static Color bot_color(const App *app) { return color_opposite(app->human_color); }
static const BoardTheme *current_theme(const App *app) { return theme_get(app->theme_index); }
static bool is_human_turn(const App *app) { return app->position.side_to_move == app->human_color; }
static bool is_game_over(const App *app) { return app->status.kind != STATUS_ONGOING; }
static void sync_status(App *app) { app->status = position_game_status(&app->position); }

static void clear_selection(App *app) {
    app->selected_square = -1;
    app->selected_moves.count = 0;
}

static void clear_arrows(App *app) {
    app->arrow_count = 0;
    app->arrow_drag_start = -1;
    app->arrow_drag_current = -1;
}

static void set_copy_feedback(App *app, const char *message) {
    snprintf(app->copy_feedback, sizeof(app->copy_feedback), "%s", message);
    app->copy_feedback_at = sys_time_ms();
    app->has_copy_feedback = true;
}

static void abandon_job(App *app) {
    if (app->job) {
        app->job->cancel = 1;
        job_release(app->job);
        app->job = NULL;
    }
}

static void new_game(App *app) {
    position_startpos(&app->position);
    clear_selection(app);
    app->move_count = 0;
    app->thinking = false;
    abandon_job(app);
    clear_arrows(app);
    app->snapshot_count = 0;
    app->has_search_root = false;
    app->has_copy_feedback = false;
    app->has_premove = false;
    app->move_scroll = 0;
    app->move_scroll_stick = true;
    sync_status(app);
}

App *app_create(ClipboardFn set_clipboard) {
    App *app = (App *)calloc(1, sizeof(App));
    if (!app) return NULL;
    app->theme_index = 0;
    app->human_color = COLOR_WHITE;
    app->bot_elo = 1600;
    app->set_clipboard = set_clipboard;
    app->board_press_square = -1;
    new_game(app);
    return app;
}

void app_destroy(App *app) {
    if (!app) return;
    abandon_job(app);
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < 7; k++) image_free(&app->pieces[c][k]);
    free(app->move_log);
    free(app->move_history);
    free(app);
}

static void push_history(App *app, Move mv) {
    if (app->move_count == app->move_cap) {
        int cap = app->move_cap ? app->move_cap * 2 : 128;
        char(*log)[6] = (char(*)[6])realloc(app->move_log, (size_t)cap * sizeof(*log));
        Move *hist = (Move *)realloc(app->move_history, (size_t)cap * sizeof(Move));
        if (log) app->move_log = log;
        if (hist) app->move_history = hist;
        if (!log || !hist) return;
        app->move_cap = cap;
    }
    move_to_coord(mv, app->move_log[app->move_count]);
    app->move_history[app->move_count] = mv;
    app->move_count++;
}

static void apply_move(App *app, Move mv) {
    position_make_move(&app->position, mv);
    push_history(app, mv);
    clear_selection(app);
    sync_status(app);
}

static Move preferred_move(const Move *candidates, int count) {
    for (int i = 0; i < count; i++) {
        if (candidates[i].promotion == KIND_QUEEN) return candidates[i];
    }
    return candidates[0];
}

static bool try_execute_premove(App *app) {
    if (!app->has_premove) return false;
    Move premove = app->queued_premove;
    app->has_premove = false;
    clear_selection(app);

    if (is_game_over(app) || !is_human_turn(app)) return false;

    MoveList legal;
    position_legal_moves(&app->position, &legal);
    Move candidates[MAX_MOVES];
    int n = 0;
    for (int i = 0; i < legal.count; i++) {
        if (legal.moves[i].from == premove.from && legal.moves[i].to == premove.to) candidates[n++] = legal.moves[i];
    }
    if (n == 0) return false;

    Move chosen = preferred_move(candidates, n);
    if (premove.promotion != KIND_NONE) {
        for (int i = 0; i < n; i++) {
            if (candidates[i].promotion == premove.promotion) {
                chosen = candidates[i];
                break;
            }
        }
    }
    apply_move(app, chosen);
    set_copy_feedback(app, "Premove played");
    return true;
}

static void push_snapshot(App *app, const SearchSnapshot *snap) {
    if (app->snapshot_count == SEARCH_SNAPSHOT_LIMIT) {
        memmove(app->snapshots, app->snapshots + 1, sizeof(SearchSnapshot) * (SEARCH_SNAPSHOT_LIMIT - 1));
        app->snapshot_count--;
    }
    app->snapshots[app->snapshot_count++] = *snap;
}

static void poll_engine_result(App *app) {
    if (!app->thinking) return;
    if (!app->job) {
        app->thinking = false;
        return;
    }

    EngineJob *job = app->job;
    SearchSnapshot pending[JOB_QUEUE];
    int pending_count;
    bool done, has_move;
    Move result;
    sys_mutex_lock(job->mutex);
    pending_count = job->queue_count;
    memcpy(pending, job->queue, sizeof(SearchSnapshot) * (size_t)pending_count);
    job->queue_count = 0;
    done = job->done;
    has_move = job->has_move;
    result = job->result;
    sys_mutex_unlock(job->mutex);

    for (int i = 0; i < pending_count; i++) push_snapshot(app, &pending[i]);

    if (done) {
        job_release(job);
        app->job = NULL;
        app->thinking = false;
        if (has_move) {
            apply_move(app, result);
            try_execute_premove(app);
        } else {
            sync_status(app);
        }
    }
}

static void maybe_start_engine_turn(App *app) {
    if (app->thinking || is_game_over(app) || is_human_turn(app)) return;

    EngineJob *job = (EngineJob *)calloc(1, sizeof(EngineJob));
    if (!job) return;
    job->mutex = sys_mutex_create();
    if (!job->mutex) {
        free(job);
        return;
    }
    job->refs = 2; /* app + worker */
    job->position = app->position;
    job->elo = app->bot_elo;

    app->search_root = app->position;
    app->has_search_root = true;
    app->snapshot_count = 0;
    app->job = job;
    app->thinking = true;

    if (sys_thread_start(job_run, job) != 0) {
        /* No threads available: search synchronously. */
        job_run(job);
    }
}

static const char *game_result_tag(const App *app) {
    switch (app->status.kind) {
    case STATUS_ONGOING: return "*";
    case STATUS_CHECKMATE: return app->status.winner == COLOR_WHITE ? "1-0" : "0-1";
    default: return "1/2-1/2";
    }
}

static char *game_to_pgn(const App *app) {
    const char *result = game_result_tag(app);
    size_t cap = (size_t)app->move_count * 24 + 128;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    int n = snprintf(out, cap, "[Event \"Chessplate\"]\n[Site \"Local\"]\n[Result \"%s\"]\n\n", result);
    size_t header_len = (size_t)n;

    Position replay;
    position_startpos(&replay);
    for (int i = 0; i < app->move_count; i++) {
        char san[16];
        move_to_san(&replay, app->move_history[i], san);
        if (i % 2 == 0) {
            n += snprintf(out + n, cap - (size_t)n, "%s%d. %s", (size_t)n > header_len ? " " : "", i / 2 + 1, san);
        } else {
            n += snprintf(out + n, cap - (size_t)n, " %s", san);
        }
        position_make_move(&replay, app->move_history[i]);
    }
    snprintf(out + n, cap - (size_t)n, "%s%s", (size_t)n > header_len ? " " : "", result);
    return out;
}

static void set_selection_from_legal(App *app, int square, const MoveList *legal) {
    app->selected_square = square;
    app->selected_moves.count = 0;
    for (int i = 0; i < legal->count; i++) {
        if (legal->moves[i].from == square) app->selected_moves.moves[app->selected_moves.count++] = legal->moves[i];
    }
    if (app->selected_moves.count == 0) clear_selection(app);
}

static void set_selection(App *app, int square) {
    MoveList legal;
    position_legal_moves(&app->position, &legal);
    set_selection_from_legal(app, square, &legal);
}

static void set_premove_selection(App *app, int square) {
    Position preview = app->position;
    preview.side_to_move = app->human_color;
    MoveList legal;
    position_legal_moves(&preview, &legal);
    set_selection_from_legal(app, square, &legal);
}

static int selected_candidates(const App *app, int from, int to, Move *out) {
    int n = 0;
    for (int i = 0; i < app->selected_moves.count; i++) {
        Move m = app->selected_moves.moves[i];
        if (m.from == from && m.to == to) out[n++] = m;
    }
    return n;
}

static void handle_live_turn_click(App *app, int square) {
    if (app->thinking || is_game_over(app) || !is_human_turn(app)) return;

    if (app->selected_square >= 0) {
        if (app->selected_square == square) {
            clear_selection(app);
            return;
        }
        Move candidates[MAX_MOVES];
        int n = selected_candidates(app, app->selected_square, square, candidates);
        if (n > 0) {
            Move chosen = preferred_move(candidates, n);
            app->has_premove = false;
            apply_move(app, chosen);
            return;
        }
    }

    Piece piece = position_piece_at(&app->position, square);
    if (piece != PIECE_NONE && PIECE_COLOR(piece) == app->human_color) set_selection(app, square);
    else clear_selection(app);
}

static void handle_premove_click(App *app, int square) {
    if (is_game_over(app) || is_human_turn(app)) return;

    if (app->selected_square >= 0) {
        if (app->selected_square == square) {
            clear_selection(app);
            return;
        }
        Move candidates[MAX_MOVES];
        int n = selected_candidates(app, app->selected_square, square, candidates);
        if (n > 0) {
            Move chosen = preferred_move(candidates, n);
            if (app->has_premove && move_equal(app->queued_premove, chosen)) {
                app->has_premove = false;
                set_copy_feedback(app, "Premove cleared");
            } else {
                app->queued_premove = chosen;
                app->has_premove = true;
                set_copy_feedback(app, "Premove queued");
            }
            clear_selection(app);
            return;
        }
    }

    Piece piece = position_piece_at(&app->position, square);
    if (piece != PIECE_NONE && PIECE_COLOR(piece) == app->human_color) set_premove_selection(app, square);
    else clear_selection(app);
}

static void handle_square_click(App *app, int square) {
    if (is_human_turn(app)) handle_live_turn_click(app, square);
    else handle_premove_click(app, square);
}

static void status_text(const App *app, char *buf, size_t size) {
    switch (app->status.kind) {
    case STATUS_ONGOING:
        if (app->thinking) {
            snprintf(buf, size, "%s is thinking...", color_name(bot_color(app)));
        } else {
            Color side = app->position.side_to_move;
            snprintf(buf, size, "%s to move%s", color_name(side),
                     position_is_in_check(&app->position, side) ? " (check)" : "");
        }
        break;
    case STATUS_CHECKMATE: snprintf(buf, size, "Checkmate. %s wins.", color_name(app->status.winner)); break;
    case STATUS_STALEMATE: snprintf(buf, size, "Stalemate. Draw."); break;
    case STATUS_DRAW_FIFTY_MOVE: snprintf(buf, size, "Draw by 50-move rule."); break;
    }
}

/* ------------------------------------------------------------------------ */
/* Board geometry                                                           */
/* ------------------------------------------------------------------------ */

static int square_from_board_grid(const App *app, int column, int row) {
    int file = app->human_color == COLOR_WHITE ? column : 7 - column;
    int rank = app->human_color == COLOR_WHITE ? 7 - row : row;
    return rank * 8 + file;
}

static void board_grid_from_square(const App *app, int square, int *column, int *row) {
    int file = square % 8, rank = square / 8;
    *column = app->human_color == COLOR_WHITE ? file : 7 - file;
    *row = app->human_color == COLOR_WHITE ? 7 - rank : rank;
}

static int square_from_pointer(const App *app, float x, float y, Rect board, float square_size) {
    if (!rect_contains(board, x, y)) return -1;
    int column = (int)floorf((x - board.x0) / square_size);
    int row = (int)floorf((y - board.y0) / square_size);
    if (column < 0 || column > 7 || row < 0 || row > 7) return -1;
    return square_from_board_grid(app, column, row);
}

static Vec2 square_center(const App *app, int square, Rect board, float square_size) {
    int column, row;
    board_grid_from_square(app, square, &column, &row);
    return vec2(board.x0 + ((float)column + 0.5f) * square_size, board.y0 + ((float)row + 0.5f) * square_size);
}

static void toggle_arrow(App *app, int from, int to) {
    if (from == to) return;
    for (int i = 0; i < app->arrow_count; i++) {
        if (app->arrows[i].from == from && app->arrows[i].to == to) {
            memmove(&app->arrows[i], &app->arrows[i + 1], sizeof(DrawnArrow) * (size_t)(app->arrow_count - i - 1));
            app->arrow_count--;
            return;
        }
    }
    if (app->arrow_count < MAX_ARROWS) {
        app->arrows[app->arrow_count].from = (uint8_t)from;
        app->arrows[app->arrow_count].to = (uint8_t)to;
        app->arrow_count++;
    }
}

static void handle_arrow_input(App *app, const InputState *in, Rect board, float square_size) {
    bool pointer_ok = in->mouse_inside && !app->ui.blocked;
    if (in->pressed[MOUSE_RIGHT]) {
        int start = pointer_ok ? square_from_pointer(app, in->mouse_x, in->mouse_y, board, square_size) : -1;
        app->arrow_drag_start = start;
        app->arrow_drag_current = start;
    }
    if (app->arrow_drag_start < 0) return;

    if (in->down[MOUSE_RIGHT] && in->mouse_inside) {
        int current = square_from_pointer(app, in->mouse_x, in->mouse_y, board, square_size);
        if (current >= 0) app->arrow_drag_current = current;
    }
    if (in->released[MOUSE_RIGHT]) {
        if (app->arrow_drag_start >= 0 && app->arrow_drag_current >= 0)
            toggle_arrow(app, app->arrow_drag_start, app->arrow_drag_current);
        app->arrow_drag_start = -1;
        app->arrow_drag_current = -1;
    }
}

static void draw_arrow(Canvas *c, Vec2 from, Vec2 to, Rgba color, float thickness) {
    float dx = to.x - from.x, dy = to.y - from.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 2.0f) return;
    float ux = dx / length, uy = dy / length;
    float px = -uy, py = ux;

    Vec2 start = vec2(from.x + ux * thickness * 0.6f, from.y + uy * thickness * 0.6f);
    Vec2 tip = vec2(to.x - ux * thickness * 0.2f, to.y - uy * thickness * 0.2f);
    Vec2 shaft_end = vec2(tip.x - ux * thickness * 2.0f, tip.y - uy * thickness * 2.0f);
    draw_line(c, start.x, start.y, shaft_end.x, shaft_end.y, thickness, color);

    Vec2 head_back = vec2(tip.x - ux * thickness * 2.3f, tip.y - uy * thickness * 2.3f);
    float wing = thickness * 1.2f;
    Vec2 head[3] = {tip, vec2(head_back.x + px * wing, head_back.y + py * wing),
                    vec2(head_back.x - px * wing, head_back.y - py * wing)};
    draw_polygon(c, head, 3, color);
}

static const char *const PIECE_SVGS[2][7] = {
    {NULL, asset_wP_svg, asset_wN_svg, asset_wB_svg, asset_wR_svg, asset_wQ_svg, asset_wK_svg},
    {NULL, asset_bP_svg, asset_bN_svg, asset_bB_svg, asset_bR_svg, asset_bQ_svg, asset_bK_svg},
};

static unsigned long piece_svg_len(int color, int kind) {
    static const unsigned long *const lens[2][7] = {
        {NULL, &asset_wP_svg_len, &asset_wN_svg_len, &asset_wB_svg_len, &asset_wR_svg_len, &asset_wQ_svg_len,
         &asset_wK_svg_len},
        {NULL, &asset_bP_svg_len, &asset_bN_svg_len, &asset_bB_svg_len, &asset_bR_svg_len, &asset_bQ_svg_len,
         &asset_bK_svg_len},
    };
    return *lens[color][kind];
}

static void ensure_piece_images(App *app, int size) {
    if (app->piece_size == size) return;
    for (int c = 0; c < 2; c++) {
        for (int k = 1; k < 7; k++) {
            image_free(&app->pieces[c][k]);
            svg_render(PIECE_SVGS[c][k], piece_svg_len(c, k), size, size, &app->pieces[c][k]);
        }
    }
    app->piece_size = size;
}

static void paint_premove_overlay(App *app, Canvas *c, Rect board, float square_size, const ThemePalette *pal) {
    if (!app->has_premove) return;
    Vec2 from = square_center(app, app->queued_premove.from, board, square_size);
    Vec2 to = square_center(app, app->queued_premove.to, board, square_size);
    draw_arrow(c, from, to, rgba_scale_alpha(pal->arrow_preview, 0.85f), 4.0f);

    float marker = fmaxf(square_size - 8.0f, 12.0f);
    Rgba stroke = rgba_scale_alpha(pal->arrow_preview, 0.95f);
    int squares[2] = {app->queued_premove.from, app->queued_premove.to};
    for (int i = 0; i < 2; i++) {
        Vec2 center = square_center(app, squares[i], board, square_size);
        draw_round_rect_stroke(c, center.x - marker * 0.5f, center.y - marker * 0.5f, center.x + marker * 0.5f,
                               center.y + marker * 0.5f, 6.0f, 2.0f, stroke);
    }
}

static void paint_arrows(App *app, Canvas *c, Rect board, float square_size, const ThemePalette *pal) {
    for (int i = 0; i < app->arrow_count; i++) {
        Vec2 from = square_center(app, app->arrows[i].from, board, square_size);
        Vec2 to = square_center(app, app->arrows[i].to, board, square_size);
        draw_arrow(c, from, to, pal->arrow, 6.0f);
    }
    if (app->arrow_drag_start >= 0 && app->arrow_drag_current >= 0) {
        Vec2 from = square_center(app, app->arrow_drag_start, board, square_size);
        Vec2 to = square_center(app, app->arrow_drag_current, board, square_size);
        draw_arrow(c, from, to, pal->arrow_preview, 5.0f);
    }
}

static void draw_board(App *app, Canvas *c, InputState *in, Rect board, float square_size) {
    const ThemePalette *pal = &current_theme(app)->palette;
    int piece_px = (int)fmaxf(square_size - 10.0f, 30.0f);
    ensure_piece_images(app, piece_px);

    bool targets[64] = {false};
    for (int i = 0; i < app->selected_moves.count; i++) targets[app->selected_moves.moves[i].to] = true;

    /* Primary clicks: press and release on the same square. */
    bool pointer_ok = in->mouse_inside && !app->ui.blocked;
    int hover_square = pointer_ok ? square_from_pointer(app, in->mouse_x, in->mouse_y, board, square_size) : -1;
    if (in->pressed[MOUSE_LEFT]) app->board_press_square = hover_square;
    int clicked_square = -1;
    if (in->released[MOUSE_LEFT]) {
        if (app->board_press_square >= 0 && app->board_press_square == hover_square) clicked_square = hover_square;
        app->board_press_square = -1;
    }
    if (clicked_square >= 0) {
        handle_square_click(app, clicked_square);
        for (int i = 0; i < 64; i++) targets[i] = false;
        for (int i = 0; i < app->selected_moves.count; i++) targets[app->selected_moves.moves[i].to] = true;
    }

    for (int row = 0; row < 8; row++) {
        for (int column = 0; column < 8; column++) {
            int square = square_from_board_grid(app, column, row);
            bool is_light = (square % 8 + square / 8) % 2 == 0;
            Rgba fill = is_light ? pal->board_light : pal->board_dark;
            if (square == app->selected_square) fill = pal->selected;
            else if (targets[square]) fill = pal->target;

            float x0 = board.x0 + (float)column * square_size, y0 = board.y0 + (float)row * square_size;
            draw_rect(c, x0, y0, x0 + square_size, y0 + square_size, fill);

            Piece piece = position_piece_at(&app->position, square);
            if (piece != PIECE_NONE) {
                const Image *img = &app->pieces[PIECE_COLOR(piece)][PIECE_KIND(piece)];
                if (img->pixels) {
                    int ix = (int)floorf(x0 + (square_size - (float)img->w) * 0.5f + 0.5f);
                    int iy = (int)floorf(y0 + (square_size - (float)img->h) * 0.5f + 0.5f);
                    draw_image(c, img, ix, iy);
                } else {
                    static const char letters[] = " PNBRQK";
                    char text[2] = {letters[PIECE_KIND(piece)], 0};
                    draw_text(c, FONT_BOLD_20, x0 + square_size * 0.5f, y0 + square_size * 0.5f, ALIGN_CENTER_CENTER,
                              text, PIECE_COLOR(piece) == COLOR_WHITE ? rgb(255, 255, 255) : rgb(0, 0, 0));
                }
            }
        }
    }

    handle_arrow_input(app, in, board, square_size);
    int cx0 = c->clip_x0, cy0 = c->clip_y0, cx1 = c->clip_x1, cy1 = c->clip_y1;
    canvas_set_clip(c, board.x0, board.y0, board.x1, board.y1);
    paint_premove_overlay(app, c, board, square_size, pal);
    paint_arrows(app, c, board, square_size, pal);
    c->clip_x0 = cx0, c->clip_y0 = cy0, c->clip_x1 = cx1, c->clip_y1 = cy1;
}

/* ------------------------------------------------------------------------ */
/* Search Prism                                                             */
/* ------------------------------------------------------------------------ */

static void paint_search_chart(App *app, Canvas *c, Rect r, const ThemePalette *pal) {
    draw_round_rect(c, r.x0, r.y0, r.x1, r.y1, 8.0f, rgba(8, 10, 16, 40));

    float zero_y = (r.y0 + r.y1) * 0.5f;
    draw_line(c, r.x0, zero_y, r.x1, zero_y, 1.0f, pal->viz_grid);
    for (int idx = 1; idx < 8; idx++) {
        float x = r.x0 + rect_w(r) * (float)idx / 8.0f;
        draw_line(c, x, r.y0, x, r.y1, 1.0f, pal->viz_grid);
    }

    if (app->snapshot_count == 0) {
        draw_text(c, FONT_MONO_14, (r.x0 + r.x1) * 0.5f, zero_y, ALIGN_CENTER_CENTER,
                  app->thinking ? "Engine warming up..." : "No active search", pal->viz_text_muted);
        return;
    }

    int take = app->snapshot_count < 96 ? app->snapshot_count : 96;
    const SearchSnapshot *history = app->snapshots + (app->snapshot_count - take);
    int max_depth = 1;
    for (int i = 0; i < take; i++)
        if (history[i].depth > max_depth) max_depth = history[i].depth;

    Vec2 points[96];
    for (int i = 0; i < take; i++) {
        float x = take == 1 ? (r.x0 + r.x1) * 0.5f : r.x0 + rect_w(r) * ((float)i / ((float)take - 1.0f));
        float depth_h = ((float)history[i].depth / (float)max_depth) * (rect_h(r) * 0.36f);
        draw_round_rect(c, x - 1.5f, r.y1 - depth_h, x + 1.5f, r.y1, 1.0f, pal->viz_depth);

        int score = history[i].best_score_cp;
        if (score < -600) score = -600;
        if (score > 600) score = 600;
        float y = zero_y - ((float)score / 600.0f) * (rect_h(r) * 0.42f);
        points[i] = vec2(x, y);
    }

    if (take >= 2) draw_polyline(c, points, take, 6.0f, pal->viz_eval_glow);
    Rgba trend = history[take - 1].best_score_cp >= 0 ? pal->viz_eval_pos : pal->viz_eval_neg;
    draw_polyline(c, points, take, 2.2f, trend);

    Vec2 last = points[take - 1];
    draw_circle(c, last.x, last.y, 4.5f, trend);
    draw_circle_stroke(c, last.x, last.y, 7.5f, 1.2f, pal->viz_eval_glow);
}

static void paint_search_cards(App *app, Canvas *c, Rect r, const ThemePalette *pal) {
    if (app->snapshot_count == 0) {
        draw_text(c, FONT_MONO_12, (r.x0 + r.x1) * 0.5f, (r.y0 + r.y1) * 0.5f, ALIGN_CENTER_CENTER,
                  "Waiting for search data", pal->viz_text_muted);
        return;
    }
    const SearchSnapshot *latest = &app->snapshots[app->snapshot_count - 1];

    uint64_t elapsed = latest->elapsed_ms > 0 ? latest->elapsed_ms : 1;
    uint64_t nps = latest->nodes * 1000u / elapsed;
    float eval_pawns = (float)latest->best_score_cp / 100.0f;
    float stability = best_move_stability(app->snapshots, app->snapshot_count);
    float volatility = eval_volatility_cp(app->snapshots, app->snapshot_count);
    int gap_cp = latest->top_count > 1 ? latest->best_score_cp - latest->top_moves[1].score_cp : 0;

    float left = r.x0;
    float y = r.y0 + 2.0f;
    const float line_h = 15.0f;

    draw_text(c, FONT_SANS_16, left, y, ALIGN_LEFT_TOP, "Search Prism", pal->viz_text_main);
    y += line_h + 2.0f;

    char nodes_text[32], nps_text[32], lines[7][64];
    format_big_number(latest->nodes, nodes_text, sizeof(nodes_text));
    format_big_number(nps, nps_text, sizeof(nps_text));
    snprintf(lines[0], sizeof(lines[0]), "Depth: %d", latest->depth);
    snprintf(lines[1], sizeof(lines[1]), "Nodes: %s", nodes_text);
    snprintf(lines[2], sizeof(lines[2]), "NPS: %s", nps_text);
    snprintf(lines[3], sizeof(lines[3]), "Score: %+.2f pawns", eval_pawns);
    snprintf(lines[4], sizeof(lines[4]), "Stability: %.0f%%", stability * 100.0f);
    snprintf(lines[5], sizeof(lines[5]), "Volatility: %ld cp", lroundf(volatility));
    snprintf(lines[6], sizeof(lines[6]), "Lead gap: %d cp", gap_cp);
    for (int i = 0; i < 7; i++) {
        draw_text(c, FONT_MONO_12, left, y, ALIGN_LEFT_TOP, lines[i], pal->viz_text_muted);
        y += line_h;
    }

    y += 4.0f;
    draw_text(c, FONT_SANS_14, left, y, ALIGN_LEFT_TOP, "Top Candidates", pal->viz_text_main);
    y += line_h;

    int best_abs = latest->top_count > 0 ? abs(latest->top_moves[0].score_cp) : 1;
    if (best_abs < 1) best_abs = 1;

    int shown = latest->top_count < 3 ? latest->top_count : 3;
    for (int i = 0; i < shown; i++) {
        const ScoredMove *cand = &latest->top_moves[i];
        Rect card = rect_make(left, y, r.x1 - 2.0f, y + 18.0f);
        draw_round_rect(c, card.x0, card.y0, card.x1, card.y1, 6.0f, pal->viz_card);

        char move_text[16];
        if (app->has_search_root) move_to_san(&app->search_root, cand->mv, move_text);
        else move_to_coord(cand->mv, move_text);

        float ratio = (float)abs(cand->score_cp) / (float)best_abs;
        if (ratio > 1.0f) ratio = 1.0f;
        float meter_w = (rect_w(card) - 72.0f) * ratio;
        if (meter_w > 0)
            draw_round_rect(c, card.x1 - meter_w - 4.0f, card.y0 + 13.0f, card.x1 - 4.0f, card.y1 - 4.0f, 1.0f,
                            cand->score_cp >= 0 ? pal->viz_eval_pos : pal->viz_eval_neg);

        char label[48];
        snprintf(label, sizeof(label), "%s  (%+dcp)", move_text, cand->score_cp);
        draw_text(c, FONT_MONO_11, card.x0 + 6.0f, card.y0 + 1.0f, ALIGN_LEFT_TOP, label, pal->viz_text_main);

        y += 22.0f;
        if (y > r.y1 - 20.0f) break;
    }
}

static void draw_thinking_visualization(App *app, Canvas *c, Rect r) {
    const ThemePalette *pal = &current_theme(app)->palette;
    int cx0 = c->clip_x0, cy0 = c->clip_y0, cx1 = c->clip_x1, cy1 = c->clip_y1;
    canvas_set_clip(c, r.x0, r.y0, r.x1, r.y1);
    draw_round_rect(c, r.x0, r.y0, r.x1, r.y1, 12.0f, pal->viz_bg);

    Rect inner = rect_make(r.x0 + 10, r.y0 + 10, r.x1 - 10, r.y1 - 10);
    float chart_h = rect_h(inner) * 0.54f;
    Rect chart = rect_make(inner.x0, inner.y0, inner.x1, inner.y0 + chart_h);
    Rect cards = rect_make(inner.x0, chart.y1 + 8.0f, inner.x1, inner.y1);
    paint_search_chart(app, c, chart, pal);
    paint_search_cards(app, c, cards, pal);
    c->clip_x0 = cx0, c->clip_y0 = cy0, c->clip_x1 = cx1, c->clip_y1 = cy1;
}

/* ------------------------------------------------------------------------ */
/* Side panel                                                               */
/* ------------------------------------------------------------------------ */

static const char *theme_item_text(int index, void *user) {
    (void)user;
    return theme_get(index)->name;
}

static float draw_controls(App *app, Canvas *c, float x, float y, float w, float bottom) {
    Ui *ui = &app->ui;
    const float sp = UI_ITEM_SPACING;
    char buf[160];

    y = ui_label(ui, x, y, FONT_BOLD_20, "Chessplate", UI_TEXT_STRONG) + sp;
    y = ui_label_wrapped(ui, x, y, w, FONT_SANS_13, "Play against a local C engine.", UI_TEXT) + sp;
    ui_separator(ui, x, y + 2, w);
    y += 6 + sp;

    status_text(app, buf, sizeof(buf));
    y = ui_label_wrapped(ui, x, y, w, FONT_SANS_13, buf, UI_TEXT_STRONG) + sp;

    /* Game phases group */
    y += 8;
    PhaseNames names = phase_names(&app->position, (const char(*)[6])app->move_log, app->move_count);
    float gx = x + 7, gw = w - 14, gy = y + 6;
    gy = ui_label(ui, gx, gy, FONT_SANS_13, "Game Phases", UI_TEXT_STRONG) + sp;
    snprintf(buf, sizeof(buf), "Current: %s", names.current_phase);
    gy = ui_label_wrapped(ui, gx, gy, gw, FONT_SANS_13, buf, UI_TEXT) + sp;
    ui_separator(ui, gx, gy + 1, gw);
    gy += 4 + sp;
    snprintf(buf, sizeof(buf), "Opening: %s", names.opening_name);
    gy = ui_label_wrapped(ui, gx, gy, gw, FONT_SANS_13, buf, UI_TEXT) + sp;
    snprintf(buf, sizeof(buf), "Middlegame: %s", names.middlegame_name);
    gy = ui_label_wrapped(ui, gx, gy, gw, FONT_SANS_13, buf, UI_TEXT) + sp;
    snprintf(buf, sizeof(buf), "Endgame: %s", names.endgame_name);
    gy = ui_label_wrapped(ui, gx, gy, gw, FONT_SANS_13, buf, UI_TEXT);
    gy += 6;
    draw_round_rect_stroke(c, x + 0.5f, y + 0.5f, x + w - 0.5f, gy - 0.5f, 3.0f, 1.0f, UI_STROKE);
    y = gy + sp;

    /* Theme combo box (popup drawn at the end of the frame) */
    y += 8;
    float combo_w = 136.0f;
    app->theme_combo_rect = rect_make(x, y, x + combo_w, y + 22);
    if (ui_combo_header(ui, ID_THEME_COMBO, app->theme_combo_rect, current_theme(app)->name) &&
        ui->popup_id == ID_THEME_COMBO) {
        /* Scroll so the selected entry is visible when opening. */
        app->theme_scroll = (float)app->theme_index * 20.0f - 100.0f;
    }
    ui_label(ui, x + combo_w + 8, y + 4, FONT_SANS_13, "Color Theme", UI_TEXT);
    y += 22 + sp;

    /* Elo slider */
    y += 8;
    float slider_w = 112.0f;
    ui_slider_int(ui, ID_ELO_SLIDER, rect_make(x, y, x + slider_w, y + 20), &app->bot_elo, ENGINE_MIN_ELO,
                  ENGINE_MAX_ELO);
    Rect value_box = rect_make(x + slider_w + 6, y, x + slider_w + 50, y + 20);
    draw_round_rect(c, value_box.x0, value_box.y0, value_box.x1, value_box.y1, 3.0f, UI_WIDGET);
    snprintf(buf, sizeof(buf), "%d", app->bot_elo);
    draw_text(c, FONT_SANS_13, (value_box.x0 + value_box.x1) * 0.5f, (value_box.y0 + value_box.y1) * 0.5f,
              ALIGN_CENTER_CENTER, buf, rgb(210, 210, 210));
    ui_label(ui, value_box.x1 + 8, y + 3, FONT_SANS_13, "Bot Elo", UI_TEXT);
    y += 20 + sp;

    EloProfile profile = profile_for_elo(app->bot_elo);
    snprintf(buf, sizeof(buf), "Depth %d | %llu ms | %llu nodes", profile.depth, (unsigned long long)profile.time_ms,
             (unsigned long long)profile.node_limit);
    y = ui_label_wrapped(ui, x, y, w, FONT_SANS_13, buf, UI_TEXT) + sp;
    if (profile.randomness_cp > 0) snprintf(buf, sizeof(buf), "Error model: +/-%d centipawns", profile.randomness_cp);
    else snprintf(buf, sizeof(buf), "Error model: off");
    y = ui_label_wrapped(ui, x, y, w, FONT_SANS_13, buf, UI_TEXT) + sp;

    /* Game buttons */
    y += 10;
    const float bh = 22.0f;
    if (ui_button(ui, ID_NEW_GAME, rect_make(x, y, x + ui_button_width(FONT_SANS_13, "New Game"), y + bh),
                  FONT_SANS_13, "New Game"))
        new_game(app);
    y += bh + sp;
    if (ui_button(ui, ID_PLAY_WHITE, rect_make(x, y, x + ui_button_width(FONT_SANS_13, "Play as White"), y + bh),
                  FONT_SANS_13, "Play as White")) {
        app->human_color = COLOR_WHITE;
        new_game(app);
    }
    y += bh + sp;
    if (ui_button(ui, ID_PLAY_BLACK, rect_make(x, y, x + ui_button_width(FONT_SANS_13, "Play as Black"), y + bh),
                  FONT_SANS_13, "Play as Black")) {
        app->human_color = COLOR_BLACK;
        new_game(app);
    }
    y += bh + sp;

    float cw1 = ui_button_width(FONT_SANS_13, "Copy State");
    float cw2 = ui_button_width(FONT_SANS_13, "Copy Game");
    if (ui_button(ui, ID_COPY_STATE, rect_make(x, y, x + cw1, y + bh), FONT_SANS_13, "Copy State")) {
        char fen[128];
        position_to_fen(&app->position, fen, sizeof(fen));
        if (app->set_clipboard) app->set_clipboard(fen);
        set_copy_feedback(app, "Copied FEN state");
    }
    if (ui_button(ui, ID_COPY_GAME, rect_make(x + cw1 + 6, y, x + cw1 + 6 + cw2, y + bh), FONT_SANS_13,
                  "Copy Game")) {
        char *pgn = game_to_pgn(app);
        if (pgn) {
            if (app->set_clipboard) app->set_clipboard(pgn);
            free(pgn);
        }
        set_copy_feedback(app, "Copied PGN game");
    }
    y += bh + sp;

    if (app->has_copy_feedback) y = ui_label(ui, x, y, FONT_SANS_11, app->copy_feedback, UI_TEXT_WEAK) + sp;

    if (app->has_premove) {
        char coord[6];
        move_to_coord(app->queued_premove, coord);
        snprintf(buf, sizeof(buf), "Queued premove: %s", coord);
        float lw = text_width(FONT_SANS_13, buf);
        ui_label(ui, x, y + 2, FONT_SANS_13, buf, UI_TEXT);
        float bx = x + lw + 6;
        float bw = ui_button_width(FONT_SANS_11, "Clear") - 6;
        if (bx + bw > x + w) {
            bx = x;
            y += 20;
        }
        if (ui_button(ui, ID_CLEAR_PREMOVE, rect_make(bx, y, bx + bw, y + 19), FONT_SANS_11, "Clear")) {
            app->has_premove = false;
            clear_selection(app);
        }
        y += 19 + sp;
    } else if (!is_human_turn(app) && !is_game_over(app)) {
        y = ui_label_wrapped(ui, x, y, w, FONT_SANS_11, "Premove: queue your move while the engine thinks.",
                             UI_TEXT) +
            sp;
    }

    ui_separator(ui, x, y + 2, w);
    y += 6 + sp;
    y = ui_label(ui, x, y, FONT_SANS_13, "Moves", UI_TEXT) + sp;

    /* Move log scroll area */
    if (app->move_count == 0) {
        y = ui_label(ui, x, y, FONT_SANS_13, "No moves yet.", UI_TEXT) + sp;
        return y;
    }
    float row_h = (float)text_line_height(FONT_MONO_12) + 2.0f;
    float content_h = (float)app->move_count * row_h;
    float avail = bottom - y;
    float view_h = fminf(fminf(360.0f, content_h), fmaxf(avail, row_h));
    Rect view = rect_make(x, y, x + w, y + view_h);
    float max_scroll = content_h > view_h ? content_h - view_h : 0.0f;
    if (app->move_scroll_stick) app->move_scroll = max_scroll;
    app->move_scroll = ui_scroll_region(ui, ID_MOVE_SCROLL, view, content_h, app->move_scroll);
    app->move_scroll_stick = app->move_scroll >= max_scroll - 0.5f;

    int cx0 = c->clip_x0, cy0 = c->clip_y0, cx1 = c->clip_x1, cy1 = c->clip_y1;
    canvas_set_clip(c, view.x0, view.y0, view.x1 - 10, view.y1);
    int first = (int)(app->move_scroll / row_h);
    for (int i = first; i < app->move_count; i++) {
        float ry = view.y0 + (float)i * row_h - app->move_scroll;
        if (ry > view.y1) break;
        char prefix[16];
        if (i % 2 == 0) snprintf(prefix, sizeof(prefix), "%d.", i / 2 + 1);
        else snprintf(prefix, sizeof(prefix), "...");
        snprintf(buf, sizeof(buf), "%4s %s", prefix, app->move_log[i]);
        draw_text(c, FONT_MONO_12, x, ry, ALIGN_LEFT_TOP, buf, UI_TEXT);
    }
    c->clip_x0 = cx0, c->clip_y0 = cy0, c->clip_x1 = cx1, c->clip_y1 = cy1;
    return view.y1 + sp;
}

/* ------------------------------------------------------------------------ */
/* Frame                                                                    */
/* ------------------------------------------------------------------------ */

void app_frame(App *app, Canvas *c, InputState *in) {
    uint64_t now = sys_time_ms();
    if (app->has_copy_feedback && now - app->copy_feedback_at > COPY_FEEDBACK_MS) app->has_copy_feedback = false;

    if (in->key_escape || in->pressed[MOUSE_LEFT]) clear_arrows(app);

    poll_engine_result(app);
    maybe_start_engine_turn(app);

    ui_begin_frame(&app->ui, c, in);
    canvas_reset_clip(c);
    canvas_clear(c, UI_PANEL_FILL);

    /* Left control panel */
    draw_rect(c, PANEL_WIDTH, 0, PANEL_WIDTH + 1, (float)c->h, UI_STROKE);
    canvas_set_clip(c, 0, 0, PANEL_WIDTH, (float)c->h);
    draw_controls(app, c, PANEL_MARGIN, PANEL_MARGIN, PANEL_WIDTH - 2 * PANEL_MARGIN, (float)c->h - PANEL_MARGIN);
    canvas_reset_clip(c);

    /* Central panel: board + Search Prism */
    const float gap = 12.0f, max_square = 72.0f, min_square = 46.0f;
    const float desired_viz = 260.0f, min_viz = 150.0f;
    float left = PANEL_WIDTH + 1 + 8.0f, top = 8.0f;
    float total_w = (float)c->w - left - 8.0f;
    float total_h = (float)c->h - top - 8.0f;

    float square = max_square, viz_w = desired_viz;
    if (total_w < max_square * 8.0f + desired_viz + gap) {
        square = floorf((total_w - desired_viz - gap) / 8.0f);
        if (square < min_square) square = min_square;
        if (square > max_square) square = max_square;
        viz_w = fminf(fmaxf(total_w - square * 8.0f - gap, min_viz), desired_viz);
    }
    if (square * 8.0f > total_h) square = fmaxf(floorf(total_h / 8.0f), 24.0f);
    float board_size = square * 8.0f;

    Rect board = rect_make(left, top, left + board_size, top + board_size);
    draw_board(app, c, in, board, square);
    Rect viz = rect_make(board.x1 + gap, top, board.x1 + gap + viz_w, top + board_size);
    draw_thinking_visualization(app, c, viz);

    /* Popups go on top of everything. */
    if (app->ui.popup_id == ID_THEME_COMBO) {
        Rect cr = app->theme_combo_rect;
        float h = fminf(20.0f * (float)theme_count() + 6.0f, fmaxf((float)c->h - cr.y1 - 12.0f, 120.0f));
        h = fminf(h, 320.0f);
        Rect popup = rect_make(cr.x0, cr.y1 + 3, cr.x0 + 220.0f, cr.y1 + 3 + h);
        int picked = ui_list_popup(&app->ui, ID_THEME_COMBO, popup, theme_count(), 20.0f, app->theme_index,
                                   &app->theme_scroll, theme_item_text, NULL);
        if (picked >= 0) app->theme_index = picked;
    }

    ui_end_frame(&app->ui);

    /* A human move made this frame hands the turn to the engine right away. */
    maybe_start_engine_turn(app);
}

int app_wait_timeout_ms(const App *app) {
    if (app->thinking) return 30;
    if (app->has_copy_feedback) {
        uint64_t elapsed = sys_time_ms() - app->copy_feedback_at;
        if (elapsed >= COPY_FEEDBACK_MS) return 0;
        return (int)(COPY_FEEDBACK_MS - elapsed) + 5;
    }
    return -1;
}

bool app_render_icon(int size, Image *out) { return svg_render(asset_logo_svg, asset_logo_svg_len, size, size, out); }

bool app_is_thinking(const App *app) { return app->thinking; }
int app_move_count(const App *app) { return app->move_count; }

void app_wait_for_engine(App *app) {
    while (app->thinking && app->job) {
        sys_mutex_lock(app->job->mutex);
        bool done = app->job->done;
        sys_mutex_unlock(app->job->mutex);
        if (done) break;
        uint64_t until = sys_time_ms() + 5;
        while (sys_time_ms() < until) {
        }
    }
}
