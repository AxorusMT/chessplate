#ifndef CHESSPLATE_TEXT_H
#define CHESSPLATE_TEXT_H

#include "font.h"
#include "raster.h"

typedef enum {
    ALIGN_LEFT_TOP,
    ALIGN_CENTER_CENTER,
} TextAlign;

float text_width(FontId font, const char *text);
float text_width_n(FontId font, const char *text, int len);
int text_line_height(FontId font);
/* Draws a single line. (x, y) is the top-left of the line box for
 * ALIGN_LEFT_TOP or the box center for ALIGN_CENTER_CENTER. Returns the
 * advance width. */
float draw_text(Canvas *c, FontId font, float x, float y, TextAlign align, const char *text, Rgba color);

/* Word-wraps `text` to max_width; draws it when c is non-NULL. Returns the
 * total height used. */
float draw_text_wrapped(Canvas *c, FontId font, float x, float y, float max_width, const char *text, Rgba color);

#endif
