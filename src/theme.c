#include "theme.h"

#include <math.h>
#include <stdio.h>

typedef struct {
    const char *name;
    unsigned char board_light[3];
    unsigned char board_dark[3];
    unsigned char accent[3];
    unsigned char viz_bg[3];
    unsigned char pos[3];
    unsigned char neg[3];
} ThemeSeed;

typedef struct {
    const char *suffix;
    int exposure;
    int accent_shift;
    int viz_shift;
} ThemeVariant;

static const ThemeSeed THEME_SEEDS[] = {
    {"Aurora Circuit", {229, 236, 248}, {74, 102, 134}, {84, 255, 219}, {16, 22, 36}, {99, 239, 171}, {255, 115, 125}},
    {"Ember Sand", {246, 224, 187}, {157, 98, 68}, {255, 194, 81}, {39, 24, 17}, {255, 212, 125}, {255, 122, 109}},
    {"Graphite Bloom", {223, 226, 233}, {103, 111, 128}, {146, 232, 214}, {20, 23, 30}, {150, 231, 187}, {255, 134, 159}},
    {"Ocean Glass", {205, 233, 238}, {47, 105, 124}, {63, 210, 237}, {12, 28, 36}, {123, 237, 198}, {255, 116, 145}},
    {"Moss Cathedral", {224, 232, 213}, {84, 112, 77}, {127, 214, 118}, {21, 29, 19}, {173, 233, 137}, {250, 132, 120}},
    {"Crimson Forge", {240, 215, 206}, {129, 64, 61}, {255, 104, 92}, {34, 15, 18}, {255, 188, 130}, {255, 88, 112}},
    {"Solar Dune", {249, 232, 178}, {180, 128, 52}, {255, 213, 78}, {44, 30, 13}, {255, 221, 112}, {255, 136, 98}},
    {"Arctic Mint", {226, 242, 239}, {89, 140, 132}, {110, 247, 205}, {18, 33, 30}, {145, 244, 196}, {255, 133, 150}},
    {"Neon Arcade", {233, 223, 252}, {74, 58, 137}, {79, 255, 165}, {18, 13, 36}, {135, 246, 164}, {255, 122, 164}},
    {"Bronze Harbor", {237, 219, 202}, {122, 87, 74}, {219, 166, 116}, {31, 23, 19}, {232, 201, 144}, {255, 123, 118}},
    {"Lavender Fog", {236, 226, 246}, {111, 90, 131}, {182, 154, 238}, {27, 21, 35}, {188, 235, 164}, {255, 132, 174}},
    {"Canyon Clay", {245, 221, 204}, {150, 89, 71}, {244, 154, 107}, {38, 22, 18}, {255, 202, 129}, {255, 118, 111}},
    {"Tropical Tide", {214, 242, 224}, {58, 132, 110}, {35, 236, 177}, {13, 34, 27}, {103, 241, 188}, {255, 132, 140}},
    {"Moonstone Slate", {225, 230, 239}, {88, 102, 123}, {142, 182, 235}, {20, 24, 33}, {155, 231, 190}, {255, 140, 160}},
    {"Cherry Blossom", {250, 225, 235}, {154, 88, 116}, {255, 145, 178}, {34, 18, 30}, {255, 205, 158}, {255, 101, 143}},
    {"Iceberg Steel", {219, 232, 245}, {74, 100, 137}, {124, 201, 255}, {13, 23, 39}, {132, 236, 201}, {255, 126, 155}},
    {"Peacock Night", {223, 236, 232}, {41, 88, 102}, {44, 212, 199}, {9, 25, 33}, {119, 239, 178}, {255, 117, 147}},
    {"Amber Grove", {244, 232, 198}, {112, 102, 59}, {241, 190, 77}, {31, 30, 14}, {225, 233, 121}, {255, 135, 112}},
    {"Electric Lime", {229, 241, 208}, {102, 134, 58}, {183, 244, 66}, {21, 31, 11}, {195, 247, 115}, {255, 125, 109}},
    {"Midnight Petrol", {208, 227, 230}, {39, 82, 90}, {86, 211, 227}, {10, 19, 27}, {117, 239, 206}, {255, 128, 152}},
    {"Terracotta Storm", {239, 216, 205}, {128, 74, 63}, {221, 136, 109}, {30, 17, 15}, {237, 195, 133}, {255, 113, 112}},
    {"Velvet Indigo", {230, 226, 244}, {75, 68, 128}, {127, 145, 244}, {16, 14, 35}, {166, 225, 171}, {255, 133, 173}},
    {"Sunlit Olive", {236, 232, 198}, {108, 113, 61}, {204, 221, 98}, {26, 28, 13}, {194, 234, 119}, {255, 130, 115}},
    {"Glacier Night", {216, 228, 244}, {63, 86, 127}, {108, 180, 255}, {11, 20, 36}, {136, 232, 203}, {255, 126, 166}},
    {"Sakura Ember", {247, 224, 226}, {143, 80, 87}, {255, 129, 123}, {35, 17, 22}, {255, 201, 139}, {255, 95, 121}},
    {"Cobalt Drift", {221, 231, 247}, {62, 89, 154}, {89, 170, 255}, {13, 18, 38}, {131, 230, 198}, {255, 131, 166}},
    {"Forest Copper", {231, 225, 204}, {85, 102, 70}, {204, 148, 100}, {23, 24, 17}, {193, 232, 136}, {255, 126, 113}},
    {"Urban Concrete", {224, 224, 224}, {95, 95, 95}, {166, 196, 222}, {24, 24, 24}, {150, 225, 180}, {255, 132, 150}},
    {"Orchid Steel", {232, 228, 239}, {96, 89, 119}, {190, 149, 228}, {25, 20, 33}, {184, 232, 170}, {255, 132, 176}},
    {"Golden Reef", {247, 230, 190}, {143, 116, 66}, {255, 206, 99}, {34, 27, 12}, {251, 224, 126}, {255, 131, 108}},
};

static const ThemeVariant THEME_VARIANTS[] = {
    {"Prime", 0, 0, 0},
    {"Radiant", 16, 18, 8},
    {"Noir", -18, -8, -12},
};

#define SEED_COUNT ((int)(sizeof(THEME_SEEDS) / sizeof(THEME_SEEDS[0])))
#define VARIANT_COUNT ((int)(sizeof(THEME_VARIANTS) / sizeof(THEME_VARIANTS[0])))

static BoardTheme themes[SEED_COUNT * VARIANT_COUNT];
static int themes_built = 0;

typedef struct {
    int c[3];
} Rgb;

static int clamp_channel(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static Rgb rgb_from(const unsigned char c[3]) {
    Rgb out = {{c[0], c[1], c[2]}};
    return out;
}

static Rgb shift(Rgb color, int delta) {
    Rgb out;
    for (int i = 0; i < 3; i++) out.c[i] = clamp_channel(color.c[i] + delta);
    return out;
}

static Rgb mix(Rgb a, int br, int bg, int bb, float t) {
    int b[3] = {br, bg, bb};
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float inv = 1.0f - t;
    Rgb out;
    for (int i = 0; i < 3; i++) out.c[i] = clamp_channel((int)roundf((float)a.c[i] * inv + (float)b[i] * t));
    return out;
}

static Rgba to_rgba(Rgb c, int alpha) { return rgba((uint8_t)c.c[0], (uint8_t)c.c[1], (uint8_t)c.c[2], (uint8_t)alpha); }

static ThemePalette build_palette(const ThemeSeed *seed, const ThemeVariant *variant) {
    Rgb board_light = shift(rgb_from(seed->board_light), variant->exposure + 8);
    Rgb board_dark = shift(rgb_from(seed->board_dark), variant->exposure - 3);
    Rgb accent = shift(rgb_from(seed->accent), variant->accent_shift);
    Rgb viz_bg = shift(rgb_from(seed->viz_bg), variant->viz_shift);
    Rgb pos = shift(rgb_from(seed->pos), variant->accent_shift / 2);
    Rgb neg = shift(rgb_from(seed->neg), variant->accent_shift / 3);

    Rgb selected = mix(accent, 255, 247, 168, 0.56f);
    Rgb target = mix(accent, 151, 232, 187, 0.40f);
    Rgb arrow_preview = mix(accent, 246, 249, 255, 0.58f);
    Rgb viz_grid = mix(viz_bg, 166, 186, 214, 0.56f);
    Rgb viz_depth = mix(accent, 120, 174, 255, 0.44f);
    Rgb viz_glow = mix(accent, 188, 232, 255, 0.52f);
    Rgb viz_text_main = mix(viz_bg, 242, 247, 255, 0.86f);
    Rgb viz_text_muted = mix(viz_bg, 214, 226, 244, 0.63f);

    ThemePalette p;
    p.board_light = to_rgba(board_light, 255);
    p.board_dark = to_rgba(board_dark, 255);
    p.selected = to_rgba(selected, 255);
    p.target = to_rgba(target, 255);
    p.arrow = to_rgba(accent, 210);
    p.arrow_preview = to_rgba(arrow_preview, 175);
    p.viz_bg = to_rgba(viz_bg, 255);
    p.viz_grid = to_rgba(viz_grid, 58);
    p.viz_depth = to_rgba(viz_depth, 128);
    p.viz_eval_pos = to_rgba(pos, 255);
    p.viz_eval_neg = to_rgba(neg, 255);
    p.viz_eval_glow = to_rgba(viz_glow, 78);
    p.viz_text_main = to_rgba(viz_text_main, 255);
    p.viz_text_muted = to_rgba(viz_text_muted, 255);
    p.viz_card = to_rgba(shift(viz_bg, 18), 220);
    return p;
}

static void build_themes(void) {
    int n = 0;
    for (int s = 0; s < SEED_COUNT; s++) {
        for (int v = 0; v < VARIANT_COUNT; v++) {
            snprintf(themes[n].name, sizeof(themes[n].name), "%s %s", THEME_SEEDS[s].name, THEME_VARIANTS[v].suffix);
            themes[n].palette = build_palette(&THEME_SEEDS[s], &THEME_VARIANTS[v]);
            n++;
        }
    }
    themes_built = 1;
}

int theme_count(void) { return SEED_COUNT * VARIANT_COUNT; }

const BoardTheme *theme_get(int index) {
    if (!themes_built) build_themes();
    if (index < 0 || index >= theme_count()) index = 0;
    return &themes[index];
}
