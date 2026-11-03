#include "text.h"

#include <math.h>
#include <string.h>

static const GlyphInfo *glyph_for(const FontData *f, unsigned char ch) {
    if (ch < 32 || ch > 126) ch = '?';
    return &f->glyphs[ch - 32];
}

float text_width_n(FontId font, const char *text, int len) {
    const FontData *f = &font_table[font];
    float w = 0;
    for (int i = 0; i < len && text[i]; i++) w += glyph_for(f, (unsigned char)text[i])->advance;
    return w;
}

float text_width(FontId font, const char *text) { return text_width_n(font, text, (int)strlen(text)); }

int text_line_height(FontId font) {
    const FontData *f = &font_table[font];
    return f->ascent + f->descent;
}

static void draw_glyph(Canvas *c, const FontData *f, const GlyphInfo *g, int px, int baseline, Rgba color) {
    int gx = px + g->x, gy = baseline + g->y;
    const unsigned char *bits = f->bitmap + g->offset;
    for (int row = 0; row < g->h; row++) {
        int y = gy + row;
        if (y < c->clip_y0 || y >= c->clip_y1) continue;
        uint32_t *dst = c->pixels + (size_t)y * (size_t)c->w;
        for (int col = 0; col < g->w; col++) {
            int x = gx + col;
            if (x < c->clip_x0 || x >= c->clip_x1) continue;
            unsigned cov = bits[row * g->w + col];
            if (!cov) continue;
            unsigned a = (cov * color.a + 127) / 255;
            if (!a) continue;
            uint32_t d = dst[x];
            unsigned inv = 255 - a;
            unsigned r = (color.r * a + ((d >> 16) & 0xff) * inv + 127) / 255;
            unsigned gg = (color.g * a + ((d >> 8) & 0xff) * inv + 127) / 255;
            unsigned b = (color.b * a + (d & 0xff) * inv + 127) / 255;
            unsigned da = (d >> 24);
            unsigned oa = a + (da * inv + 127) / 255;
            dst[x] = ((uint32_t)oa << 24) | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | b;
        }
    }
}

float draw_text(Canvas *c, FontId font, float x, float y, TextAlign align, const char *text, Rgba color) {
    const FontData *f = &font_table[font];
    float width = text_width(font, text);
    float left = x, top = y;
    if (align == ALIGN_CENTER_CENTER) {
        left = x - width * 0.5f;
        top = y - (float)(f->ascent + f->descent) * 0.5f;
    }
    int baseline = (int)floorf(top + 0.5f) + f->ascent;
    float pen = floorf(left + 0.5f);
    for (const char *p = text; *p; p++) {
        const GlyphInfo *g = glyph_for(f, (unsigned char)*p);
        if (c && g->w && g->h) draw_glyph(c, f, g, (int)floorf(pen + 0.5f), baseline, color);
        pen += g->advance;
    }
    return width;
}

float draw_text_wrapped(Canvas *c, FontId font, float x, float y, float max_width, const char *text, Rgba color) {
    int line_h = text_line_height(font) + 2;
    float cy = y;
    const char *p = text;
    char line[512];
    while (*p) {
        /* Greedily take words while they fit. */
        int len = 0, last_break = -1;
        const char *q = p;
        while (q[len] && q[len] != '\n') {
            if (q[len] == ' ') last_break = len;
            if (text_width_n(font, q, len + 1) > max_width && len > 0) break;
            len++;
        }
        int take = len;
        int skip = 0;
        if (q[len] && q[len] != '\n' && last_break > 0) {
            take = last_break;
            skip = 1;
        } else if (q[len] == '\n') {
            skip = 1;
        }
        if (take >= (int)sizeof(line)) take = (int)sizeof(line) - 1;
        memcpy(line, q, (size_t)take);
        line[take] = '\0';
        if (c) draw_text(c, font, x, cy, ALIGN_LEFT_TOP, line, color);
        cy += (float)line_h;
        p = q + take + skip;
        if (take == 0 && skip == 0) break;
    }
    return cy - y;
}
