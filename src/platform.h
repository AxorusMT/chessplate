#ifndef CHESSPLATE_PLATFORM_H
#define CHESSPLATE_PLATFORM_H

/* Native windowing backend: X11 (platform_x11.c) or Win32 (platform_win32.c).
 * The application renders into a 32-bit premultiplied ARGB framebuffer that
 * the backend presents as-is. */

#include <stdbool.h>
#include <stdint.h>

#include "raster.h"

enum { MOUSE_LEFT = 0, MOUSE_RIGHT = 1, MOUSE_MIDDLE = 2, MOUSE_BUTTONS = 3 };

typedef struct {
    float mouse_x, mouse_y;
    bool mouse_inside;
    bool down[MOUSE_BUTTONS];
    /* Edge-triggered state, cleared by input_begin_frame(). */
    bool pressed[MOUSE_BUTTONS];
    bool released[MOUSE_BUTTONS];
    float wheel; /* > 0 scrolls content up (towards the start) */
    bool key_escape;
    bool quit;
} InputState;

static inline void input_begin_frame(InputState *in) {
    for (int i = 0; i < MOUSE_BUTTONS; i++) in->pressed[i] = in->released[i] = false;
    in->wheel = 0;
    in->key_escape = false;
}

static inline bool input_had_activity(const InputState *in) {
    for (int i = 0; i < MOUSE_BUTTONS; i++)
        if (in->pressed[i] || in->released[i]) return true;
    return in->wheel != 0 || in->key_escape;
}

bool platform_init(const char *title, int width, int height, int min_width, int min_height, const Image *icon);
void platform_shutdown(void);

/* Blocks until at least one event arrives or timeout_ms elapses
 * (timeout_ms < 0 waits indefinitely), then drains pending events. */
void platform_wait_events(InputState *in, int timeout_ms);

void platform_window_size(int *width, int *height);
void platform_present(const uint32_t *pixels, int width, int height);
void platform_set_clipboard(const char *utf8);

#endif
