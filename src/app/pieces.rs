use std::collections::HashMap;

use eframe::egui;
use resvg::{tiny_skia, usvg};

use crate::chess::{Color, Piece, PieceKind};

pub fn load_piece_textures(ctx: &egui::Context) -> HashMap<Piece, egui::TextureHandle> {
    let mut textures = HashMap::new();

    let entries: &[(Piece, &str, &[u8])] = &[
        (
            Piece {
                color: Color::White,
                kind: PieceKind::King,
            },
            "piece_wk",
            include_bytes!("../../assets/pieces/wK.svg"),
        ),
        (
            Piece {
                color: Color::White,
                kind: PieceKind::Queen,
            },
            "piece_wq",
            include_bytes!("../../assets/pieces/wQ.svg"),
        ),
        (
            Piece {
                color: Color::White,
                kind: PieceKind::Rook,
            },
            "piece_wr",
            include_bytes!("../../assets/pieces/wR.svg"),
        ),
        (
            Piece {
                color: Color::White,
                kind: PieceKind::Bishop,
            },
            "piece_wb",
            include_bytes!("../../assets/pieces/wB.svg"),
        ),
        (
            Piece {
                color: Color::White,
                kind: PieceKind::Knight,
            },
            "piece_wn",
            include_bytes!("../../assets/pieces/wN.svg"),
        ),
        (
            Piece {
                color: Color::White,
                kind: PieceKind::Pawn,
            },
            "piece_wp",
            include_bytes!("../../assets/pieces/wP.svg"),
        ),
        (
            Piece {
                color: Color::Black,
                kind: PieceKind::King,
            },
            "piece_bk",
            include_bytes!("../../assets/pieces/bK.svg"),
        ),
        (
            Piece {
                color: Color::Black,
                kind: PieceKind::Queen,
            },
            "piece_bq",
            include_bytes!("../../assets/pieces/bQ.svg"),
        ),
        (
            Piece {
                color: Color::Black,
                kind: PieceKind::Rook,
            },
            "piece_br",
            include_bytes!("../../assets/pieces/bR.svg"),
        ),
        (
            Piece {
                color: Color::Black,
                kind: PieceKind::Bishop,
            },
            "piece_bb",
            include_bytes!("../../assets/pieces/bB.svg"),
        ),
        (
            Piece {
                color: Color::Black,
                kind: PieceKind::Knight,
            },
            "piece_bn",
            include_bytes!("../../assets/pieces/bN.svg"),
        ),
        (
            Piece {
                color: Color::Black,
                kind: PieceKind::Pawn,
            },
            "piece_bp",
            include_bytes!("../../assets/pieces/bP.svg"),
        ),
    ];

    for (piece, texture_name, svg_data) in entries {
        let texture = render_svg_texture(ctx, texture_name, svg_data);
        textures.insert(*piece, texture);
    }

    textures
}

fn render_svg_texture(
    ctx: &egui::Context,
    texture_name: &str,
    svg_data: &[u8],
) -> egui::TextureHandle {
    let tree = usvg::Tree::from_data(svg_data, &usvg::Options::default())
        .unwrap_or_else(|err| panic!("failed to parse SVG {texture_name}: {err}"));

    let width = tree.size().width().round().max(1.0) as u32;
    let height = tree.size().height().round().max(1.0) as u32;

    let mut pixmap = tiny_skia::Pixmap::new(width, height)
        .unwrap_or_else(|| panic!("failed to allocate pixmap for {texture_name}"));
    {
        let mut pixmap_mut = pixmap.as_mut();
        resvg::render(&tree, tiny_skia::Transform::default(), &mut pixmap_mut);
    }

    let image =
        egui::ColorImage::from_rgba_unmultiplied([width as usize, height as usize], pixmap.data());

    ctx.load_texture(texture_name.to_owned(), image, egui::TextureOptions::LINEAR)
}
