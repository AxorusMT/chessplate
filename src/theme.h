#ifndef CHESSPLATE_THEME_H
#define CHESSPLATE_THEME_H

#include "raster.h"

typedef struct {
    Rgba board_light;
    Rgba board_dark;
    Rgba selected;
    Rgba target;
    Rgba arrow;
    Rgba arrow_preview;
    Rgba viz_bg;
    Rgba viz_grid;
    Rgba viz_depth;
    Rgba viz_eval_pos;
    Rgba viz_eval_neg;
    Rgba viz_eval_glow;
    Rgba viz_text_main;
    Rgba viz_text_muted;
    Rgba viz_card;
} ThemePalette;

typedef struct {
    char name[48];
    ThemePalette palette;
} BoardTheme;

/* The generated catalog: every seed combined with every variant. */
int theme_count(void);
const BoardTheme *theme_get(int index);

#endif
