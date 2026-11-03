#include <stdio.h>
#include <stdlib.h>

#include "app.h"
#include "platform.h"
#include "raster.h"

#define ICON_SIZE 256

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int chessplate_main(void) {
    Image icon = {0};
    app_render_icon(ICON_SIZE, &icon);

    if (!platform_init("Chessplate", APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT, APP_MIN_WIDTH, APP_MIN_HEIGHT, &icon)) {
        image_free(&icon);
        return 1;
    }
    image_free(&icon);

    App *app = app_create(platform_set_clipboard);
    if (!app) {
        platform_shutdown();
        return 1;
    }

    InputState input = {0};
    uint32_t *framebuffer = NULL;
    int fb_w = 0, fb_h = 0;

    bool had_input = true;
    for (;;) {
        input_begin_frame(&input);
        /* Input can change state after parts of the frame were drawn, so
         * follow every input frame with one more. */
        platform_wait_events(&input, had_input ? 0 : app_wait_timeout_ms(app));
        had_input = input_had_activity(&input);
        if (input.quit) break;

        int w, h;
        platform_window_size(&w, &h);
        if (w != fb_w || h != fb_h || !framebuffer) {
            uint32_t *fb = (uint32_t *)realloc(framebuffer, (size_t)w * (size_t)h * sizeof(uint32_t));
            if (!fb) break;
            framebuffer = fb;
            fb_w = w;
            fb_h = h;
        }

        Canvas canvas;
        canvas_init(&canvas, framebuffer, fb_w, fb_h);
        app_frame(app, &canvas, &input);
        platform_present(framebuffer, fb_w, fb_h);
    }

    free(framebuffer);
    app_destroy(app);
    platform_shutdown();
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmd, int show) {
    (void)instance;
    (void)prev;
    (void)cmd;
    (void)show;
    return chessplate_main();
}
#endif

int main(void) { return chessplate_main(); }
