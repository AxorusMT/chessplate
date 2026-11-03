#ifndef CHESSPLATE_APP_H
#define CHESSPLATE_APP_H

#include <stdbool.h>

#include "platform.h"
#include "raster.h"

#define APP_WINDOW_WIDTH 920
#define APP_WINDOW_HEIGHT 680
#define APP_MIN_WIDTH 820
#define APP_MIN_HEIGHT 620

typedef struct App App;
typedef void (*ClipboardFn)(const char *utf8);

App *app_create(ClipboardFn set_clipboard);
void app_destroy(App *app);

/* Processes input and renders one frame into the canvas. */
void app_frame(App *app, Canvas *canvas, InputState *input);

/* How long the main loop may sleep before the next frame is needed:
 * -1 = until the next input event. */
int app_wait_timeout_ms(const App *app);

/* Renders the application icon (logo) at size x size. */
bool app_render_icon(int size, Image *out);

/* ---- Hooks used by the automated tests ---------------------------------- */
bool app_is_thinking(const App *app);
int app_move_count(const App *app);
/* Blocks until the engine finishes its current search (tests only). */
void app_wait_for_engine(App *app);

#endif
