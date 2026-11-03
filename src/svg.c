#include "svg.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_DEPTH 32
#define MAX_GRADIENTS 16
#define ID_LEN 32
#define KAPPA 0.5522847498f

typedef struct {
    float a, b, c, d, e, f; /* x' = a x + c y + e ; y' = b x + d y + f */
} Matrix;

typedef enum { PAINT_NONE, PAINT_COLOR, PAINT_REF } PaintKind;

typedef struct {
    PaintKind kind;
    Rgba color;
    char ref[ID_LEN];
} SvgPaint;

typedef struct {
    Matrix m;
    SvgPaint fill, stroke;
    float fill_opacity, stroke_opacity, opacity;
    float stroke_width, miter_limit;
    LineCap cap;
    LineJoin join;
    FillRule fill_rule;
    bool hidden; /* inside defs/filter/title/... */
} Style;

typedef struct {
    char id[ID_LEN];
    float x1, y1, x2, y2;
    bool user_space;
    int stop_count;
    float offsets[PAINT_MAX_STOPS];
    Rgba colors[PAINT_MAX_STOPS];
} Gradient;

typedef struct {
    Canvas canvas;
    Style stack[MAX_DEPTH];
    int depth;
    Gradient gradients[MAX_GRADIENTS];
    int gradient_count;
    Gradient *open_gradient;
    int width, height;
} Renderer;

/* ------------------------------------------------------------------------ */
/* Matrix helpers                                                           */
/* ------------------------------------------------------------------------ */

static Matrix mat_identity(void) {
    Matrix m = {1, 0, 0, 1, 0, 0};
    return m;
}

static Matrix mat_mul(Matrix p, Matrix q) { /* p * q (apply q first) */
    Matrix r;
    r.a = p.a * q.a + p.c * q.b;
    r.b = p.b * q.a + p.d * q.b;
    r.c = p.a * q.c + p.c * q.d;
    r.d = p.b * q.c + p.d * q.d;
    r.e = p.a * q.e + p.c * q.f + p.e;
    r.f = p.b * q.e + p.d * q.f + p.f;
    return r;
}

static Matrix mat_invert(Matrix m) {
    float det = m.a * m.d - m.b * m.c;
    Matrix r = mat_identity();
    if (fabsf(det) < 1e-12f) return r;
    float id = 1.0f / det;
    r.a = m.d * id;
    r.b = -m.b * id;
    r.c = -m.c * id;
    r.d = m.a * id;
    r.e = (m.c * m.f - m.d * m.e) * id;
    r.f = (m.b * m.e - m.a * m.f) * id;
    return r;
}

static Vec2 mat_apply(Matrix m, float x, float y) { return vec2(m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f); }

/* ------------------------------------------------------------------------ */
/* Lexing helpers                                                           */
/* ------------------------------------------------------------------------ */

static const char *skip_sep(const char *s, const char *end) {
    while (s < end && (isspace((unsigned char)*s) || *s == ',')) s++;
    return s;
}

static bool parse_float(const char **sp, const char *end, float *out) {
    const char *s = skip_sep(*sp, end);
    if (s >= end) return false;
    char buf[64];
    int n = 0;
    if (*s == '+' || *s == '-') buf[n++] = *s++;
    bool digits = false, dot = false;
    while (s < end && n < 60) {
        if (isdigit((unsigned char)*s)) {
            digits = true;
            buf[n++] = *s++;
        } else if (*s == '.' && !dot) {
            dot = true;
            buf[n++] = *s++;
        } else {
            break;
        }
    }
    if (!digits) return false;
    if (s < end && (*s == 'e' || *s == 'E') && s + 1 < end &&
        (isdigit((unsigned char)s[1]) || ((s[1] == '-' || s[1] == '+') && s + 2 < end && isdigit((unsigned char)s[2])))) {
        buf[n++] = *s++;
        if (*s == '-' || *s == '+') buf[n++] = *s++;
        while (s < end && isdigit((unsigned char)*s) && n < 63) buf[n++] = *s++;
    }
    buf[n] = '\0';
    *out = (float)strtod(buf, NULL);
    *sp = s;
    return true;
}

static bool parse_flag(const char **sp, const char *end, bool *out) {
    const char *s = skip_sep(*sp, end);
    if (s >= end || (*s != '0' && *s != '1')) return false;
    *out = *s == '1';
    *sp = s + 1;
    return true;
}

static float parse_length(const char *v, float percent_base) {
    const char *s = v;
    float f = 0;
    if (!parse_float(&s, v + strlen(v), &f)) return 0;
    if (*s == '%') f = f * percent_base / 100.0f;
    return f;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static bool parse_color(const char *v, Rgba *out) {
    while (isspace((unsigned char)*v)) v++;
    size_t len = strlen(v);
    while (len > 0 && isspace((unsigned char)v[len - 1])) len--;
    if (v[0] == '#') {
        if (len >= 7) {
            *out = rgb((uint8_t)(hex_val(v[1]) * 16 + hex_val(v[2])), (uint8_t)(hex_val(v[3]) * 16 + hex_val(v[4])),
                       (uint8_t)(hex_val(v[5]) * 16 + hex_val(v[6])));
            return true;
        }
        if (len >= 4) {
            *out = rgb((uint8_t)(hex_val(v[1]) * 17), (uint8_t)(hex_val(v[2]) * 17), (uint8_t)(hex_val(v[3]) * 17));
            return true;
        }
        return false;
    }
    if (strncmp(v, "rgb(", 4) == 0) {
        const char *s = v + 4, *end = v + len;
        float r = 0, g = 0, b = 0;
        parse_float(&s, end, &r);
        parse_float(&s, end, &g);
        parse_float(&s, end, &b);
        *out = rgb((uint8_t)fminf(fmaxf(r, 0), 255), (uint8_t)fminf(fmaxf(g, 0), 255), (uint8_t)fminf(fmaxf(b, 0), 255));
        return true;
    }
    static const struct {
        const char *name;
        uint8_t r, g, b;
    } named[] = {
        {"black", 0, 0, 0},       {"white", 255, 255, 255}, {"red", 255, 0, 0},     {"green", 0, 128, 0},
        {"blue", 0, 0, 255},      {"gray", 128, 128, 128},  {"grey", 128, 128, 128}, {"yellow", 255, 255, 0},
        {"currentColor", 0, 0, 0},
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (strlen(named[i].name) == len && strncmp(v, named[i].name, len) == 0) {
            *out = rgb(named[i].r, named[i].g, named[i].b);
            return true;
        }
    }
    return false;
}

static void parse_paint(const char *v, SvgPaint *paint) {
    while (isspace((unsigned char)*v)) v++;
    if (strncmp(v, "none", 4) == 0) {
        paint->kind = PAINT_NONE;
    } else if (strncmp(v, "url(", 4) == 0) {
        const char *s = v + 4;
        if (*s == '#') s++;
        int n = 0;
        while (*s && *s != ')' && n < ID_LEN - 1) paint->ref[n++] = *s++;
        paint->ref[n] = '\0';
        paint->kind = PAINT_REF;
    } else {
        Rgba c;
        if (parse_color(v, &c)) {
            paint->kind = PAINT_COLOR;
            paint->color = c;
        }
    }
}

static Matrix parse_transform(const char *v) {
    Matrix m = mat_identity();
    const char *s = v, *end = v + strlen(v);
    while (s < end) {
        while (s < end && (isspace((unsigned char)*s) || *s == ',')) s++;
        const char *name = s;
        while (s < end && isalpha((unsigned char)*s)) s++;
        size_t nlen = (size_t)(s - name);
        while (s < end && isspace((unsigned char)*s)) s++;
        if (s >= end || *s != '(') break;
        s++;
        float args[6] = {0};
        int argc = 0;
        while (argc < 6 && parse_float(&s, end, &args[argc])) argc++;
        while (s < end && *s != ')') s++;
        if (s < end) s++;

        Matrix t = mat_identity();
        if (nlen == 9 && strncmp(name, "translate", 9) == 0) {
            t.e = args[0];
            t.f = argc > 1 ? args[1] : 0;
        } else if (nlen == 5 && strncmp(name, "scale", 5) == 0) {
            t.a = args[0];
            t.d = argc > 1 ? args[1] : args[0];
        } else if (nlen == 6 && strncmp(name, "rotate", 6) == 0) {
            float rad = args[0] * (float)M_PI / 180.0f;
            float cs = cosf(rad), sn = sinf(rad);
            Matrix r = {cs, sn, -sn, cs, 0, 0};
            if (argc >= 3) {
                Matrix to = {1, 0, 0, 1, args[1], args[2]};
                Matrix back = {1, 0, 0, 1, -args[1], -args[2]};
                t = mat_mul(mat_mul(to, r), back);
            } else {
                t = r;
            }
        } else if (nlen == 6 && strncmp(name, "matrix", 6) == 0 && argc == 6) {
            t.a = args[0], t.b = args[1], t.c = args[2], t.d = args[3], t.e = args[4], t.f = args[5];
        } else if (nlen == 5 && strncmp(name, "skewX", 5) == 0) {
            t.c = tanf(args[0] * (float)M_PI / 180.0f);
        } else if (nlen == 5 && strncmp(name, "skewY", 5) == 0) {
            t.b = tanf(args[0] * (float)M_PI / 180.0f);
        }
        m = mat_mul(m, t);
    }
    return m;
}

/* ------------------------------------------------------------------------ */
/* Attributes                                                               */
/* ------------------------------------------------------------------------ */

#define MAX_ATTRS 24
#define ATTR_VALUE_LEN 2048

typedef struct {
    char name[32];
    char *value;
} Attr;

typedef struct {
    Attr attrs[MAX_ATTRS];
    int count;
    char storage[MAX_ATTRS][ATTR_VALUE_LEN];
} AttrList;

static const char *attr_get(const AttrList *list, const char *name) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->attrs[i].name, name) == 0) return list->attrs[i].value;
    }
    return NULL;
}

static float attr_float(const AttrList *list, const char *name, float def) {
    const char *v = attr_get(list, name);
    if (!v) return def;
    return parse_length(v, 1.0f);
}

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static void apply_presentation(Style *st, const char *name, const char *value) {
    if (strcmp(name, "fill") == 0) parse_paint(value, &st->fill);
    else if (strcmp(name, "stroke") == 0) parse_paint(value, &st->stroke);
    else if (strcmp(name, "fill-opacity") == 0) st->fill_opacity = clamp01(parse_length(value, 1.0f));
    else if (strcmp(name, "stroke-opacity") == 0) st->stroke_opacity = clamp01(parse_length(value, 1.0f));
    else if (strcmp(name, "stroke-width") == 0) st->stroke_width = parse_length(value, 1.0f);
    else if (strcmp(name, "stroke-miterlimit") == 0) st->miter_limit = parse_length(value, 1.0f);
    else if (strcmp(name, "stroke-linecap") == 0) {
        if (strstr(value, "round")) st->cap = CAP_ROUND;
        else if (strstr(value, "square")) st->cap = CAP_SQUARE;
        else st->cap = CAP_BUTT;
    } else if (strcmp(name, "stroke-linejoin") == 0) {
        if (strstr(value, "round")) st->join = JOIN_ROUND;
        else if (strstr(value, "bevel")) st->join = JOIN_BEVEL;
        else st->join = JOIN_MITER;
    } else if (strcmp(name, "fill-rule") == 0) {
        st->fill_rule = strstr(value, "evenodd") ? FILL_EVENODD : FILL_NONZERO;
    } else if (strcmp(name, "opacity") == 0) {
        st->opacity *= clamp01(parse_length(value, 1.0f));
    }
}

static void apply_style_attr(Style *st, const char *style) {
    const char *s = style;
    while (*s) {
        while (*s == ' ' || *s == ';') s++;
        const char *colon = strchr(s, ':');
        if (!colon) break;
        const char *semi = strchr(colon, ';');
        if (!semi) semi = colon + strlen(colon);
        char name[32], value[256];
        size_t nl = (size_t)(colon - s), vl = (size_t)(semi - colon - 1);
        while (nl > 0 && isspace((unsigned char)s[nl - 1])) nl--;
        if (nl >= sizeof(name)) nl = sizeof(name) - 1;
        if (vl >= sizeof(value)) vl = sizeof(value) - 1;
        memcpy(name, s, nl);
        name[nl] = '\0';
        memcpy(value, colon + 1, vl);
        value[vl] = '\0';
        apply_presentation(st, name, value);
        s = *semi ? semi + 1 : semi;
    }
}

/* ------------------------------------------------------------------------ */
/* Geometry building (user space -> device space)                           */
/* ------------------------------------------------------------------------ */

typedef struct {
    Path path;
    Matrix m;
    float bx0, by0, bx1, by1; /* user-space bounds */
    Vec2 cur, start;          /* user-space current/subpath start */
} Builder;

static void b_track(Builder *b, float x, float y) {
    if (x < b->bx0) b->bx0 = x;
    if (y < b->by0) b->by0 = y;
    if (x > b->bx1) b->bx1 = x;
    if (y > b->by1) b->by1 = y;
}

static void b_move(Builder *b, float x, float y) {
    Vec2 d = mat_apply(b->m, x, y);
    path_move_to(&b->path, d.x, d.y);
    b->cur = b->start = vec2(x, y);
    b_track(b, x, y);
}

static void b_line(Builder *b, float x, float y) {
    Vec2 d = mat_apply(b->m, x, y);
    path_line_to(&b->path, d.x, d.y);
    b->cur = vec2(x, y);
    b_track(b, x, y);
}

static void b_cubic(Builder *b, float x1, float y1, float x2, float y2, float x, float y) {
    Vec2 c1 = mat_apply(b->m, x1, y1), c2 = mat_apply(b->m, x2, y2), p = mat_apply(b->m, x, y);
    path_cubic_to(&b->path, c1.x, c1.y, c2.x, c2.y, p.x, p.y);
    b->cur = vec2(x, y);
    b_track(b, x1, y1);
    b_track(b, x2, y2);
    b_track(b, x, y);
}

static void b_quad(Builder *b, float x1, float y1, float x, float y) {
    Vec2 c = mat_apply(b->m, x1, y1), p = mat_apply(b->m, x, y);
    path_quad_to(&b->path, c.x, c.y, p.x, p.y);
    b->cur = vec2(x, y);
    b_track(b, x1, y1);
    b_track(b, x, y);
}

static void b_close(Builder *b) {
    path_close(&b->path);
    b->cur = b->start;
}

static float mat_scale(Matrix m) { return sqrtf(fabsf(m.a * m.d - m.b * m.c)); }

static float vec_angle(float ux, float uy, float vx, float vy) {
    float dot = ux * vx + uy * vy;
    float len = sqrtf((ux * ux + uy * uy) * (vx * vx + vy * vy));
    float c = len > 0 ? dot / len : 1.0f;
    if (c > 1) c = 1;
    if (c < -1) c = -1;
    float a = acosf(c);
    return (ux * vy - uy * vx) < 0 ? -a : a;
}

/* SVG elliptical arc, endpoint parameterization (SVG 1.1 F.6.5). */
static void b_arc(Builder *b, float rx, float ry, float phi_deg, bool large, bool sweep, float x, float y) {
    float x1 = b->cur.x, y1 = b->cur.y;
    if (x1 == x && y1 == y) return;
    rx = fabsf(rx);
    ry = fabsf(ry);
    if (rx < 1e-6f || ry < 1e-6f) {
        b_line(b, x, y);
        return;
    }
    float phi = phi_deg * (float)M_PI / 180.0f;
    float cp = cosf(phi), sp = sinf(phi);
    float dx2 = (x1 - x) * 0.5f, dy2 = (y1 - y) * 0.5f;
    float x1p = cp * dx2 + sp * dy2;
    float y1p = -sp * dx2 + cp * dy2;

    float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lambda > 1) {
        float s = sqrtf(lambda);
        rx *= s;
        ry *= s;
    }
    float num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    float coef = den > 0 ? sqrtf(fmaxf(num / den, 0.0f)) : 0.0f;
    if (large == sweep) coef = -coef;
    float cxp = coef * rx * y1p / ry;
    float cyp = -coef * ry * x1p / rx;
    float cx = cp * cxp - sp * cyp + (x1 + x) * 0.5f;
    float cy = sp * cxp + cp * cyp + (y1 + y) * 0.5f;

    float theta1 = vec_angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dtheta = vec_angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dtheta > 0) dtheta -= 2.0f * (float)M_PI;
    else if (sweep && dtheta < 0) dtheta += 2.0f * (float)M_PI;

    float scale = mat_scale(b->m);
    int n = arc_segments((rx > ry ? rx : ry) * scale, dtheta);
    for (int i = 1; i <= n; i++) {
        float t = theta1 + dtheta * (float)i / (float)n;
        float ex = rx * cosf(t), ey = ry * sinf(t);
        float px = cp * ex - sp * ey + cx;
        float py = sp * ex + cp * ey + cy;
        if (i == n) px = x, py = y;
        b_line(b, px, py);
    }
}

static void b_ellipse(Builder *b, float cx, float cy, float rx, float ry) {
    if (rx <= 0 || ry <= 0) return;
    float kx = rx * KAPPA, ky = ry * KAPPA;
    b_move(b, cx + rx, cy);
    b_cubic(b, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    b_cubic(b, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    b_cubic(b, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    b_cubic(b, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    b_close(b);
}

static void b_rect(Builder *b, float x, float y, float w, float h, float rx, float ry) {
    if (w <= 0 || h <= 0) return;
    if (rx > w * 0.5f) rx = w * 0.5f;
    if (ry > h * 0.5f) ry = h * 0.5f;
    if (rx <= 0 || ry <= 0) {
        b_move(b, x, y);
        b_line(b, x + w, y);
        b_line(b, x + w, y + h);
        b_line(b, x, y + h);
        b_close(b);
        return;
    }
    float kx = rx * KAPPA, ky = ry * KAPPA;
    b_move(b, x + rx, y);
    b_line(b, x + w - rx, y);
    b_cubic(b, x + w - rx + kx, y, x + w, y + ry - ky, x + w, y + ry);
    b_line(b, x + w, y + h - ry);
    b_cubic(b, x + w, y + h - ry + ky, x + w - rx + kx, y + h, x + w - rx, y + h);
    b_line(b, x + rx, y + h);
    b_cubic(b, x + rx - kx, y + h, x, y + h - ry + ky, x, y + h - ry);
    b_line(b, x, y + ry);
    b_cubic(b, x, y + ry - ky, x + rx - kx, y, x + rx, y);
    b_close(b);
}

static void b_path_data(Builder *b, const char *d) {
    const char *s = d, *end = d + strlen(d);
    char cmd = 0;
    float lcx = 0, lcy = 0; /* last control point for S/T */
    char last_cmd = 0;
    while (true) {
        s = skip_sep(s, end);
        if (s >= end) break;
        if (isalpha((unsigned char)*s)) {
            cmd = *s++;
        } else if (cmd == 0) {
            break;
        }
        bool rel = islower((unsigned char)cmd) != 0;
        float ox = rel ? b->cur.x : 0, oy = rel ? b->cur.y : 0;
        char up = (char)toupper((unsigned char)cmd);
        float v[7];
        bool ok = true;

        switch (up) {
        case 'M':
            if (!(parse_float(&s, end, &v[0]) && parse_float(&s, end, &v[1]))) {
                ok = false;
                break;
            }
            b_move(b, ox + v[0], oy + v[1]);
            cmd = rel ? 'l' : 'L'; /* subsequent pairs are implicit lineto */
            break;
        case 'L':
            if (!(parse_float(&s, end, &v[0]) && parse_float(&s, end, &v[1]))) {
                ok = false;
                break;
            }
            b_line(b, ox + v[0], oy + v[1]);
            break;
        case 'H':
            if (!parse_float(&s, end, &v[0])) {
                ok = false;
                break;
            }
            b_line(b, ox + v[0], b->cur.y);
            break;
        case 'V':
            if (!parse_float(&s, end, &v[0])) {
                ok = false;
                break;
            }
            b_line(b, b->cur.x, oy + v[0]);
            break;
        case 'C':
            for (int i = 0; i < 6 && ok; i++) ok = parse_float(&s, end, &v[i]);
            if (!ok) break;
            b_cubic(b, ox + v[0], oy + v[1], ox + v[2], oy + v[3], ox + v[4], oy + v[5]);
            lcx = ox + v[2];
            lcy = oy + v[3];
            break;
        case 'S': {
            for (int i = 0; i < 4 && ok; i++) ok = parse_float(&s, end, &v[i]);
            if (!ok) break;
            char lu = (char)toupper((unsigned char)last_cmd);
            float c1x = b->cur.x, c1y = b->cur.y;
            if (lu == 'C' || lu == 'S') {
                c1x = 2 * b->cur.x - lcx;
                c1y = 2 * b->cur.y - lcy;
            }
            b_cubic(b, c1x, c1y, ox + v[0], oy + v[1], ox + v[2], oy + v[3]);
            lcx = ox + v[0];
            lcy = oy + v[1];
            break;
        }
        case 'Q':
            for (int i = 0; i < 4 && ok; i++) ok = parse_float(&s, end, &v[i]);
            if (!ok) break;
            b_quad(b, ox + v[0], oy + v[1], ox + v[2], oy + v[3]);
            lcx = ox + v[0];
            lcy = oy + v[1];
            break;
        case 'T': {
            for (int i = 0; i < 2 && ok; i++) ok = parse_float(&s, end, &v[i]);
            if (!ok) break;
            char lu = (char)toupper((unsigned char)last_cmd);
            float cx = b->cur.x, cy = b->cur.y;
            if (lu == 'Q' || lu == 'T') {
                cx = 2 * b->cur.x - lcx;
                cy = 2 * b->cur.y - lcy;
            }
            b_quad(b, cx, cy, ox + v[0], oy + v[1]);
            lcx = cx;
            lcy = cy;
            break;
        }
        case 'A': {
            bool large = false, sweep = false;
            ok = parse_float(&s, end, &v[0]) && parse_float(&s, end, &v[1]) && parse_float(&s, end, &v[2]) &&
                 parse_flag(&s, end, &large) && parse_flag(&s, end, &sweep) && parse_float(&s, end, &v[3]) &&
                 parse_float(&s, end, &v[4]);
            if (!ok) break;
            b_arc(b, v[0], v[1], v[2], large, sweep, ox + v[3], oy + v[4]);
            break;
        }
        case 'Z':
            b_close(b);
            /* A following command without a moveto starts at the subpath start. */
            {
                Vec2 st = b->start;
                const char *peek = skip_sep(s, end);
                if (peek < end && toupper((unsigned char)*peek) != 'M') b_move(b, st.x, st.y);
            }
            break;
        default: ok = false; break;
        }
        if (!ok) break;
        last_cmd = cmd;
    }
}

static void b_points(Builder *b, const char *v, bool close) {
    const char *s = v, *end = v + strlen(v);
    float x, y;
    bool first = true;
    while (parse_float(&s, end, &x) && parse_float(&s, end, &y)) {
        if (first) b_move(b, x, y);
        else b_line(b, x, y);
        first = false;
    }
    if (close && !first) b_close(b);
}

/* ------------------------------------------------------------------------ */
/* Painting shapes                                                          */
/* ------------------------------------------------------------------------ */

static Gradient *find_gradient(Renderer *r, const char *id) {
    for (int i = 0; i < r->gradient_count; i++) {
        if (strcmp(r->gradients[i].id, id) == 0) return &r->gradients[i];
    }
    return NULL;
}

static bool make_paint(Renderer *r, const SvgPaint *sp, float opacity, const Builder *b, Paint *out) {
    if (sp->kind == PAINT_NONE) return false;
    if (sp->kind == PAINT_COLOR) {
        *out = paint_solid(sp->color);
        out->opacity = opacity;
        return true;
    }
    Gradient *g = find_gradient(r, sp->ref);
    if (!g || g->stop_count == 0) return false;
    *out = paint_solid(g->colors[0]);
    out->opacity = opacity;
    if (g->stop_count == 1) return true;

    Matrix unit = mat_identity();
    if (!g->user_space) {
        float bw = b->bx1 - b->bx0, bh = b->by1 - b->by0;
        if (bw <= 0 || bh <= 0) return true;
        Matrix bbox = {bw, 0, 0, bh, b->bx0, b->by0};
        unit = bbox;
    }
    Matrix inv = mat_invert(mat_mul(b->m, unit));
    float gx = g->x2 - g->x1, gy = g->y2 - g->y1;
    float l2 = gx * gx + gy * gy;
    if (l2 <= 1e-12f) return true;
    float kx = gx / l2, ky = gy / l2;
    out->gradient = 1;
    out->ga = kx * inv.a + ky * inv.b;
    out->gb = kx * inv.c + ky * inv.d;
    out->gc = kx * inv.e + ky * inv.f - (kx * g->x1 + ky * g->y1);
    out->stop_count = g->stop_count;
    for (int i = 0; i < g->stop_count; i++) {
        out->stop_offset[i] = g->offsets[i];
        out->stop_color[i] = g->colors[i];
    }
    return true;
}

static void paint_builder(Renderer *r, const Style *st, Builder *b) {
    if (b->path.count == 0) return;
    Paint paint;
    if (make_paint(r, &st->fill, st->fill_opacity * st->opacity, b, &paint)) {
        raster_fill_path(&r->canvas, &b->path, st->fill_rule, &paint);
    }
    if (st->stroke_width > 0 && make_paint(r, &st->stroke, st->stroke_opacity * st->opacity, b, &paint)) {
        StrokeStyle ss;
        ss.width = st->stroke_width * mat_scale(b->m);
        ss.cap = st->cap;
        ss.join = st->join;
        ss.miter_limit = st->miter_limit;
        raster_stroke_path(&r->canvas, &b->path, &ss, &paint);
    }
}

static void render_shape(Renderer *r, const char *name, const AttrList *attrs, const Style *st) {
    Builder b;
    path_init(&b.path);
    b.m = st->m;
    b.bx0 = b.by0 = 1e30f;
    b.bx1 = b.by1 = -1e30f;
    b.cur = b.start = vec2(0, 0);

    if (strcmp(name, "path") == 0) {
        const char *d = attr_get(attrs, "d");
        if (d) b_path_data(&b, d);
    } else if (strcmp(name, "rect") == 0) {
        float rx = attr_float(attrs, "rx", -1), ry = attr_float(attrs, "ry", -1);
        if (rx < 0) rx = ry;
        if (ry < 0) ry = rx;
        if (rx < 0) rx = ry = 0;
        b_rect(&b, attr_float(attrs, "x", 0), attr_float(attrs, "y", 0), attr_float(attrs, "width", 0),
               attr_float(attrs, "height", 0), rx, ry);
    } else if (strcmp(name, "circle") == 0) {
        float rr = attr_float(attrs, "r", 0);
        b_ellipse(&b, attr_float(attrs, "cx", 0), attr_float(attrs, "cy", 0), rr, rr);
    } else if (strcmp(name, "ellipse") == 0) {
        b_ellipse(&b, attr_float(attrs, "cx", 0), attr_float(attrs, "cy", 0), attr_float(attrs, "rx", 0),
                  attr_float(attrs, "ry", 0));
    } else if (strcmp(name, "line") == 0) {
        b_move(&b, attr_float(attrs, "x1", 0), attr_float(attrs, "y1", 0));
        b_line(&b, attr_float(attrs, "x2", 0), attr_float(attrs, "y2", 0));
    } else if (strcmp(name, "polyline") == 0 || strcmp(name, "polygon") == 0) {
        const char *pts = attr_get(attrs, "points");
        if (pts) b_points(&b, pts, name[4] == 'g');
    }

    Style local = *st;
    if (strcmp(name, "line") == 0 || strcmp(name, "polyline") == 0) {
        /* Open shapes are never filled in practice; keep SVG semantics but
         * avoid filling degenerate 2-point paths. */
        if (b.path.count < 3) local.fill.kind = PAINT_NONE;
    }
    paint_builder(r, &local, &b);
    path_free(&b.path);
}

/* ------------------------------------------------------------------------ */
/* XML walking                                                              */
/* ------------------------------------------------------------------------ */

static bool is_container_hidden(const char *name) {
    return strcmp(name, "defs") == 0 || strcmp(name, "filter") == 0 || strcmp(name, "title") == 0 ||
           strcmp(name, "desc") == 0 || strcmp(name, "clipPath") == 0 || strcmp(name, "mask") == 0 ||
           strcmp(name, "symbol") == 0 || strcmp(name, "metadata") == 0 || strcmp(name, "pattern") == 0 ||
           strcmp(name, "linearGradient") == 0 || strcmp(name, "radialGradient") == 0 || strcmp(name, "style") == 0;
}

static void handle_svg_root(Renderer *r, const AttrList *attrs, Style *st) {
    float vx = 0, vy = 0, vw = 0, vh = 0;
    const char *vb = attr_get(attrs, "viewBox");
    if (vb) {
        const char *s = vb, *end = vb + strlen(vb);
        parse_float(&s, end, &vx);
        parse_float(&s, end, &vy);
        parse_float(&s, end, &vw);
        parse_float(&s, end, &vh);
    }
    if (vw <= 0 || vh <= 0) {
        vw = attr_float(attrs, "width", (float)r->width);
        vh = attr_float(attrs, "height", (float)r->height);
    }
    if (vw <= 0) vw = (float)r->width;
    if (vh <= 0) vh = (float)r->height;
    Matrix m = {(float)r->width / vw, 0, 0, (float)r->height / vh, 0, 0};
    m.e = -vx * m.a;
    m.f = -vy * m.d;
    st->m = mat_mul(st->m, m);
}

static void handle_element(Renderer *r, const char *name, const AttrList *attrs, bool self_closing) {
    Style parent = r->stack[r->depth];
    Style st = parent;

    for (int i = 0; i < attrs->count; i++) {
        if (strcmp(attrs->attrs[i].name, "style") != 0 && strcmp(attrs->attrs[i].name, "transform") != 0)
            apply_presentation(&st, attrs->attrs[i].name, attrs->attrs[i].value);
    }
    const char *style = attr_get(attrs, "style");
    if (style) apply_style_attr(&st, style);
    const char *transform = attr_get(attrs, "transform");
    if (transform) st.m = mat_mul(st.m, parse_transform(transform));

    if (strcmp(name, "svg") == 0) {
        handle_svg_root(r, attrs, &st);
    } else if (strcmp(name, "linearGradient") == 0 && r->gradient_count < MAX_GRADIENTS) {
        Gradient *g = &r->gradients[r->gradient_count++];
        memset(g, 0, sizeof(*g));
        const char *id = attr_get(attrs, "id");
        if (id) {
            size_t il = strlen(id);
            if (il >= ID_LEN) il = ID_LEN - 1;
            memcpy(g->id, id, il);
            g->id[il] = '\0';
        }
        const char *units = attr_get(attrs, "gradientUnits");
        g->user_space = units && strcmp(units, "userSpaceOnUse") == 0;
        const char *v;
        g->x1 = (v = attr_get(attrs, "x1")) ? parse_length(v, 1.0f) : 0;
        g->y1 = (v = attr_get(attrs, "y1")) ? parse_length(v, 1.0f) : 0;
        g->x2 = (v = attr_get(attrs, "x2")) ? parse_length(v, 1.0f) : 1;
        g->y2 = (v = attr_get(attrs, "y2")) ? parse_length(v, 1.0f) : 0;
        r->open_gradient = self_closing ? NULL : g;
    } else if (strcmp(name, "stop") == 0 && r->open_gradient) {
        Gradient *g = r->open_gradient;
        if (g->stop_count < PAINT_MAX_STOPS) {
            const char *v = attr_get(attrs, "offset");
            float off = v ? parse_length(v, 1.0f) : 0;
            Rgba col = rgb(0, 0, 0);
            float op = 1.0f;
            if ((v = attr_get(attrs, "stop-color"))) parse_color(v, &col);
            if ((v = attr_get(attrs, "stop-opacity"))) op = clamp01(parse_length(v, 1.0f));
            if (style) {
                const char *sc = strstr(style, "stop-color:");
                if (sc) parse_color(sc + 11, &col);
            }
            col.a = (uint8_t)(op * 255.0f + 0.5f);
            if (g->stop_count > 0 && off < g->offsets[g->stop_count - 1]) off = g->offsets[g->stop_count - 1];
            g->offsets[g->stop_count] = clamp01(off);
            g->colors[g->stop_count] = col;
            g->stop_count++;
        }
    } else if (!st.hidden && (strcmp(name, "path") == 0 || strcmp(name, "rect") == 0 || strcmp(name, "circle") == 0 ||
                              strcmp(name, "ellipse") == 0 || strcmp(name, "line") == 0 ||
                              strcmp(name, "polyline") == 0 || strcmp(name, "polygon") == 0)) {
        render_shape(r, name, attrs, &st);
    }

    if (is_container_hidden(name)) st.hidden = true;

    if (!self_closing && r->depth + 1 < MAX_DEPTH) {
        r->depth++;
        r->stack[r->depth] = st;
    }
}

static void handle_end(Renderer *r, const char *name) {
    if (strcmp(name, "linearGradient") == 0) r->open_gradient = NULL;
    if (r->depth > 0) r->depth--;
}

static void walk(Renderer *r, const char *data, size_t len) {
    const char *s = data, *end = data + len;
    AttrList *attrs = (AttrList *)malloc(sizeof(AttrList));
    if (!attrs) return;

    while (s < end) {
        const char *lt = memchr(s, '<', (size_t)(end - s));
        if (!lt) break;
        s = lt + 1;
        if (s >= end) break;

        if (end - s >= 3 && strncmp(s, "!--", 3) == 0) {
            const char *close = s + 3;
            while (close + 2 < end && strncmp(close, "-->", 3) != 0) close++;
            s = close + 3;
            continue;
        }
        if (*s == '?' || *s == '!') {
            const char *gt = memchr(s, '>', (size_t)(end - s));
            s = gt ? gt + 1 : end;
            continue;
        }
        if (*s == '/') {
            s++;
            char name[32];
            int n = 0;
            while (s < end && !isspace((unsigned char)*s) && *s != '>' && n < 31) name[n++] = *s++;
            name[n] = '\0';
            const char *gt = memchr(s, '>', (size_t)(end - s));
            s = gt ? gt + 1 : end;
            handle_end(r, name);
            continue;
        }

        char name[32];
        int n = 0;
        while (s < end && !isspace((unsigned char)*s) && *s != '>' && *s != '/' && n < 31) name[n++] = *s++;
        name[n] = '\0';
        /* Strip namespace prefixes such as svg:path. */
        char *colon = strchr(name, ':');
        if (colon) memmove(name, colon + 1, strlen(colon));

        attrs->count = 0;
        bool self_closing = false;
        while (s < end) {
            while (s < end && isspace((unsigned char)*s)) s++;
            if (s >= end) break;
            if (*s == '>') {
                s++;
                break;
            }
            if (*s == '/' && s + 1 < end && s[1] == '>') {
                self_closing = true;
                s += 2;
                break;
            }
            char aname[32];
            int an = 0;
            while (s < end && *s != '=' && !isspace((unsigned char)*s) && *s != '>' && *s != '/' && an < 31)
                aname[an++] = *s++;
            aname[an] = '\0';
            while (s < end && isspace((unsigned char)*s)) s++;
            if (s >= end || *s != '=') {
                if (an == 0) s++;
                continue;
            }
            s++;
            while (s < end && isspace((unsigned char)*s)) s++;
            if (s >= end) break;
            char quote = *s;
            if (quote != '"' && quote != '\'') continue;
            s++;
            const char *vstart = s;
            while (s < end && *s != quote) s++;
            size_t vl = (size_t)(s - vstart);
            if (s < end) s++;
            if (attrs->count < MAX_ATTRS) {
                Attr *a = &attrs->attrs[attrs->count];
                char *storage = attrs->storage[attrs->count];
                if (vl >= ATTR_VALUE_LEN) vl = ATTR_VALUE_LEN - 1;
                memcpy(storage, vstart, vl);
                storage[vl] = '\0';
                const char *nm = strchr(aname, ':');
                const char *src = nm && strncmp(aname, "xlink", 5) != 0 ? nm + 1 : aname;
                size_t nl = strlen(src);
                if (nl >= sizeof(a->name)) nl = sizeof(a->name) - 1;
                memcpy(a->name, src, nl);
                a->name[nl] = '\0';
                a->value = storage;
                attrs->count++;
            }
        }
        handle_element(r, name, attrs, self_closing);
    }
    free(attrs);
}

bool svg_render(const char *data, size_t len, int width, int height, Image *out) {
    if (width <= 0 || height <= 0) return false;
    if (!image_alloc(out, width, height)) return false;

    Renderer *r = (Renderer *)calloc(1, sizeof(Renderer));
    if (!r) {
        image_free(out);
        return false;
    }
    canvas_from_image(&r->canvas, out);
    r->width = width;
    r->height = height;
    Style *root = &r->stack[0];
    root->m = mat_identity();
    root->fill.kind = PAINT_COLOR;
    root->fill.color = rgb(0, 0, 0);
    root->stroke.kind = PAINT_NONE;
    root->fill_opacity = root->stroke_opacity = root->opacity = 1.0f;
    root->stroke_width = 1.0f;
    root->miter_limit = 4.0f;
    root->cap = CAP_BUTT;
    root->join = JOIN_MITER;
    root->fill_rule = FILL_NONZERO;
    root->hidden = false;

    walk(r, data, len);
    free(r);
    return true;
}
