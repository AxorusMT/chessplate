use eframe::egui::IconData;
use resvg::{tiny_skia, usvg};

const APP_ICON_SVG: &[u8] = include_bytes!("../assets/logo/chessplate-logo.svg");
const ICON_SIZE: u32 = 256;

pub fn app_icon() -> IconData {
    let tree = usvg::Tree::from_data(APP_ICON_SVG, &usvg::Options::default())
        .expect("failed to parse app icon svg");

    let mut pixmap =
        tiny_skia::Pixmap::new(ICON_SIZE, ICON_SIZE).expect("failed to allocate app icon pixmap");

    let sx = ICON_SIZE as f32 / tree.size().width();
    let sy = ICON_SIZE as f32 / tree.size().height();
    let transform = tiny_skia::Transform::from_scale(sx, sy);

    {
        let mut pixmap_mut = pixmap.as_mut();
        resvg::render(&tree, transform, &mut pixmap_mut);
    }

    IconData {
        rgba: pixmap.data().to_vec(),
        width: ICON_SIZE,
        height: ICON_SIZE,
    }
}
