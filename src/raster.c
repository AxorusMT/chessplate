#include "raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Vertical sub-scanlines per pixel row; horizontal coverage is exact. */
#define SUBSAMPLES 8

static inline uint32_t div255(uint32_t x) { return (x + 128u + ((x + 128u) >> 8)) >> 8; }

Rgba rgba_scale_alpha(Rgba c, float factor) {
    float a = (float)c.a * factor;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    c.a = (uint8_t)(a + 0.5f);
    return c;
}

/* Blend a straight-alpha color with coverage alpha (0..255) over a pixel. */
static inline void blend_pixel(uint32_t *dst, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    if (a == 0) return;
    uint32_t pr = div255(r * a), pg = div255(g * a), pb = div255(b * a);
    if (a == 255) {
        *dst = 0xff000000u | (pr << 16) | (pg << 8) | pb;
        return;
    }
    uint32_t d = *dst;
    uint32_t inv = 255u - a;
    uint32_t da = d >> 24, dr = (d >> 16) & 0xff, dg = (d >> 8) & 0xff, db = d & 0xff;
    uint32_t oa = a + div255(da * inv);
    uint32_t or_ = pr + div255(dr * inv);
    uint32_t og = pg + div255(dg * inv);
    uint32_t ob = pb + div255(db * inv);
    *dst = (oa << 24) | (or_ << 16) | (og << 8) | ob;
}

bool image_alloc(Image *img, int w, int h) {
    img->w = w;
    img->h = h;
    img->pixels = (uint32_t *)calloc((size_t)(w > 0 ? w : 1) * (size_t)(h > 0 ? h : 1), sizeof(uint32_t));
    return img->pixels != NULL;
}

void image_free(Image *img) {
    free(img->pixels);
    img->pixels = NULL;
    img->w = img->h = 0;
}

void canvas_init(Canvas *c, uint32_t *pixels, int w, int h) {
    c->pixels = pixels;
    c->w = w;
    c->h = h;
    canvas_reset_clip(c);
}

void canvas_from_image(Canvas *c, Image *img) { canvas_init(c, img->pixels, img->w, img->h); }

void canvas_reset_clip(Canvas *c) {
    c->clip_x0 = 0;
    c->clip_y0 = 0;
    c->clip_x1 = c->w;
    c->clip_y1 = c->h;
}

void canvas_set_clip(Canvas *c, float x0, float y0, float x1, float y1) {
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > c->w) ix1 = c->w;
    if (iy1 > c->h) iy1 = c->h;
    if (ix1 < ix0) ix1 = ix0;
    if (iy1 < iy0) iy1 = iy0;
    c->clip_x0 = ix0;
    c->clip_y0 = iy0;
    c->clip_x1 = ix1;
    c->clip_y1 = iy1;
}

void canvas_clear(Canvas *c, Rgba color) {
    uint32_t a = color.a;
    uint32_t px = (a << 24) | (div255(color.r * a) << 16) | (div255(color.g * a) << 8) | div255(color.b * a);
    for (int y = c->clip_y0; y < c->clip_y1; y++) {
        uint32_t *row = c->pixels + (size_t)y * (size_t)c->w;
        for (int x = c->clip_x0; x < c->clip_x1; x++) row[x] = px;
    }
}

/* ------------------------------------------------------------------------ */
/* Paths                                                                    */
/* ------------------------------------------------------------------------ */

void path_init(Path *p) { memset(p, 0, sizeof(*p)); }

void path_free(Path *p) {
    free(p->pts);
    free(p->starts);
    free(p->closed);
    path_init(p);
}

void path_reset(Path *p) {
    p->count = 0;
    p->contours = 0;
}

static void path_push_point(Path *p, float x, float y) {
    if (p->count == p->cap) {
        int cap = p->cap ? p->cap * 2 : 64;
        Vec2 *pts = (Vec2 *)realloc(p->pts, (size_t)cap * sizeof(Vec2));
        if (!pts) return;
        p->pts = pts;
        p->cap = cap;
    }
    p->pts[p->count++] = vec2(x, y);
}

void path_move_to(Path *p, float x, float y) {
    if (p->contours == p->contour_cap) {
        int cap = p->contour_cap ? p->contour_cap * 2 : 8;
        int *starts = (int *)realloc(p->starts, (size_t)cap * sizeof(int));
        bool *closed = (bool *)realloc(p->closed, (size_t)cap * sizeof(bool));
        if (starts) p->starts = starts;
        if (closed) p->closed = closed;
        if (!starts || !closed) return;
        p->contour_cap = cap;
    }
    p->starts[p->contours] = p->count;
    p->closed[p->contours] = false;
    p->contours++;
    path_push_point(p, x, y);
}

bool path_has_current(const Path *p) { return p->contours > 0 && p->count > p->starts[p->contours - 1]; }

Vec2 path_current_point(const Path *p) {
    if (p->count == 0) return vec2(0, 0);
    return p->pts[p->count - 1];
}

void path_line_to(Path *p, float x, float y) {
    if (!path_has_current(p)) {
        path_move_to(p, x, y);
        return;
    }
    path_push_point(p, x, y);
}

void path_close(Path *p) {
    if (p->contours > 0) p->closed[p->contours - 1] = true;
}

static int curve_segments(float length) {
    int n = (int)ceilf(sqrtf(length * 2.0f));
    if (n < 2) n = 2;
    if (n > 128) n = 128;
    return n;
}

void path_quad_to(Path *p, float cx, float cy, float x, float y) {
    Vec2 s = path_current_point(p);
    float len = hypotf(cx - s.x, cy - s.y) + hypotf(x - cx, y - cy);
    int n = curve_segments(len);
    for (int i = 1; i <= n; i++) {
        float t = (float)i / (float)n, u = 1.0f - t;
        path_line_to(p, u * u * s.x + 2 * u * t * cx + t * t * x, u * u * s.y + 2 * u * t * cy + t * t * y);
    }
}

void path_cubic_to(Path *p, float c1x, float c1y, float c2x, float c2y, float x, float y) {
    Vec2 s = path_current_point(p);
    float len = hypotf(c1x - s.x, c1y - s.y) + hypotf(c2x - c1x, c2y - c1y) + hypotf(x - c2x, y - c2y);
    int n = curve_segments(len);
    for (int i = 1; i <= n; i++) {
        float t = (float)i / (float)n, u = 1.0f - t;
        float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        path_line_to(p, a * s.x + b * c1x + c * c2x + d * x, a * s.y + b * c1y + c * c2y + d * y);
    }
}

int arc_segments(float radius, float angle) {
    const float tol = 0.12f;
    float a = fabsf(angle);
    if (radius <= tol) return 4;
    float step = 2.0f * acosf(1.0f - tol / radius);
    if (!(step > 0.0f)) step = 0.5f;
    int n = (int)ceilf(a / step);
    if (n < 4) n = 4;
    if (n > 720) n = 720;
    return n;
}

void path_add_rect(Path *p, float x0, float y0, float x1, float y1) {
    path_move_to(p, x0, y0);
    path_line_to(p, x1, y0);
    path_line_to(p, x1, y1);
    path_line_to(p, x0, y1);
    path_close(p);
}

static void path_corner(Path *p, float cx, float cy, float r, float a0) {
    int n = arc_segments(r, (float)M_PI * 0.5f);
    for (int i = 0; i <= n; i++) {
        float a = a0 + (float)M_PI * 0.5f * (float)i / (float)n;
        path_line_to(p, cx + cosf(a) * r, cy + sinf(a) * r);
    }
}

void path_add_round_rect(Path *p, float x0, float y0, float x1, float y1, float r) {
    float w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r <= 0.01f) {
        path_add_rect(p, x0, y0, x1, y1);
        return;
    }
    path_move_to(p, x0 + r, y0);
    path_corner(p, x1 - r, y0 + r, r, -(float)M_PI * 0.5f);
    path_corner(p, x1 - r, y1 - r, r, 0.0f);
    path_corner(p, x0 + r, y1 - r, r, (float)M_PI * 0.5f);
    path_corner(p, x0 + r, y0 + r, r, (float)M_PI);
    path_close(p);
}

void path_add_ellipse(Path *p, float cx, float cy, float rx, float ry) {
    if (rx <= 0 || ry <= 0) return;
    int n = arc_segments(rx > ry ? rx : ry, 2.0f * (float)M_PI);
    path_move_to(p, cx + rx, cy);
    for (int i = 1; i < n; i++) {
        float a = 2.0f * (float)M_PI * (float)i / (float)n;
        path_line_to(p, cx + cosf(a) * rx, cy + sinf(a) * ry);
    }
    path_close(p);
}

void path_add_circle(Path *p, float cx, float cy, float r) { path_add_ellipse(p, cx, cy, r, r); }

void path_add_polygon(Path *p, const Vec2 *pts, int n) {
    if (n <= 0) return;
    path_move_to(p, pts[0].x, pts[0].y);
    for (int i = 1; i < n; i++) path_line_to(p, pts[i].x, pts[i].y);
    path_close(p);
}

/* ------------------------------------------------------------------------ */
/* Scanline fill                                                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    float y0, y1; /* y0 < y1 */
    float x0;     /* x at y0 */
    float dxdy;
    int dir;
} Edge;

typedef struct {
    float x;
    int dir;
} Crossing;

/* Scratch buffers, grown on demand. Rendering happens on one thread only. */
static Edge *g_edges;
static int g_edge_cap;
static Crossing *g_cross;
static int g_cross_cap;
static float *g_cov;
static int g_cov_cap;

static bool ensure_scratch(int edges, int width) {
    if (edges > g_edge_cap) {
        Edge *e = (Edge *)realloc(g_edges, (size_t)edges * sizeof(Edge));
        Crossing *c = (Crossing *)realloc(g_cross, (size_t)edges * sizeof(Crossing));
        if (e) g_edges = e;
        if (c) g_cross = c;
        if (!e || !c) return false;
        g_edge_cap = g_cross_cap = edges;
    }
    if (width + 2 > g_cov_cap) {
        float *cov = (float *)realloc(g_cov, (size_t)(width + 2) * sizeof(float));
        if (!cov) return false;
        g_cov = cov;
        g_cov_cap = width + 2;
    }
    return true;
}

static int edge_cmp(const void *a, const void *b) {
    float ya = ((const Edge *)a)->y0, yb = ((const Edge *)b)->y0;
    return ya < yb ? -1 : (ya > yb ? 1 : 0);
}

Paint paint_solid(Rgba color) {
    Paint p;
    memset(&p, 0, sizeof(p));
    p.color = color;
    p.opacity = 1.0f;
    return p;
}

static Rgba gradient_color(const Paint *paint, float t) {
    if (paint->stop_count <= 0) return paint->color;
    if (t <= paint->stop_offset[0]) return paint->stop_color[0];
    int last = paint->stop_count - 1;
    if (t >= paint->stop_offset[last]) return paint->stop_color[last];
    for (int i = 0; i < last; i++) {
        float o0 = paint->stop_offset[i], o1 = paint->stop_offset[i + 1];
        if (t >= o0 && t <= o1) {
            float f = o1 > o0 ? (t - o0) / (o1 - o0) : 0.0f;
            Rgba a = paint->stop_color[i], b = paint->stop_color[i + 1];
            return rgba((uint8_t)(a.r + (b.r - a.r) * f + 0.5f), (uint8_t)(a.g + (b.g - a.g) * f + 0.5f),
                        (uint8_t)(a.b + (b.b - a.b) * f + 0.5f), (uint8_t)(a.a + (b.a - a.a) * f + 0.5f));
        }
    }
    return paint->stop_color[last];
}

static inline void add_span(float *cov, float xa, float xb, float w, int x0, int x1, int *minx, int *maxx) {
    if (xa < (float)x0) xa = (float)x0;
    if (xb > (float)x1) xb = (float)x1;
    if (xb <= xa) return;
    int ia = (int)xa, ib = (int)xb;
    if (ia < *minx) *minx = ia;
    if (ib > *maxx) *maxx = ib;
    if (ia == ib) {
        cov[ia - x0] += (xb - xa) * w;
        return;
    }
    cov[ia - x0] += ((float)(ia + 1) - xa) * w;
    for (int i = ia + 1; i < ib; i++) cov[i - x0] += w;
    if (ib < x1) cov[ib - x0] += (xb - (float)ib) * w;
}

void raster_fill_path(Canvas *c, const Path *path, FillRule rule, const Paint *paint) {
    if (path->count < 3 || c->clip_x1 <= c->clip_x0 || c->clip_y1 <= c->clip_y0) return;
    int cw = c->clip_x1 - c->clip_x0;
    if (!ensure_scratch(path->count + 1, cw)) return;

    int ne = 0;
    float miny = 1e30f, maxy = -1e30f;
    for (int ci = 0; ci < path->contours; ci++) {
        int start = path->starts[ci];
        int end = ci + 1 < path->contours ? path->starts[ci + 1] : path->count;
        int n = end - start;
        if (n < 2) continue;
        for (int i = 0; i < n; i++) {
            Vec2 a = path->pts[start + i];
            Vec2 b = path->pts[start + (i + 1) % n];
            if (a.y == b.y) continue;
            if (!(a.x == a.x) || !(b.x == b.x)) continue; /* NaN guard */
            Edge *e = &g_edges[ne++];
            if (a.y < b.y) {
                e->y0 = a.y, e->y1 = b.y, e->x0 = a.x, e->dir = 1;
                e->dxdy = (b.x - a.x) / (b.y - a.y);
            } else {
                e->y0 = b.y, e->y1 = a.y, e->x0 = b.x, e->dir = -1;
                e->dxdy = (a.x - b.x) / (a.y - b.y);
            }
            if (e->y0 < miny) miny = e->y0;
            if (e->y1 > maxy) maxy = e->y1;
        }
    }
    if (ne == 0) return;
    qsort(g_edges, (size_t)ne, sizeof(Edge), edge_cmp);

    int y_start = (int)floorf(miny), y_end = (int)ceilf(maxy);
    if (y_start < c->clip_y0) y_start = c->clip_y0;
    if (y_end > c->clip_y1) y_end = c->clip_y1;

    const float sub_w = 1.0f / SUBSAMPLES;
    float opacity = paint->opacity;
    int x0 = c->clip_x0, x1 = c->clip_x1;
    int first_edge = 0;

    for (int y = y_start; y < y_end; y++) {
        int minx = x1, maxx = x0 - 1;
        /* Skip the prefix of edges that end above this row. */
        while (first_edge < ne && g_edges[first_edge].y1 <= (float)y) first_edge++;

        for (int s = 0; s < SUBSAMPLES; s++) {
            float sy = (float)y + ((float)s + 0.5f) * sub_w;
            int nc = 0;
            for (int i = first_edge; i < ne; i++) {
                const Edge *e = &g_edges[i];
                if (e->y0 > sy) break;
                if (sy >= e->y1) continue;
                g_cross[nc].x = e->x0 + (sy - e->y0) * e->dxdy;
                g_cross[nc].dir = e->dir;
                nc++;
            }
            if (nc < 2) continue;
            /* insertion sort by x */
            for (int i = 1; i < nc; i++) {
                Crossing k = g_cross[i];
                int j = i - 1;
                while (j >= 0 && g_cross[j].x > k.x) {
                    g_cross[j + 1] = g_cross[j];
                    j--;
                }
                g_cross[j + 1] = k;
            }
            int winding = 0;
            float span_start = 0;
            for (int i = 0; i < nc; i++) {
                bool was_inside = rule == FILL_NONZERO ? winding != 0 : (winding & 1) != 0;
                winding += g_cross[i].dir;
                bool inside = rule == FILL_NONZERO ? winding != 0 : (winding & 1) != 0;
                if (!was_inside && inside) span_start = g_cross[i].x;
                else if (was_inside && !inside)
                    add_span(g_cov, span_start, g_cross[i].x, sub_w, x0, x1, &minx, &maxx);
            }
        }

        if (maxx < minx) continue;
        if (maxx >= x1) maxx = x1 - 1;
        uint32_t *row = c->pixels + (size_t)y * (size_t)c->w;
        for (int x = minx; x <= maxx; x++) {
            float cov = g_cov[x - x0];
            g_cov[x - x0] = 0.0f;
            if (cov <= 0.0005f) continue;
            if (cov > 1.0f) cov = 1.0f;
            Rgba col = paint->gradient
                           ? gradient_color(paint, paint->ga * ((float)x + 0.5f) + paint->gb * ((float)y + 0.5f) + paint->gc)
                           : paint->color;
            uint32_t a = (uint32_t)(cov * opacity * (float)col.a + 0.5f);
            if (a > 255) a = 255;
            blend_pixel(&row[x], col.r, col.g, col.b, a);
        }
        if (maxx + 1 < x1) g_cov[maxx + 1 - x0] = 0.0f;
    }
}

/* ------------------------------------------------------------------------ */
/* Stroking                                                                 */
/* ------------------------------------------------------------------------ */

static void add_oriented_polygon(Path *out, const Vec2 *pts, int n) {
    if (n < 3) return;
    float area = 0;
    for (int i = 0; i < n; i++) {
        Vec2 a = pts[i], b = pts[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    if (fabsf(area) < 1e-9f) return;
    if (area > 0) {
        path_move_to(out, pts[0].x, pts[0].y);
        for (int i = 1; i < n; i++) path_line_to(out, pts[i].x, pts[i].y);
    } else {
        path_move_to(out, pts[n - 1].x, pts[n - 1].y);
        for (int i = n - 2; i >= 0; i--) path_line_to(out, pts[i].x, pts[i].y);
    }
    path_close(out);
}

static void add_disc(Path *out, Vec2 c, float r) {
    Vec2 buf[720];
    int n = arc_segments(r, 2.0f * (float)M_PI);
    for (int i = 0; i < n; i++) {
        float a = 2.0f * (float)M_PI * (float)i / (float)n;
        buf[i] = vec2(c.x + cosf(a) * r, c.y + sinf(a) * r);
    }
    add_oriented_polygon(out, buf, n);
}

void stroke_to_path(const Path *path, const StrokeStyle *style, Path *out) {
    float hw = style->width * 0.5f;
    if (hw <= 0.0f) return;

    for (int ci = 0; ci < path->contours; ci++) {
        int start = path->starts[ci];
        int end = ci + 1 < path->contours ? path->starts[ci + 1] : path->count;
        bool closed = path->closed[ci];

        /* Copy points dropping consecutive duplicates. */
        int cap = end - start;
        if (cap <= 0) continue;
        Vec2 *pts = (Vec2 *)malloc((size_t)cap * sizeof(Vec2));
        if (!pts) return;
        int n = 0;
        for (int i = start; i < end; i++) {
            Vec2 p = path->pts[i];
            if (n > 0 && fabsf(p.x - pts[n - 1].x) < 1e-4f && fabsf(p.y - pts[n - 1].y) < 1e-4f) continue;
            pts[n++] = p;
        }
        if (closed && n > 1 && fabsf(pts[0].x - pts[n - 1].x) < 1e-4f && fabsf(pts[0].y - pts[n - 1].y) < 1e-4f) n--;

        if (n == 1) {
            if (style->cap == CAP_ROUND) add_disc(out, pts[0], hw);
            else if (style->cap == CAP_SQUARE) {
                Vec2 q[4] = {vec2(pts[0].x - hw, pts[0].y - hw), vec2(pts[0].x + hw, pts[0].y - hw),
                             vec2(pts[0].x + hw, pts[0].y + hw), vec2(pts[0].x - hw, pts[0].y + hw)};
                add_oriented_polygon(out, q, 4);
            }
            free(pts);
            continue;
        }
        if (n < 2) {
            free(pts);
            continue;
        }
        if (n == 2) closed = false;

        int nseg = closed ? n : n - 1;
        for (int i = 0; i < nseg; i++) {
            Vec2 a = pts[i], b = pts[(i + 1) % n];
            float dx = b.x - a.x, dy = b.y - a.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-6f) continue;
            dx /= len;
            dy /= len;
            float nx = -dy * hw, ny = dx * hw;
            if (!closed && style->cap == CAP_SQUARE) {
                if (i == 0) a.x -= dx * hw, a.y -= dy * hw;
                if (i == nseg - 1) b.x += dx * hw, b.y += dy * hw;
            }
            Vec2 q[4] = {vec2(a.x + nx, a.y + ny), vec2(b.x + nx, b.y + ny), vec2(b.x - nx, b.y - ny),
                         vec2(a.x - nx, a.y - ny)};
            add_oriented_polygon(out, q, 4);
        }

        int jfirst = closed ? 0 : 1;
        int jlast = closed ? n - 1 : n - 2;
        for (int i = jfirst; i <= jlast; i++) {
            Vec2 prev = pts[(i - 1 + n) % n], v = pts[i], next = pts[(i + 1) % n];
            float d1x = v.x - prev.x, d1y = v.y - prev.y, d2x = next.x - v.x, d2y = next.y - v.y;
            float l1 = sqrtf(d1x * d1x + d1y * d1y), l2 = sqrtf(d2x * d2x + d2y * d2y);
            if (l1 < 1e-6f || l2 < 1e-6f) continue;
            d1x /= l1, d1y /= l1, d2x /= l2, d2y /= l2;
            float cross = d1x * d2y - d1y * d2x;
            float dot = d1x * d2x + d1y * d2y;
            if (fabsf(cross) < 1e-6f && dot > 0) continue;

            if (style->join == JOIN_ROUND) {
                add_disc(out, v, hw);
                continue;
            }
            float s = cross > 0 ? -1.0f : 1.0f;
            Vec2 o1 = vec2(-d1y * hw * s, d1x * hw * s);
            Vec2 o2 = vec2(-d2y * hw * s, d2x * hw * s);
            Vec2 p1 = vec2(v.x + o1.x, v.y + o1.y), p2 = vec2(v.x + o2.x, v.y + o2.y);
            if (style->join == JOIN_MITER) {
                float bx = o1.x + o2.x, by = o1.y + o2.y;
                float bl2 = bx * bx + by * by;
                float bl = sqrtf(bl2);
                float limit = style->miter_limit > 1.0f ? style->miter_limit : 4.0f;
                if (bl > 1e-6f && 2.0f * hw / bl <= limit) {
                    float k = 2.0f * hw * hw / bl2;
                    Vec2 m = vec2(v.x + bx * k, v.y + by * k);
                    Vec2 q[4] = {v, p1, m, p2};
                    add_oriented_polygon(out, q, 4);
                    continue;
                }
            }
            Vec2 t[3] = {v, p1, p2};
            add_oriented_polygon(out, t, 3);
        }

        if (!closed && style->cap == CAP_ROUND) {
            add_disc(out, pts[0], hw);
            add_disc(out, pts[n - 1], hw);
        }
        free(pts);
    }
}

void raster_stroke_path(Canvas *c, const Path *path, const StrokeStyle *style, const Paint *paint) {
    Path outline;
    path_init(&outline);
    stroke_to_path(path, style, &outline);
    raster_fill_path(c, &outline, FILL_NONZERO, paint);
    path_free(&outline);
}

/* ------------------------------------------------------------------------ */
/* Convenience shapes                                                       */
/* ------------------------------------------------------------------------ */

void draw_rect(Canvas *c, float x0, float y0, float x1, float y1, Rgba color) {
    if (x1 <= x0 || y1 <= y0 || color.a == 0) return;
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x1), iy1 = (int)ceilf(y1);
    if (ix0 < c->clip_x0) ix0 = c->clip_x0;
    if (iy0 < c->clip_y0) iy0 = c->clip_y0;
    if (ix1 > c->clip_x1) ix1 = c->clip_x1;
    if (iy1 > c->clip_y1) iy1 = c->clip_y1;
    for (int y = iy0; y < iy1; y++) {
        float cy = fminf(y1, (float)y + 1.0f) - fmaxf(y0, (float)y);
        if (cy <= 0) continue;
        uint32_t *row = c->pixels + (size_t)y * (size_t)c->w;
        for (int x = ix0; x < ix1; x++) {
            float cx = fminf(x1, (float)x + 1.0f) - fmaxf(x0, (float)x);
            if (cx <= 0) continue;
            uint32_t a = (uint32_t)(cx * cy * (float)color.a + 0.5f);
            blend_pixel(&row[x], color.r, color.g, color.b, a > 255 ? 255 : a);
        }
    }
}

void draw_round_rect(Canvas *c, float x0, float y0, float x1, float y1, float r, Rgba color) {
    if (r <= 0.01f) {
        draw_rect(c, x0, y0, x1, y1, color);
        return;
    }
    Path p;
    path_init(&p);
    path_add_round_rect(&p, x0, y0, x1, y1, r);
    Paint paint = paint_solid(color);
    raster_fill_path(c, &p, FILL_NONZERO, &paint);
    path_free(&p);
}

void draw_round_rect_stroke(Canvas *c, float x0, float y0, float x1, float y1, float r, float width, Rgba color) {
    float h = width * 0.5f;
    Path p;
    path_init(&p);
    path_add_round_rect(&p, x0 - h, y0 - h, x1 + h, y1 + h, r + h);
    path_add_round_rect(&p, x0 + h, y0 + h, x1 - h, y1 - h, r - h > 0 ? r - h : 0);
    Paint paint = paint_solid(color);
    raster_fill_path(c, &p, FILL_EVENODD, &paint);
    path_free(&p);
}

void draw_line(Canvas *c, float x0, float y0, float x1, float y1, float width, Rgba color) {
    Path p;
    path_init(&p);
    path_move_to(&p, x0, y0);
    path_line_to(&p, x1, y1);
    StrokeStyle st = {width, CAP_BUTT, JOIN_MITER, 4.0f};
    Paint paint = paint_solid(color);
    raster_stroke_path(c, &p, &st, &paint);
    path_free(&p);
}

void draw_polyline(Canvas *c, const Vec2 *pts, int n, float width, Rgba color) {
    if (n < 2) return;
    Path p;
    path_init(&p);
    path_move_to(&p, pts[0].x, pts[0].y);
    for (int i = 1; i < n; i++) path_line_to(&p, pts[i].x, pts[i].y);
    StrokeStyle st = {width, CAP_BUTT, JOIN_ROUND, 4.0f};
    Paint paint = paint_solid(color);
    raster_stroke_path(c, &p, &st, &paint);
    path_free(&p);
}

void draw_polygon(Canvas *c, const Vec2 *pts, int n, Rgba color) {
    Path p;
    path_init(&p);
    path_add_polygon(&p, pts, n);
    Paint paint = paint_solid(color);
    raster_fill_path(c, &p, FILL_NONZERO, &paint);
    path_free(&p);
}

void draw_circle(Canvas *c, float cx, float cy, float r, Rgba color) {
    Path p;
    path_init(&p);
    path_add_circle(&p, cx, cy, r);
    Paint paint = paint_solid(color);
    raster_fill_path(c, &p, FILL_NONZERO, &paint);
    path_free(&p);
}

void draw_circle_stroke(Canvas *c, float cx, float cy, float r, float width, Rgba color) {
    float h = width * 0.5f;
    Path p;
    path_init(&p);
    path_add_circle(&p, cx, cy, r + h);
    if (r - h > 0) path_add_circle(&p, cx, cy, r - h);
    Paint paint = paint_solid(color);
    raster_fill_path(c, &p, FILL_EVENODD, &paint);
    path_free(&p);
}

void draw_image(Canvas *c, const Image *img, int x, int y) {
    if (!img->pixels) return;
    int sx0 = 0, sy0 = 0, w = img->w, h = img->h;
    if (x < c->clip_x0) sx0 = c->clip_x0 - x;
    if (y < c->clip_y0) sy0 = c->clip_y0 - y;
    if (x + w > c->clip_x1) w = c->clip_x1 - x;
    if (y + h > c->clip_y1) h = c->clip_y1 - y;
    for (int sy = sy0; sy < h; sy++) {
        const uint32_t *src = img->pixels + (size_t)sy * (size_t)img->w;
        uint32_t *dst = c->pixels + (size_t)(y + sy) * (size_t)c->w + x;
        for (int sx = sx0; sx < w; sx++) {
            uint32_t s = src[sx];
            uint32_t sa = s >> 24;
            if (sa == 0) continue;
            if (sa == 255) {
                dst[sx] = s;
                continue;
            }
            uint32_t d = dst[sx], inv = 255u - sa;
            uint32_t oa = sa + div255((d >> 24) * inv);
            uint32_t or_ = ((s >> 16) & 0xff) + div255(((d >> 16) & 0xff) * inv);
            uint32_t og = ((s >> 8) & 0xff) + div255(((d >> 8) & 0xff) * inv);
            uint32_t ob = (s & 0xff) + div255((d & 0xff) * inv);
            if (or_ > 255) or_ = 255;
            if (og > 255) og = 255;
            if (ob > 255) ob = 255;
            if (oa > 255) oa = 255;
            dst[sx] = (oa << 24) | (or_ << 16) | (og << 8) | ob;
        }
    }
}
