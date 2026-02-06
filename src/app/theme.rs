use std::sync::OnceLock;

use eframe::egui::Color32;

#[derive(Clone, Debug)]
pub struct BoardTheme {
    pub name: String,
    pub palette: ThemePalette,
}

#[derive(Clone, Copy, Debug)]
pub struct ThemePalette {
    pub board_light: Color32,
    pub board_dark: Color32,
    pub selected: Color32,
    pub target: Color32,
    pub arrow: Color32,
    pub arrow_preview: Color32,
    pub viz_bg: Color32,
    pub viz_grid: Color32,
    pub viz_depth: Color32,
    pub viz_eval_pos: Color32,
    pub viz_eval_neg: Color32,
    pub viz_eval_glow: Color32,
    pub viz_text_main: Color32,
    pub viz_text_muted: Color32,
    pub viz_card: Color32,
}

#[derive(Clone, Copy, Debug)]
struct ThemeSeed {
    name: &'static str,
    board_light: [u8; 3],
    board_dark: [u8; 3],
    accent: [u8; 3],
    viz_bg: [u8; 3],
    pos: [u8; 3],
    neg: [u8; 3],
}

#[derive(Clone, Copy, Debug)]
struct ThemeVariant {
    suffix: &'static str,
    exposure: i16,
    accent_shift: i16,
    viz_shift: i16,
}

const THEME_SEEDS: &[ThemeSeed] = &[
    ThemeSeed {
        name: "Aurora Circuit",
        board_light: [229, 236, 248],
        board_dark: [74, 102, 134],
        accent: [84, 255, 219],
        viz_bg: [16, 22, 36],
        pos: [99, 239, 171],
        neg: [255, 115, 125],
    },
    ThemeSeed {
        name: "Ember Sand",
        board_light: [246, 224, 187],
        board_dark: [157, 98, 68],
        accent: [255, 194, 81],
        viz_bg: [39, 24, 17],
        pos: [255, 212, 125],
        neg: [255, 122, 109],
    },
    ThemeSeed {
        name: "Graphite Bloom",
        board_light: [223, 226, 233],
        board_dark: [103, 111, 128],
        accent: [146, 232, 214],
        viz_bg: [20, 23, 30],
        pos: [150, 231, 187],
        neg: [255, 134, 159],
    },
    ThemeSeed {
        name: "Ocean Glass",
        board_light: [205, 233, 238],
        board_dark: [47, 105, 124],
        accent: [63, 210, 237],
        viz_bg: [12, 28, 36],
        pos: [123, 237, 198],
        neg: [255, 116, 145],
    },
    ThemeSeed {
        name: "Moss Cathedral",
        board_light: [224, 232, 213],
        board_dark: [84, 112, 77],
        accent: [127, 214, 118],
        viz_bg: [21, 29, 19],
        pos: [173, 233, 137],
        neg: [250, 132, 120],
    },
    ThemeSeed {
        name: "Crimson Forge",
        board_light: [240, 215, 206],
        board_dark: [129, 64, 61],
        accent: [255, 104, 92],
        viz_bg: [34, 15, 18],
        pos: [255, 188, 130],
        neg: [255, 88, 112],
    },
    ThemeSeed {
        name: "Solar Dune",
        board_light: [249, 232, 178],
        board_dark: [180, 128, 52],
        accent: [255, 213, 78],
        viz_bg: [44, 30, 13],
        pos: [255, 221, 112],
        neg: [255, 136, 98],
    },
    ThemeSeed {
        name: "Arctic Mint",
        board_light: [226, 242, 239],
        board_dark: [89, 140, 132],
        accent: [110, 247, 205],
        viz_bg: [18, 33, 30],
        pos: [145, 244, 196],
        neg: [255, 133, 150],
    },
    ThemeSeed {
        name: "Neon Arcade",
        board_light: [233, 223, 252],
        board_dark: [74, 58, 137],
        accent: [79, 255, 165],
        viz_bg: [18, 13, 36],
        pos: [135, 246, 164],
        neg: [255, 122, 164],
    },
    ThemeSeed {
        name: "Bronze Harbor",
        board_light: [237, 219, 202],
        board_dark: [122, 87, 74],
        accent: [219, 166, 116],
        viz_bg: [31, 23, 19],
        pos: [232, 201, 144],
        neg: [255, 123, 118],
    },
    ThemeSeed {
        name: "Lavender Fog",
        board_light: [236, 226, 246],
        board_dark: [111, 90, 131],
        accent: [182, 154, 238],
        viz_bg: [27, 21, 35],
        pos: [188, 235, 164],
        neg: [255, 132, 174],
    },
    ThemeSeed {
        name: "Canyon Clay",
        board_light: [245, 221, 204],
        board_dark: [150, 89, 71],
        accent: [244, 154, 107],
        viz_bg: [38, 22, 18],
        pos: [255, 202, 129],
        neg: [255, 118, 111],
    },
    ThemeSeed {
        name: "Tropical Tide",
        board_light: [214, 242, 224],
        board_dark: [58, 132, 110],
        accent: [35, 236, 177],
        viz_bg: [13, 34, 27],
        pos: [103, 241, 188],
        neg: [255, 132, 140],
    },
    ThemeSeed {
        name: "Moonstone Slate",
        board_light: [225, 230, 239],
        board_dark: [88, 102, 123],
        accent: [142, 182, 235],
        viz_bg: [20, 24, 33],
        pos: [155, 231, 190],
        neg: [255, 140, 160],
    },
    ThemeSeed {
        name: "Cherry Blossom",
        board_light: [250, 225, 235],
        board_dark: [154, 88, 116],
        accent: [255, 145, 178],
        viz_bg: [34, 18, 30],
        pos: [255, 205, 158],
        neg: [255, 101, 143],
    },
    ThemeSeed {
        name: "Iceberg Steel",
        board_light: [219, 232, 245],
        board_dark: [74, 100, 137],
        accent: [124, 201, 255],
        viz_bg: [13, 23, 39],
        pos: [132, 236, 201],
        neg: [255, 126, 155],
    },
    ThemeSeed {
        name: "Peacock Night",
        board_light: [223, 236, 232],
        board_dark: [41, 88, 102],
        accent: [44, 212, 199],
        viz_bg: [9, 25, 33],
        pos: [119, 239, 178],
        neg: [255, 117, 147],
    },
    ThemeSeed {
        name: "Amber Grove",
        board_light: [244, 232, 198],
        board_dark: [112, 102, 59],
        accent: [241, 190, 77],
        viz_bg: [31, 30, 14],
        pos: [225, 233, 121],
        neg: [255, 135, 112],
    },
    ThemeSeed {
        name: "Electric Lime",
        board_light: [229, 241, 208],
        board_dark: [102, 134, 58],
        accent: [183, 244, 66],
        viz_bg: [21, 31, 11],
        pos: [195, 247, 115],
        neg: [255, 125, 109],
    },
    ThemeSeed {
        name: "Midnight Petrol",
        board_light: [208, 227, 230],
        board_dark: [39, 82, 90],
        accent: [86, 211, 227],
        viz_bg: [10, 19, 27],
        pos: [117, 239, 206],
        neg: [255, 128, 152],
    },
    ThemeSeed {
        name: "Terracotta Storm",
        board_light: [239, 216, 205],
        board_dark: [128, 74, 63],
        accent: [221, 136, 109],
        viz_bg: [30, 17, 15],
        pos: [237, 195, 133],
        neg: [255, 113, 112],
    },
    ThemeSeed {
        name: "Velvet Indigo",
        board_light: [230, 226, 244],
        board_dark: [75, 68, 128],
        accent: [127, 145, 244],
        viz_bg: [16, 14, 35],
        pos: [166, 225, 171],
        neg: [255, 133, 173],
    },
    ThemeSeed {
        name: "Sunlit Olive",
        board_light: [236, 232, 198],
        board_dark: [108, 113, 61],
        accent: [204, 221, 98],
        viz_bg: [26, 28, 13],
        pos: [194, 234, 119],
        neg: [255, 130, 115],
    },
    ThemeSeed {
        name: "Glacier Night",
        board_light: [216, 228, 244],
        board_dark: [63, 86, 127],
        accent: [108, 180, 255],
        viz_bg: [11, 20, 36],
        pos: [136, 232, 203],
        neg: [255, 126, 166],
    },
    ThemeSeed {
        name: "Sakura Ember",
        board_light: [247, 224, 226],
        board_dark: [143, 80, 87],
        accent: [255, 129, 123],
        viz_bg: [35, 17, 22],
        pos: [255, 201, 139],
        neg: [255, 95, 121],
    },
    ThemeSeed {
        name: "Cobalt Drift",
        board_light: [221, 231, 247],
        board_dark: [62, 89, 154],
        accent: [89, 170, 255],
        viz_bg: [13, 18, 38],
        pos: [131, 230, 198],
        neg: [255, 131, 166],
    },
    ThemeSeed {
        name: "Forest Copper",
        board_light: [231, 225, 204],
        board_dark: [85, 102, 70],
        accent: [204, 148, 100],
        viz_bg: [23, 24, 17],
        pos: [193, 232, 136],
        neg: [255, 126, 113],
    },
    ThemeSeed {
        name: "Urban Concrete",
        board_light: [224, 224, 224],
        board_dark: [95, 95, 95],
        accent: [166, 196, 222],
        viz_bg: [24, 24, 24],
        pos: [150, 225, 180],
        neg: [255, 132, 150],
    },
    ThemeSeed {
        name: "Orchid Steel",
        board_light: [232, 228, 239],
        board_dark: [96, 89, 119],
        accent: [190, 149, 228],
        viz_bg: [25, 20, 33],
        pos: [184, 232, 170],
        neg: [255, 132, 176],
    },
    ThemeSeed {
        name: "Golden Reef",
        board_light: [247, 230, 190],
        board_dark: [143, 116, 66],
        accent: [255, 206, 99],
        viz_bg: [34, 27, 12],
        pos: [251, 224, 126],
        neg: [255, 131, 108],
    },
];

const THEME_VARIANTS: &[ThemeVariant] = &[
    ThemeVariant {
        suffix: "Prime",
        exposure: 0,
        accent_shift: 0,
        viz_shift: 0,
    },
    ThemeVariant {
        suffix: "Radiant",
        exposure: 16,
        accent_shift: 18,
        viz_shift: 8,
    },
    ThemeVariant {
        suffix: "Noir",
        exposure: -18,
        accent_shift: -8,
        viz_shift: -12,
    },
];

pub fn all_themes() -> &'static [BoardTheme] {
    static THEMES: OnceLock<Vec<BoardTheme>> = OnceLock::new();
    THEMES.get_or_init(build_themes).as_slice()
}

fn build_themes() -> Vec<BoardTheme> {
    let mut themes = Vec::with_capacity(THEME_SEEDS.len() * THEME_VARIANTS.len());
    for seed in THEME_SEEDS {
        for variant in THEME_VARIANTS {
            themes.push(BoardTheme {
                name: format!("{} {}", seed.name, variant.suffix),
                palette: build_palette(*seed, *variant),
            });
        }
    }
    themes
}

fn build_palette(seed: ThemeSeed, variant: ThemeVariant) -> ThemePalette {
    let board_light = shift(seed.board_light, variant.exposure + 8);
    let board_dark = shift(seed.board_dark, variant.exposure - 3);
    let accent = shift(seed.accent, variant.accent_shift);
    let viz_bg = shift(seed.viz_bg, variant.viz_shift);
    let pos = shift(seed.pos, variant.accent_shift / 2);
    let neg = shift(seed.neg, variant.accent_shift / 3);

    let selected = mix(accent, [255, 247, 168], 0.56);
    let target = mix(accent, [151, 232, 187], 0.40);
    let arrow_preview = mix(accent, [246, 249, 255], 0.58);
    let viz_grid = mix(viz_bg, [166, 186, 214], 0.56);
    let viz_depth = mix(accent, [120, 174, 255], 0.44);
    let viz_glow = mix(accent, [188, 232, 255], 0.52);
    let viz_text_main = mix(viz_bg, [242, 247, 255], 0.86);
    let viz_text_muted = mix(viz_bg, [214, 226, 244], 0.63);

    ThemePalette {
        board_light: rgb(board_light),
        board_dark: rgb(board_dark),
        selected: rgb(selected),
        target: rgb(target),
        arrow: rgba(accent, 210),
        arrow_preview: rgba(arrow_preview, 175),
        viz_bg: rgb(viz_bg),
        viz_grid: rgba(viz_grid, 58),
        viz_depth: rgba(viz_depth, 128),
        viz_eval_pos: rgb(pos),
        viz_eval_neg: rgb(neg),
        viz_eval_glow: rgba(viz_glow, 78),
        viz_text_main: rgb(viz_text_main),
        viz_text_muted: rgb(viz_text_muted),
        viz_card: rgba(shift(viz_bg, 18), 220),
    }
}

fn rgb(c: [u8; 3]) -> Color32 {
    Color32::from_rgb(c[0], c[1], c[2])
}

fn rgba(c: [u8; 3], alpha: u8) -> Color32 {
    Color32::from_rgba_unmultiplied(c[0], c[1], c[2], alpha)
}

fn shift(color: [u8; 3], delta: i16) -> [u8; 3] {
    [
        clamp_channel(i16::from(color[0]) + delta),
        clamp_channel(i16::from(color[1]) + delta),
        clamp_channel(i16::from(color[2]) + delta),
    ]
}

fn mix(a: [u8; 3], b: [u8; 3], t: f32) -> [u8; 3] {
    let t = t.clamp(0.0, 1.0);
    let inv = 1.0 - t;
    [
        ((a[0] as f32 * inv) + (b[0] as f32 * t)).round() as u8,
        ((a[1] as f32 * inv) + (b[1] as f32 * t)).round() as u8,
        ((a[2] as f32 * inv) + (b[2] as f32 * t)).round() as u8,
    ]
}

fn clamp_channel(value: i16) -> u8 {
    value.clamp(0, 255) as u8
}
