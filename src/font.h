#ifndef CHESSPLATE_FONT_H
#define CHESSPLATE_FONT_H

/* Embedded anti-aliased bitmap fonts (printable ASCII 32..126). */

typedef enum {
    FONT_SANS_11,
    FONT_SANS_13,
    FONT_SANS_14,
    FONT_SANS_16,
    FONT_BOLD_20,
    FONT_MONO_11,
    FONT_MONO_12,
    FONT_MONO_14,
    FONT_COUNT
} FontId;

typedef struct {
    short x;            /* left bearing relative to pen position */
    short y;            /* top of bitmap relative to baseline (negative = above) */
    unsigned char w, h; /* bitmap size */
    float advance;      /* horizontal advance in pixels */
    unsigned int offset; /* offset into the font's bitmap array */
} GlyphInfo;

typedef struct {
    int ascent;
    int descent;
    const GlyphInfo *glyphs; /* indexed by (ch - 32) */
    const unsigned char *bitmap;
} FontData;

extern const FontData font_table[FONT_COUNT];

#endif
