use eframe::egui::Color32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BoardTheme {
    AuroraCircuit,
    EmberSand,
    GraphiteBloom,
}

impl BoardTheme {
    pub fn name(self) -> &'static str {
        match self {
            Self::AuroraCircuit => "Aurora Circuit",
            Self::EmberSand => "Ember Sand",
            Self::GraphiteBloom => "Graphite Bloom",
        }
    }

    pub fn all() -> [Self; 3] {
        [Self::AuroraCircuit, Self::EmberSand, Self::GraphiteBloom]
    }

    pub fn palette(self) -> ThemePalette {
        match self {
            Self::AuroraCircuit => ThemePalette {
                board_light: Color32::from_rgb(229, 236, 248),
                board_dark: Color32::from_rgb(74, 102, 134),
                selected: Color32::from_rgb(252, 245, 156),
                target: Color32::from_rgb(118, 214, 171),
                arrow: Color32::from_rgba_unmultiplied(84, 255, 219, 210),
                arrow_preview: Color32::from_rgba_unmultiplied(166, 255, 237, 175),
                viz_bg: Color32::from_rgb(16, 22, 36),
                viz_grid: Color32::from_rgba_unmultiplied(125, 145, 186, 55),
                viz_depth: Color32::from_rgba_unmultiplied(75, 170, 255, 120),
                viz_eval_pos: Color32::from_rgb(99, 239, 171),
                viz_eval_neg: Color32::from_rgb(255, 115, 125),
                viz_eval_glow: Color32::from_rgba_unmultiplied(120, 240, 255, 80),
                viz_text_main: Color32::from_rgb(230, 240, 255),
                viz_text_muted: Color32::from_rgb(153, 180, 214),
                viz_card: Color32::from_rgba_unmultiplied(26, 34, 52, 220),
            },
            Self::EmberSand => ThemePalette {
                board_light: Color32::from_rgb(246, 224, 187),
                board_dark: Color32::from_rgb(157, 98, 68),
                selected: Color32::from_rgb(255, 231, 135),
                target: Color32::from_rgb(228, 170, 112),
                arrow: Color32::from_rgba_unmultiplied(255, 194, 81, 215),
                arrow_preview: Color32::from_rgba_unmultiplied(255, 223, 163, 180),
                viz_bg: Color32::from_rgb(39, 24, 17),
                viz_grid: Color32::from_rgba_unmultiplied(208, 135, 101, 55),
                viz_depth: Color32::from_rgba_unmultiplied(255, 154, 94, 130),
                viz_eval_pos: Color32::from_rgb(255, 212, 125),
                viz_eval_neg: Color32::from_rgb(255, 122, 109),
                viz_eval_glow: Color32::from_rgba_unmultiplied(255, 201, 112, 80),
                viz_text_main: Color32::from_rgb(255, 236, 213),
                viz_text_muted: Color32::from_rgb(211, 170, 140),
                viz_card: Color32::from_rgba_unmultiplied(55, 34, 24, 220),
            },
            Self::GraphiteBloom => ThemePalette {
                board_light: Color32::from_rgb(223, 226, 233),
                board_dark: Color32::from_rgb(103, 111, 128),
                selected: Color32::from_rgb(223, 247, 163),
                target: Color32::from_rgb(149, 221, 194),
                arrow: Color32::from_rgba_unmultiplied(146, 232, 214, 210),
                arrow_preview: Color32::from_rgba_unmultiplied(188, 247, 236, 175),
                viz_bg: Color32::from_rgb(20, 23, 30),
                viz_grid: Color32::from_rgba_unmultiplied(147, 165, 191, 50),
                viz_depth: Color32::from_rgba_unmultiplied(143, 173, 224, 125),
                viz_eval_pos: Color32::from_rgb(150, 231, 187),
                viz_eval_neg: Color32::from_rgb(255, 134, 159),
                viz_eval_glow: Color32::from_rgba_unmultiplied(186, 230, 255, 70),
                viz_text_main: Color32::from_rgb(235, 240, 249),
                viz_text_muted: Color32::from_rgb(167, 180, 204),
                viz_card: Color32::from_rgba_unmultiplied(31, 37, 49, 220),
            },
        }
    }
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
