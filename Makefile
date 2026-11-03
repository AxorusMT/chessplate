# Chessplate -- plain C99, no third-party libraries.
#
#   make            build ./build/chessplate (X11 on Linux/BSD)
#   make run        build and launch
#   make test       build and run the test suite (headless)
#   make windows    cross-compile build/chessplate.exe with MinGW-w64
#   make assets     regenerate src/assets.c from assets/
#   make clean

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
LDLIBS  ?=

BUILD   := build

CORE_SRC := src/chess.c src/engine.c src/insight.c src/metrics.c src/theme.c src/sys.c \
            src/raster.c src/svg.c src/text.c src/font_data.c src/assets.c src/ui.c src/app.c
APP_SRC  := $(CORE_SRC) src/main.c

X11_CFLAGS ?= $(shell pkg-config --cflags x11 2>/dev/null)
X11_LIBS   ?= $(shell pkg-config --libs x11 2>/dev/null || echo -lX11)

MINGW_CC ?= x86_64-w64-mingw32-gcc

.PHONY: all run test windows assets clean

all: $(BUILD)/chessplate

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/chessplate: $(APP_SRC) src/platform_x11.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) $(X11_CFLAGS) -o $@ $(APP_SRC) src/platform_x11.c $(X11_LIBS) -lpthread -lm $(LDLIBS)

run: $(BUILD)/chessplate
	./$(BUILD)/chessplate

$(BUILD)/chessplate_tests: $(CORE_SRC) tests/tests.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE_SRC) tests/tests.c -lpthread -lm $(LDLIBS)

test: $(BUILD)/chessplate_tests
	./$(BUILD)/chessplate_tests

windows: $(BUILD)/chessplate.exe

$(BUILD)/chessplate.exe: $(APP_SRC) src/platform_win32.c src/*.h | $(BUILD)
	$(MINGW_CC) -O2 -std=c99 -Wall -Wextra -o $@ $(APP_SRC) src/platform_win32.c -mwindows -lgdi32 -luser32 -lm

$(BUILD)/embed_assets: tools/embed_assets.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/embed_assets.c

assets: $(BUILD)/embed_assets
	./$(BUILD)/embed_assets assets > src/assets.c

clean:
	rm -rf $(BUILD)
