mod app;
mod chess;
mod engine;

fn main() -> eframe::Result<()> {
    let options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_title("Chessplate")
            .with_inner_size([920.0, 680.0])
            .with_min_inner_size([820.0, 620.0]),
        ..Default::default()
    };

    eframe::run_native(
        "Chessplate",
        options,
        Box::new(|cc| Ok(Box::new(app::ChessApp::new(cc)))),
    )
}
