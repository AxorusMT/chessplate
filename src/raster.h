#ifndef CHESSPLATE_RASTER_H
#define CHESSPLATE_RASTER_H

/* Software 2D rasterizer: anti-aliased paths, strokes, gradients, images.
 *
 * Pixels are 32-bit premultiplied ARGB (0xAARRGGBB), which is also the
 * native layout of 32-bit X11 ZPixmaps and Win32 DIB sections on
 * little-endian machines. */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t r, g, b, a; /* straight (non-premultiplied) alpha */
} Rgba;

static inline Rgba rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Rgba c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}
static inline Rgba rgb(uint8_t r, uint8_t g, uint8_t b) { return rgba(r, g, b, 255); }
Rgba rgba_scale_alpha(Rgba c, float factor);

typedef struct {
    float x, y;
} Vec2;

static inline Vec2 vec2(float x, float y) {
    Vec2 v;
    v.x = x;
    v.y = y;
    return v;
}

typedef struct {
    uint32_t *pixels;
    int w, h;
} Image;

bool image_alloc(Image *img, int w, int h);
void image_free(Image *img);

typedef struct {
    uint32_t *pixels;
    int w, h;
    int clip_x0, clip_y0, clip_x1, clip_y1; /* half-open */
} Canvas;

void canvas_init(Canvas *c, uint32_t *pixels, int w, int h);
void canvas_from_image(Canvas *c, Image *img);
void canvas_clear(Canvas *c, Rgba color);
/* Sets the clip to the intersection of the canvas bounds and the given rect. */
void canvas_set_clip(Canvas *c, float x0, float y0, float x1, float y1);
void canvas_reset_clip(Canvas *c);

/* ---- Paths ------------------------------------------------------------- */

typedef struct {
    Vec2 *pts;
    int count, cap;
    int *starts; /* index of first point for each contour */
    bool *closed;
    int contours, contour_cap;
} Path;

void path_init(Path *p);
void path_free(Path *p);
void path_reset(Path *p);
void path_move_to(Path *p, float x, float y);
void path_line_to(Path *p, float x, float y);
void path_close(Path *p);
/* Curves are flattened immediately (coordinates are device pixels). */
void path_quad_to(Path *p, float cx, float cy, float x, float y);
void path_cubic_to(Path *p, float c1x, float c1y, float c2x, float c2y, float x, float y);
Vec2 path_current_point(const Path *p);
bool path_has_current(const Path *p);

void path_add_rect(Path *p, float x0, float y0, float x1, float y1);
void path_add_round_rect(Path *p, float x0, float y0, float x1, float y1, float r);
void path_add_ellipse(Path *p, float cx, float cy, float rx, float ry);
void path_add_circle(Path *p, float cx, float cy, float r);
void path_add_polygon(Path *p, const Vec2 *pts, int n);

/* Number of line segments needed to approximate an arc of `radius` pixels
 * spanning `angle` radians within a small error. */
int arc_segments(float radius, float angle);

/* ---- Painting ---------------------------------------------------------- */

typedef enum { FILL_NONZERO, FILL_EVENODD } FillRule;
typedef enum { CAP_BUTT, CAP_ROUND, CAP_SQUARE } LineCap;
typedef enum { JOIN_MITER, JOIN_ROUND, JOIN_BEVEL } LineJoin;

#define PAINT_MAX_STOPS 8

typedef struct {
    int gradient; /* 0 = solid color */
    Rgba color;
    float opacity; /* extra multiplier, 0..1 */
    /* Linear gradient: t = ga * x + gb * y + gc (device pixel centers). */
    float ga, gb, gc;
    int stop_count;
    float stop_offset[PAINT_MAX_STOPS];
    Rgba stop_color[PAINT_MAX_STOPS];
} Paint;

Paint paint_solid(Rgba color);

void raster_fill_path(Canvas *c, const Path *path, FillRule rule, const Paint *paint);

typedef struct {
    float width;
    LineCap cap;
    LineJoin join;
    float miter_limit;
} StrokeStyle;

/* Converts `path` into fill polygons representing its stroke outline. */
void stroke_to_path(const Path *path, const StrokeStyle *style, Path *out);
void raster_stroke_path(Canvas *c, const Path *path, const StrokeStyle *style, const Paint *paint);

/* ---- Convenience shapes ------------------------------------------------ */

void draw_rect(Canvas *c, float x0, float y0, float x1, float y1, Rgba color);
void draw_round_rect(Canvas *c, float x0, float y0, float x1, float y1, float r, Rgba color);
void draw_round_rect_stroke(Canvas *c, float x0, float y0, float x1, float y1, float r, float width, Rgba color);
void draw_line(Canvas *c, float x0, float y0, float x1, float y1, float width, Rgba color);
void draw_polyline(Canvas *c, const Vec2 *pts, int n, float width, Rgba color);
void draw_polygon(Canvas *c, const Vec2 *pts, int n, Rgba color);
void draw_circle(Canvas *c, float cx, float cy, float r, Rgba color);
void draw_circle_stroke(Canvas *c, float cx, float cy, float r, float width, Rgba color);
/* Composites a premultiplied image with its top-left at (x, y). */
void draw_image(Canvas *c, const Image *img, int x, int y);

#endif
