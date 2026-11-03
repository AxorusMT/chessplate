#ifndef CHESSPLATE_UI_H
#define CHESSPLATE_UI_H

/* A small immediate-mode widget toolkit drawn with the software rasterizer.
 * Visual style follows a dark desktop theme. */

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "platform.h"
#include "raster.h"

typedef struct {
    float x0, y0, x1, y1;
} Rect;

static inline Rect rect_make(float x0, float y0, float x1, float y1) {
    Rect r;
    r.x0 = x0;
    r.y0 = y0;
    r.x1 = x1;
    r.y1 = y1;
    return r;
}
static inline float rect_w(Rect r) { return r.x1 - r.x0; }
static inline float rect_h(Rect r) { return r.y1 - r.y0; }
static inline bool rect_contains(Rect r, float x, float y) { return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

/* Palette */
#define UI_PANEL_FILL rgb(27, 27, 27)
#define UI_TEXT rgb(160, 160, 160)
#define UI_TEXT_STRONG rgb(240, 240, 240)
#define UI_TEXT_WEAK rgb(120, 120, 120)
#define UI_STROKE rgb(60, 60, 60)
#define UI_WIDGET rgb(60, 60, 60)
#define UI_WIDGET_HOVER rgb(72, 72, 72)
#define UI_WIDGET_ACTIVE rgb(52, 52, 52)
#define UI_WIDGET_STROKE_HOVER rgb(150, 150, 150)
#define UI_ACCENT rgb(90, 170, 255)
#define UI_EXTREME_BG rgb(10, 10, 10)

#define UI_ITEM_SPACING 4.0f

typedef struct {
    Canvas *canvas;
    InputState *in;
    uint32_t active_id; /* widget currently held by the left mouse button */
    uint32_t popup_id;  /* open popup (0 = none) */
    Rect popup_rect;    /* popup area from the previous frame */
    bool blocked;       /* input captured by an open popup */
} Ui;

void ui_begin_frame(Ui *ui, Canvas *canvas, InputState *in);
void ui_end_frame(Ui *ui);

/* True when the mouse is over r and no popup captures input. */
bool ui_hovered(const Ui *ui, Rect r);
/* Standard press/release click behavior. */
bool ui_click_behavior(Ui *ui, uint32_t id, Rect r, bool *hovered, bool *held);

/* Widgets; each returns its bottom y (or the clicked/changed state). */
float ui_label(Ui *ui, float x, float y, FontId font, const char *text, Rgba color);
float ui_label_wrapped(Ui *ui, float x, float y, float width, FontId font, const char *text, Rgba color);
void ui_separator(Ui *ui, float x, float y, float width);
float ui_button_width(FontId font, const char *label);
bool ui_button(Ui *ui, uint32_t id, Rect r, FontId font, const char *label);
/* Returns true while the value changed this frame. */
bool ui_slider_int(Ui *ui, uint32_t id, Rect track, int *value, int min, int max);
/* Draws a combo box header; returns true when clicked. */
bool ui_combo_header(Ui *ui, uint32_t id, Rect r, const char *text);

/* Scrollable list popup. Returns the clicked item index or -1. */
typedef const char *(*UiItemText)(int index, void *user);
int ui_list_popup(Ui *ui, uint32_t id, Rect r, int item_count, float item_h, int selected, float *scroll,
                  UiItemText text_fn, void *user);

/* Vertical scroll handling for a region; returns the clamped scroll offset. */
float ui_scroll_region(Ui *ui, uint32_t id, Rect view, float content_h, float scroll);

#endif
