#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "text.h"

#define BUTTON_PAD_X 8.0f
#define SCROLLBAR_W 8.0f

static bool g_popup_opened_this_frame;

void ui_begin_frame(Ui *ui, Canvas *canvas, InputState *in) {
    ui->canvas = canvas;
    ui->in = in;
    ui->blocked = ui->popup_id != 0;
    g_popup_opened_this_frame = false;
}

void ui_end_frame(Ui *ui) {
    if (ui->active_id && !ui->in->down[MOUSE_LEFT]) ui->active_id = 0;
}

bool ui_hovered(const Ui *ui, Rect r) {
    return !ui->blocked && ui->in->mouse_inside && rect_contains(r, ui->in->mouse_x, ui->in->mouse_y);
}

bool ui_click_behavior(Ui *ui, uint32_t id, Rect r, bool *hovered, bool *held) {
    bool hov = ui_hovered(ui, r);
    if (hov && ui->in->pressed[MOUSE_LEFT]) ui->active_id = id;
    bool clicked = false;
    if (ui->active_id == id && ui->in->released[MOUSE_LEFT]) {
        clicked = hov;
        ui->active_id = 0;
    }
    if (hovered) *hovered = hov;
    if (held) *held = ui->active_id == id && ui->in->down[MOUSE_LEFT];
    return clicked;
}

float ui_label(Ui *ui, float x, float y, FontId font, const char *text, Rgba color) {
    draw_text(ui->canvas, font, x, y, ALIGN_LEFT_TOP, text, color);
    return y + (float)text_line_height(font);
}

float ui_label_wrapped(Ui *ui, float x, float y, float width, FontId font, const char *text, Rgba color) {
    return y + draw_text_wrapped(ui->canvas, font, x, y, width, text, color) - 2.0f;
}

void ui_separator(Ui *ui, float x, float y, float width) { draw_rect(ui->canvas, x, y, x + width, y + 1.0f, UI_STROKE); }

float ui_button_width(FontId font, const char *label) { return ceilf(text_width(font, label)) + 2.0f * BUTTON_PAD_X; }

static void draw_widget_frame(Ui *ui, Rect r, bool hovered, bool held) {
    Rgba fill = held ? UI_WIDGET_ACTIVE : (hovered ? UI_WIDGET_HOVER : UI_WIDGET);
    draw_round_rect(ui->canvas, r.x0, r.y0, r.x1, r.y1, 3.0f, fill);
    if (hovered || held) draw_round_rect_stroke(ui->canvas, r.x0 + 0.5f, r.y0 + 0.5f, r.x1 - 0.5f, r.y1 - 0.5f, 3.0f, 1.0f, UI_WIDGET_STROKE_HOVER);
}

bool ui_button(Ui *ui, uint32_t id, Rect r, FontId font, const char *label) {
    bool hovered, held;
    bool clicked = ui_click_behavior(ui, id, r, &hovered, &held);
    draw_widget_frame(ui, r, hovered, held);
    draw_text(ui->canvas, font, (r.x0 + r.x1) * 0.5f, (r.y0 + r.y1) * 0.5f, ALIGN_CENTER_CENTER, label,
              hovered ? UI_TEXT_STRONG : rgb(200, 200, 200));
    return clicked;
}

bool ui_slider_int(Ui *ui, uint32_t id, Rect track, int *value, int min, int max) {
    float cy = (track.y0 + track.y1) * 0.5f;
    float hx0 = track.x0 + 7.0f, hx1 = track.x1 - 7.0f;
    bool hovered = ui_hovered(ui, track);
    if (hovered && ui->in->pressed[MOUSE_LEFT]) ui->active_id = id;
    bool held = ui->active_id == id && ui->in->down[MOUSE_LEFT];
    int old = *value;
    if (held && hx1 > hx0) {
        float t = (ui->in->mouse_x - hx0) / (hx1 - hx0);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        *value = min + (int)lroundf(t * (float)(max - min));
    }
    if (hovered && ui->in->wheel != 0.0f && !held) {
        *value += ui->in->wheel > 0 ? 10 : -10;
    }
    if (*value < min) *value = min;
    if (*value > max) *value = max;

    float t = max > min ? (float)(*value - min) / (float)(max - min) : 0.0f;
    float hx = hx0 + t * (hx1 - hx0);
    draw_round_rect(ui->canvas, track.x0, cy - 2.5f, track.x1, cy + 2.5f, 2.5f, UI_WIDGET);
    draw_round_rect(ui->canvas, track.x0, cy - 2.5f, hx, cy + 2.5f, 2.5f, rgba(90, 170, 255, 150));
    Rgba knob = held ? rgb(220, 220, 220) : (hovered ? rgb(200, 200, 200) : rgb(170, 170, 170));
    draw_circle(ui->canvas, hx, cy, 7.0f, knob);
    draw_circle_stroke(ui->canvas, hx, cy, 7.0f, 1.0f, rgb(40, 40, 40));
    return *value != old;
}

static void draw_truncated(Canvas *c, FontId font, float x, float y, float max_w, const char *text, Rgba color) {
    if (text_width(font, text) <= max_w) {
        draw_text(c, font, x, y, ALIGN_LEFT_TOP, text, color);
        return;
    }
    char buf[256];
    size_t len = strlen(text);
    if (len >= sizeof(buf) - 4) len = sizeof(buf) - 4;
    float dots = text_width(font, "...");
    while (len > 0 && text_width_n(font, text, (int)len) + dots > max_w) len--;
    memcpy(buf, text, len);
    memcpy(buf + len, "...", 4);
    draw_text(c, font, x, y, ALIGN_LEFT_TOP, buf, color);
}

bool ui_combo_header(Ui *ui, uint32_t id, Rect r, const char *text) {
    bool open = ui->popup_id == id;
    bool hovered = ui->in->mouse_inside && rect_contains(r, ui->in->mouse_x, ui->in->mouse_y) && (!ui->blocked || open);
    bool toggled = false;
    if (hovered && ui->in->pressed[MOUSE_LEFT]) {
        if (open) {
            ui->popup_id = 0;
        } else {
            ui->popup_id = id;
            g_popup_opened_this_frame = true;
        }
        toggled = true;
    }
    draw_widget_frame(ui, r, hovered, ui->popup_id == id);
    FontId font = FONT_SANS_13;
    float th = (float)text_line_height(font);
    draw_truncated(ui->canvas, font, r.x0 + 6.0f, (r.y0 + r.y1 - th) * 0.5f, rect_w(r) - 26.0f, text,
                   hovered ? UI_TEXT_STRONG : rgb(200, 200, 200));
    float cx = r.x1 - 11.0f, cy = (r.y0 + r.y1) * 0.5f;
    Vec2 tri[3];
    if (ui->popup_id == id) {
        tri[0] = vec2(cx - 4, cy + 2), tri[1] = vec2(cx + 4, cy + 2), tri[2] = vec2(cx, cy - 3);
    } else {
        tri[0] = vec2(cx - 4, cy - 2), tri[1] = vec2(cx + 4, cy - 2), tri[2] = vec2(cx, cy + 3);
    }
    draw_polygon(ui->canvas, tri, 3, rgb(200, 200, 200));
    return toggled;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void draw_scrollbar(Ui *ui, Rect bar, float view_h, float content_h, float scroll, bool hot) {
    if (content_h <= view_h) return;
    float thumb_h = fmaxf(view_h * view_h / content_h, 18.0f);
    float t = scroll / (content_h - view_h);
    float ty = bar.y0 + t * (rect_h(bar) - thumb_h);
    draw_round_rect(ui->canvas, bar.x0 + 1.5f, bar.y0, bar.x1 - 1.5f, bar.y1, 2.5f, rgba(255, 255, 255, 14));
    draw_round_rect(ui->canvas, bar.x0 + 1.5f, ty, bar.x1 - 1.5f, ty + thumb_h, 2.5f,
                    hot ? rgb(150, 150, 150) : rgb(95, 95, 95));
}

/* Shared scrollbar interaction. `inside` decides whether wheel events apply. */
static float scroll_input(Ui *ui, uint32_t bar_id, Rect view, Rect bar, float content_h, float scroll, bool inside,
                          bool bar_hover, bool *bar_held) {
    float view_h = rect_h(view);
    float max_scroll = content_h > view_h ? content_h - view_h : 0.0f;
    if (inside && ui->in->wheel != 0.0f) scroll -= ui->in->wheel * 48.0f;
    if (content_h > view_h && bar_hover && ui->in->pressed[MOUSE_LEFT]) ui->active_id = bar_id;
    *bar_held = ui->active_id == bar_id && ui->in->down[MOUSE_LEFT];
    if (*bar_held) {
        float thumb_h = fmaxf(view_h * view_h / content_h, 18.0f);
        float t = (ui->in->mouse_y - bar.y0 - thumb_h * 0.5f) / fmaxf(rect_h(bar) - thumb_h, 1.0f);
        scroll = clampf(t, 0.0f, 1.0f) * max_scroll;
    }
    return clampf(scroll, 0.0f, max_scroll);
}

float ui_scroll_region(Ui *ui, uint32_t id, Rect view, float content_h, float scroll) {
    Rect bar = rect_make(view.x1 - SCROLLBAR_W, view.y0, view.x1, view.y1);
    bool inside = ui_hovered(ui, view);
    bool bar_hover = ui_hovered(ui, bar);
    bool held;
    scroll = scroll_input(ui, id, view, bar, content_h, scroll, inside, bar_hover, &held);
    draw_scrollbar(ui, bar, rect_h(view), content_h, scroll, held || bar_hover);
    return scroll;
}

int ui_list_popup(Ui *ui, uint32_t id, Rect r, int item_count, float item_h, int selected, float *scroll,
                  UiItemText text_fn, void *user) {
    if (ui->popup_id != id) return -1;
    InputState *in = ui->in;
    ui->popup_rect = r;

    bool inside = in->mouse_inside && rect_contains(r, in->mouse_x, in->mouse_y);
    if (!g_popup_opened_this_frame && in->pressed[MOUSE_LEFT] && !inside) {
        ui->popup_id = 0;
        return -1;
    }
    if (in->key_escape) {
        ui->popup_id = 0;
        return -1;
    }

    Canvas *c = ui->canvas;
    draw_round_rect(c, r.x0 + 2, r.y0 + 4, r.x1 + 4, r.y1 + 6, 6.0f, rgba(0, 0, 0, 90));
    draw_round_rect(c, r.x0, r.y0, r.x1, r.y1, 4.0f, rgb(30, 30, 30));
    draw_round_rect_stroke(c, r.x0 + 0.5f, r.y0 + 0.5f, r.x1 - 0.5f, r.y1 - 0.5f, 4.0f, 1.0f, UI_STROKE);

    Rect view = rect_make(r.x0 + 3, r.y0 + 3, r.x1 - 3, r.y1 - 3);
    float content_h = (float)item_count * item_h;
    Rect bar = rect_make(view.x1 - SCROLLBAR_W, view.y0, view.x1, view.y1);
    bool bar_hover = in->mouse_inside && rect_contains(bar, in->mouse_x, in->mouse_y) && content_h > rect_h(view);
    bool held;
    *scroll = scroll_input(ui, id ^ 0x5c011u, view, bar, content_h, *scroll, inside, bar_hover, &held);

    int clicked = -1;
    float list_x1 = content_h > rect_h(view) ? bar.x0 - 2 : view.x1;
    int saved_x0 = c->clip_x0, saved_y0 = c->clip_y0, saved_x1 = c->clip_x1, saved_y1 = c->clip_y1;
    canvas_set_clip(c, view.x0, view.y0, view.x1, view.y1);
    int first = (int)(*scroll / item_h);
    for (int i = first; i < item_count; i++) {
        float y0 = view.y0 + (float)i * item_h - *scroll;
        if (y0 > view.y1) break;
        Rect row = rect_make(view.x0, y0, list_x1, y0 + item_h);
        bool hov = !held && in->mouse_inside && rect_contains(row, in->mouse_x, in->mouse_y) &&
                   rect_contains(view, in->mouse_x, in->mouse_y);
        if (i == selected) draw_round_rect(c, row.x0, row.y0, row.x1, row.y1, 3.0f, rgba(90, 170, 255, 90));
        else if (hov) draw_round_rect(c, row.x0, row.y0, row.x1, row.y1, 3.0f, UI_WIDGET_HOVER);
        draw_text(c, FONT_SANS_13, row.x0 + 6, row.y0 + (item_h - (float)text_line_height(FONT_SANS_13)) * 0.5f,
                  ALIGN_LEFT_TOP, text_fn(i, user), hov || i == selected ? UI_TEXT_STRONG : rgb(200, 200, 200));
        if (hov && in->pressed[MOUSE_LEFT] && !g_popup_opened_this_frame) clicked = i;
    }
    c->clip_x0 = saved_x0, c->clip_y0 = saved_y0, c->clip_x1 = saved_x1, c->clip_y1 = saved_y1;
    draw_scrollbar(ui, bar, rect_h(view), content_h, *scroll, held || bar_hover);

    if (clicked >= 0) ui->popup_id = 0;
    return clicked;
}
