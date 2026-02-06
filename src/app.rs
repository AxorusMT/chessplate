use std::collections::HashMap;
use std::sync::mpsc::{self, Receiver};
use std::thread;
use std::time::Duration;

use eframe::egui::{self, Color32, PointerButton, Pos2, Rect, RichText, Stroke, Vec2};
use resvg::{tiny_skia, usvg};

use crate::chess::{
    Color, GameStatus, Move, Piece, PieceKind, Position, move_to_coord, move_to_san,
    piece_to_unicode,
};
use crate::engine::{EngineConfig, best_move, profile_for_elo};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum GamePhase {
    Opening,
    Middlegame,
    Endgame,
}

impl GamePhase {
    fn name(self) -> &'static str {
        match self {
            Self::Opening => "Opening",
            Self::Middlegame => "Middlegame",
            Self::Endgame => "Endgame",
        }
    }
}

#[derive(Clone, Debug)]
struct PhaseNames {
    current_phase: &'static str,
    opening_name: &'static str,
    middlegame_name: &'static str,
    endgame_name: &'static str,
}

#[derive(Clone, Copy, Debug)]
struct OpeningLine {
    name: &'static str,
    uci_moves: &'static [&'static str],
}

#[derive(Default, Clone, Copy, Debug)]
struct MaterialSnapshot {
    white_queens: u8,
    black_queens: u8,
    rooks: u8,
    bishops: u8,
    knights: u8,
    pawns: u8,
    non_pawn_material: i32,
    total_piece_count: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct DrawnArrow {
    from: u8,
    to: u8,
}

pub struct ChessApp {
    position: Position,
    selected_square: Option<u8>,
    selected_moves: Vec<Move>,
    human_color: Color,
    bot_elo: u16,
    status: GameStatus,
    move_log: Vec<String>,
    move_history: Vec<Move>,
    thinking: bool,
    engine_rx: Option<Receiver<Option<Move>>>,
    piece_textures: HashMap<Piece, egui::TextureHandle>,
    drawn_arrows: Vec<DrawnArrow>,
    arrow_drag_start: Option<u8>,
    arrow_drag_current: Option<u8>,
}

impl ChessApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        let position = Position::startpos();
        Self {
            status: position.game_status(),
            position,
            selected_square: None,
            selected_moves: Vec::new(),
            human_color: Color::White,
            bot_elo: 1600,
            move_log: Vec::new(),
            move_history: Vec::new(),
            thinking: false,
            engine_rx: None,
            piece_textures: load_piece_textures(&cc.egui_ctx),
            drawn_arrows: Vec::new(),
            arrow_drag_start: None,
            arrow_drag_current: None,
        }
    }

    fn bot_color(&self) -> Color {
        self.human_color.opposite()
    }

    fn new_game(&mut self) {
        self.position = Position::startpos();
        self.selected_square = None;
        self.selected_moves.clear();
        self.move_log.clear();
        self.move_history.clear();
        self.thinking = false;
        self.engine_rx = None;
        self.drawn_arrows.clear();
        self.arrow_drag_start = None;
        self.arrow_drag_current = None;
        self.sync_status();
    }

    fn is_human_turn(&self) -> bool {
        self.position.side_to_move == self.human_color
    }

    fn sync_status(&mut self) {
        self.status = self.position.game_status();
    }

    fn is_game_over(&self) -> bool {
        !matches!(self.status, GameStatus::Ongoing)
    }

    fn poll_engine_result(&mut self) {
        if !self.thinking {
            return;
        }

        let Some(receiver) = self.engine_rx.as_ref() else {
            self.thinking = false;
            return;
        };

        if let Ok(engine_move) = receiver.try_recv() {
            self.engine_rx = None;
            self.thinking = false;

            if let Some(mv) = engine_move {
                self.apply_move(mv);
            } else {
                self.sync_status();
            }
        }
    }

    fn maybe_start_engine_turn(&mut self) {
        if self.thinking || self.is_game_over() || self.is_human_turn() {
            return;
        }

        let position = self.position.clone();
        let config = EngineConfig::new(self.bot_elo);
        let (tx, rx) = mpsc::channel();

        self.engine_rx = Some(rx);
        self.thinking = true;

        thread::spawn(move || {
            let mv = best_move(&position, config);
            let _ = tx.send(mv);
        });
    }

    fn apply_move(&mut self, mv: Move) {
        let notation = move_to_coord(mv);
        self.position.make_move(mv);
        self.move_log.push(notation);
        self.move_history.push(mv);
        self.selected_square = None;
        self.selected_moves.clear();
        self.sync_status();
    }

    fn game_result_tag(&self) -> &'static str {
        match self.status {
            GameStatus::Ongoing => "*",
            GameStatus::Checkmate(winner) => {
                if winner == Color::White {
                    "1-0"
                } else {
                    "0-1"
                }
            }
            GameStatus::Stalemate | GameStatus::DrawFiftyMove => "1/2-1/2",
        }
    }

    fn game_to_pgn(&self) -> String {
        let result = self.game_result_tag();
        let mut replay = Position::startpos();
        let mut move_text = String::new();

        for (index, mv) in self.move_history.iter().enumerate() {
            let san = move_to_san(&replay, *mv);
            if index % 2 == 0 {
                if !move_text.is_empty() {
                    move_text.push(' ');
                }
                move_text.push_str(&format!("{}.", (index / 2) + 1));
                move_text.push(' ');
                move_text.push_str(&san);
            } else {
                move_text.push(' ');
                move_text.push_str(&san);
            }
            let _ = replay.make_move(*mv);
        }

        if !move_text.is_empty() {
            move_text.push(' ');
        }
        move_text.push_str(result);

        format!("[Event \"Chessplate\"]\n[Site \"Local\"]\n[Result \"{result}\"]\n\n{move_text}")
    }

    fn set_selection(&mut self, square: u8) {
        self.selected_square = Some(square);
        let legal = self.position.legal_moves();
        self.selected_moves = legal
            .into_iter()
            .filter(|mv| mv.from == square)
            .collect::<Vec<_>>();

        if self.selected_moves.is_empty() {
            self.selected_square = None;
        }
    }

    fn handle_square_click(&mut self, square: u8) {
        if self.thinking || self.is_game_over() || !self.is_human_turn() {
            return;
        }

        if let Some(from_square) = self.selected_square {
            if from_square == square {
                self.selected_square = None;
                self.selected_moves.clear();
                return;
            }

            let candidates = self
                .selected_moves
                .iter()
                .copied()
                .filter(|mv| mv.from == from_square && mv.to == square)
                .collect::<Vec<_>>();

            if !candidates.is_empty() {
                let chosen = candidates
                    .iter()
                    .copied()
                    .find(|mv| mv.promotion == Some(PieceKind::Queen))
                    .unwrap_or(candidates[0]);
                self.apply_move(chosen);
                return;
            }
        }

        if let Some(piece) = self.position.piece_at(square) {
            if piece.color == self.human_color {
                self.set_selection(square);
            } else {
                self.selected_square = None;
                self.selected_moves.clear();
            }
        } else {
            self.selected_square = None;
            self.selected_moves.clear();
        }
    }

    fn status_text(&self) -> String {
        match self.status {
            GameStatus::Ongoing => {
                if self.thinking {
                    format!("{} is thinking...", self.bot_color().name())
                } else {
                    let side = self.position.side_to_move.name();
                    let suffix = if self.position.is_in_check(self.position.side_to_move) {
                        " (check)"
                    } else {
                        ""
                    };
                    format!("{side} to move{suffix}")
                }
            }
            GameStatus::Checkmate(winner) => format!("Checkmate. {} wins.", winner.name()),
            GameStatus::Stalemate => "Stalemate. Draw.".to_owned(),
            GameStatus::DrawFiftyMove => "Draw by 50-move rule.".to_owned(),
        }
    }

    fn square_from_board_grid(&self, column: usize, row: usize) -> u8 {
        let file = match self.human_color {
            Color::White => column as u8,
            Color::Black => 7_u8.saturating_sub(column as u8),
        };

        let rank = match self.human_color {
            Color::White => 7_u8.saturating_sub(row as u8),
            Color::Black => row as u8,
        };

        (rank * 8) + file
    }

    fn board_grid_from_square(&self, square: u8) -> (usize, usize) {
        let file = square % 8;
        let rank = square / 8;

        let column = match self.human_color {
            Color::White => file,
            Color::Black => 7 - file,
        } as usize;

        let row = match self.human_color {
            Color::White => 7 - rank,
            Color::Black => rank,
        } as usize;

        (column, row)
    }

    fn square_from_pointer_pos(&self, pos: Pos2, board_rect: Rect, square_size: f32) -> Option<u8> {
        if !board_rect.contains(pos) {
            return None;
        }

        let column = ((pos.x - board_rect.min.x) / square_size).floor() as i32;
        let row = ((pos.y - board_rect.min.y) / square_size).floor() as i32;

        if !(0..8).contains(&column) || !(0..8).contains(&row) {
            return None;
        }

        Some(self.square_from_board_grid(column as usize, row as usize))
    }

    fn square_center(&self, square: u8, board_rect: Rect, square_size: f32) -> Pos2 {
        let (column, row) = self.board_grid_from_square(square);
        Pos2::new(
            board_rect.min.x + ((column as f32) + 0.5) * square_size,
            board_rect.min.y + ((row as f32) + 0.5) * square_size,
        )
    }

    fn toggle_arrow(&mut self, from: u8, to: u8) {
        if from == to {
            return;
        }

        if let Some(index) = self
            .drawn_arrows
            .iter()
            .position(|arrow| arrow.from == from && arrow.to == to)
        {
            self.drawn_arrows.remove(index);
        } else {
            self.drawn_arrows.push(DrawnArrow { from, to });
        }
    }

    fn clear_arrows(&mut self) {
        self.drawn_arrows.clear();
        self.arrow_drag_start = None;
        self.arrow_drag_current = None;
    }

    fn handle_arrow_input(&mut self, ctx: &egui::Context, board_rect: Rect, square_size: f32) {
        if ctx.input(|i| i.pointer.secondary_pressed()) {
            let start_square = ctx.input(|i| {
                i.pointer
                    .interact_pos()
                    .and_then(|pos| self.square_from_pointer_pos(pos, board_rect, square_size))
            });
            self.arrow_drag_start = start_square;
            self.arrow_drag_current = start_square;
        }

        if self.arrow_drag_start.is_none() {
            return;
        }

        if ctx.input(|i| i.pointer.secondary_down()) {
            let current_square = ctx.input(|i| {
                i.pointer
                    .interact_pos()
                    .or(i.pointer.hover_pos())
                    .and_then(|pos| self.square_from_pointer_pos(pos, board_rect, square_size))
            });
            if let Some(square) = current_square {
                self.arrow_drag_current = Some(square);
            }
            ctx.request_repaint();
        }

        if ctx.input(|i| i.pointer.secondary_released()) {
            if let (Some(from), Some(to)) = (self.arrow_drag_start, self.arrow_drag_current) {
                self.toggle_arrow(from, to);
            }
            self.arrow_drag_start = None;
            self.arrow_drag_current = None;
        }
    }

    fn draw_arrow(painter: &egui::Painter, from: Pos2, to: Pos2, color: Color32, thickness: f32) {
        let direction = to - from;
        let length = direction.length();
        if length < 2.0 {
            return;
        }

        let unit = direction / length;
        let perpendicular = Vec2::new(-unit.y, unit.x);

        let start = from + unit * (thickness * 0.6);
        let tip = to - unit * (thickness * 0.2);
        let shaft_end = tip - unit * (thickness * 2.0);

        painter.line_segment([start, shaft_end], Stroke::new(thickness, color));

        let head_back = tip - unit * (thickness * 2.3);
        let wing = thickness * 1.2;
        let left = head_back + perpendicular * wing;
        let right = head_back - perpendicular * wing;
        painter.add(egui::Shape::convex_polygon(
            vec![tip, left, right],
            color,
            Stroke::NONE,
        ));
    }

    fn paint_arrows(&self, ui: &egui::Ui, board_rect: Rect, square_size: f32) {
        let painter = ui.painter_at(board_rect);

        let committed_color = Color32::from_rgba_unmultiplied(249, 214, 47, 200);
        for arrow in &self.drawn_arrows {
            let from = self.square_center(arrow.from, board_rect, square_size);
            let to = self.square_center(arrow.to, board_rect, square_size);
            Self::draw_arrow(&painter, from, to, committed_color, 6.0);
        }

        if let (Some(from_square), Some(to_square)) =
            (self.arrow_drag_start, self.arrow_drag_current)
        {
            let from = self.square_center(from_square, board_rect, square_size);
            let to = self.square_center(to_square, board_rect, square_size);
            let preview = Color32::from_rgba_unmultiplied(243, 238, 115, 180);
            Self::draw_arrow(&painter, from, to, preview, 5.0);
        }
    }

    fn draw_controls(&mut self, ui: &mut egui::Ui) {
        ui.heading("Chessplate");
        ui.label("Play against a local Rust engine.");
        ui.separator();

        ui.label(self.status_text());
        let phase_names = phase_names(&self.position, &self.move_log);
        ui.add_space(8.0);
        ui.group(|ui| {
            ui.label(RichText::new("Game Phases").strong());
            ui.label(format!("Current: {}", phase_names.current_phase));
            ui.separator();
            ui.label(format!("Opening: {}", phase_names.opening_name));
            ui.label(format!("Middlegame: {}", phase_names.middlegame_name));
            ui.label(format!("Endgame: {}", phase_names.endgame_name));
        });

        ui.add_space(8.0);
        ui.add(egui::Slider::new(&mut self.bot_elo, 600..=2800).text("Bot Elo"));

        let profile = profile_for_elo(self.bot_elo);
        ui.label(format!(
            "Depth {} | {} ms | {} nodes",
            profile.depth, profile.time_ms, profile.node_limit
        ));

        if profile.randomness_cp > 0 {
            ui.label(format!(
                "Error model: +/-{} centipawns",
                profile.randomness_cp
            ));
        } else {
            ui.label("Error model: off");
        }

        ui.add_space(10.0);
        if ui.button("New Game").clicked() {
            self.new_game();
        }

        if ui.button("Play as White").clicked() {
            self.human_color = Color::White;
            self.new_game();
        }

        if ui.button("Play as Black").clicked() {
            self.human_color = Color::Black;
            self.new_game();
        }

        ui.horizontal(|ui| {
            if ui.button("Copy State").clicked() {
                ui.ctx().copy_text(self.position.to_fen());
            }
            if ui.button("Copy Game").clicked() {
                ui.ctx().copy_text(self.game_to_pgn());
            }
        });

        ui.separator();
        ui.label("Moves");
        egui::ScrollArea::vertical()
            .id_salt("move_log")
            .max_height(360.0)
            .show(ui, |ui| {
                if self.move_log.is_empty() {
                    ui.label("No moves yet.");
                } else {
                    for (index, mv) in self.move_log.iter().enumerate() {
                        let turn = (index / 2) + 1;
                        let prefix = if index % 2 == 0 {
                            format!("{turn}.")
                        } else {
                            "...".to_owned()
                        };
                        ui.monospace(format!("{prefix:>4} {mv}"));
                    }
                }
            });
    }

    fn draw_board(&mut self, ui: &mut egui::Ui) {
        let square_size = 72.0;
        let piece_image_size = Vec2::splat(square_size - 12.0);

        let selected = self.selected_square;
        let selected_targets = self
            .selected_moves
            .iter()
            .map(|mv| mv.to)
            .collect::<Vec<_>>();

        let previous_padding = ui.spacing().button_padding;
        ui.spacing_mut().button_padding = Vec2::ZERO;

        let board_response = ui
            .scope(|ui| {
                egui::Grid::new("chess_board")
                    .spacing(Vec2::ZERO)
                    .show(ui, |ui| {
                        for row in 0..8 {
                            for column in 0..8 {
                                let square = self.square_from_board_grid(column, row);
                                let file = square % 8;
                                let rank = square / 8;
                                let is_light = (file + rank) % 2 == 0;

                                let mut fill = if is_light {
                                    Color32::from_rgb(241, 217, 181)
                                } else {
                                    Color32::from_rgb(181, 136, 99)
                                };

                                if Some(square) == selected {
                                    fill = Color32::from_rgb(245, 246, 130);
                                } else if selected_targets.contains(&square) {
                                    fill = Color32::from_rgb(140, 195, 120);
                                }

                                let button = if let Some(piece) = self.position.piece_at(square) {
                                    if let Some(texture) = self.piece_textures.get(&piece) {
                                        egui::Button::image(egui::Image::new((
                                            texture.id(),
                                            piece_image_size,
                                        )))
                                    } else {
                                        egui::Button::new(
                                            RichText::new(piece_to_unicode(piece)).size(40.0),
                                        )
                                    }
                                } else {
                                    egui::Button::new("")
                                }
                                .fill(fill)
                                .frame(false)
                                .min_size(Vec2::new(square_size, square_size));

                                if ui.add(button).clicked_by(PointerButton::Primary) {
                                    self.handle_square_click(square);
                                }
                            }
                            ui.end_row();
                        }
                    });
            })
            .response;

        ui.spacing_mut().button_padding = previous_padding;

        let board_rect = board_response.rect;
        self.handle_arrow_input(ui.ctx(), board_rect, square_size);
        self.paint_arrows(ui, board_rect, square_size);
    }
}

impl eframe::App for ChessApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let should_clear_arrows = ctx.input(|i| {
            i.key_pressed(egui::Key::Escape) || i.pointer.button_pressed(PointerButton::Primary)
        });
        if should_clear_arrows {
            self.clear_arrows();
        }

        self.poll_engine_result();
        self.maybe_start_engine_turn();

        egui::SidePanel::left("control_panel")
            .resizable(false)
            .min_width(220.0)
            .show(ctx, |ui| {
                self.draw_controls(ui);
            });

        egui::CentralPanel::default()
            .frame(egui::Frame::default().inner_margin(egui::Margin::same(8.0)))
            .show(ctx, |ui| {
                ui.with_layout(egui::Layout::top_down(egui::Align::Center), |ui| {
                    self.draw_board(ui);
                });
            });

        if self.thinking {
            ctx.request_repaint_after(Duration::from_millis(30));
        }
    }
}

const OPENING_BOOK: &[OpeningLine] = &[
    OpeningLine {
        name: "King's Pawn Game",
        uci_moves: &["e2e4", "e7e5"],
    },
    OpeningLine {
        name: "Italian Game",
        uci_moves: &["e2e4", "e7e5", "g1f3", "b8c6", "f1c4"],
    },
    OpeningLine {
        name: "Ruy Lopez",
        uci_moves: &["e2e4", "e7e5", "g1f3", "b8c6", "f1b5"],
    },
    OpeningLine {
        name: "Scotch Game",
        uci_moves: &["e2e4", "e7e5", "g1f3", "b8c6", "d2d4"],
    },
    OpeningLine {
        name: "Petrov Defense",
        uci_moves: &["e2e4", "e7e5", "g1f3", "g8f6"],
    },
    OpeningLine {
        name: "Sicilian Defense",
        uci_moves: &["e2e4", "c7c5"],
    },
    OpeningLine {
        name: "French Defense",
        uci_moves: &["e2e4", "e7e6"],
    },
    OpeningLine {
        name: "Caro-Kann Defense",
        uci_moves: &["e2e4", "c7c6"],
    },
    OpeningLine {
        name: "Pirc Defense",
        uci_moves: &["e2e4", "d7d6"],
    },
    OpeningLine {
        name: "Scandinavian Defense",
        uci_moves: &["e2e4", "d7d5"],
    },
    OpeningLine {
        name: "Queen's Gambit",
        uci_moves: &["d2d4", "d7d5", "c2c4"],
    },
    OpeningLine {
        name: "Queen's Gambit Accepted",
        uci_moves: &["d2d4", "d7d5", "c2c4", "d5c4"],
    },
    OpeningLine {
        name: "Queen's Gambit Declined",
        uci_moves: &["d2d4", "d7d5", "c2c4", "e7e6"],
    },
    OpeningLine {
        name: "Slav Defense",
        uci_moves: &["d2d4", "d7d5", "c2c4", "c7c6"],
    },
    OpeningLine {
        name: "King's Indian Defense",
        uci_moves: &["d2d4", "g8f6", "c2c4", "g7g6"],
    },
    OpeningLine {
        name: "Nimzo-Indian Defense",
        uci_moves: &["d2d4", "g8f6", "c2c4", "e7e6", "b1c3", "f8b4"],
    },
    OpeningLine {
        name: "Dutch Defense",
        uci_moves: &["d2d4", "f7f5"],
    },
    OpeningLine {
        name: "English Opening",
        uci_moves: &["c2c4"],
    },
    OpeningLine {
        name: "Reti Opening",
        uci_moves: &["g1f3"],
    },
    OpeningLine {
        name: "London System",
        uci_moves: &["d2d4", "d7d5", "g1f3", "g8f6", "c1f4"],
    },
];

fn phase_names(position: &Position, move_log: &[String]) -> PhaseNames {
    let phase = classify_phase(position, move_log.len());
    let opening_name = detect_opening(move_log).unwrap_or("Unclassified Opening");

    let middlegame_name = if phase == GamePhase::Opening {
        "Not reached yet"
    } else {
        middlegame_theme(position)
    };

    let endgame_name = if phase == GamePhase::Endgame {
        endgame_theme(position)
    } else {
        "Not reached yet"
    };

    PhaseNames {
        current_phase: phase.name(),
        opening_name,
        middlegame_name,
        endgame_name,
    }
}

fn detect_opening(move_log: &[String]) -> Option<&'static str> {
    let mut best: Option<&OpeningLine> = None;

    for line in OPENING_BOOK {
        if move_log.len() < line.uci_moves.len() {
            continue;
        }

        let matches = line
            .uci_moves
            .iter()
            .zip(move_log.iter())
            .all(|(expected, played)| *expected == played.as_str());

        if !matches {
            continue;
        }

        if best.is_none_or(|current| line.uci_moves.len() > current.uci_moves.len()) {
            best = Some(line);
        }
    }

    best.map(|line| line.name)
}

fn classify_phase(position: &Position, move_count: usize) -> GamePhase {
    let material = material_snapshot(position);

    if is_endgame(material) {
        return GamePhase::Endgame;
    }

    if move_count <= 20 && material.non_pawn_material >= 6_200 && material.total_piece_count >= 24 {
        return GamePhase::Opening;
    }

    GamePhase::Middlegame
}

fn is_endgame(material: MaterialSnapshot) -> bool {
    let queen_count = material.white_queens + material.black_queens;
    let minor_count = material.bishops + material.knights;

    if material.non_pawn_material <= 2_400 {
        return true;
    }

    if queen_count == 0 && material.rooks <= 2 && minor_count <= 4 {
        return true;
    }

    if queen_count <= 1 && material.rooks <= 1 && minor_count <= 2 {
        return true;
    }

    false
}

fn middlegame_theme(position: &Position) -> &'static str {
    let material = material_snapshot(position);
    let queen_count = material.white_queens + material.black_queens;

    if has_opposite_side_castling(position) {
        return "Opposite-Side Castling Attack";
    }

    if queen_count == 0 {
        return "Queenless Middlegame";
    }

    if center_is_closed(position) {
        return "Closed Center Middlegame";
    }

    if center_is_open(position) {
        return "Open Center Middlegame";
    }

    "Classical Piece Middlegame"
}

fn endgame_theme(position: &Position) -> &'static str {
    let material = material_snapshot(position);
    let queen_count = material.white_queens + material.black_queens;
    let minor_count = material.bishops + material.knights;

    if material.total_piece_count == 2 {
        return "Bare Kings";
    }

    if material.non_pawn_material <= 200 && material.pawns > 0 {
        return "King and Pawn Endgame";
    }

    if is_opposite_colored_bishop_endgame(position) {
        return "Opposite-Colored Bishops Endgame";
    }

    if material.rooks > 0 && queen_count == 0 && minor_count <= 2 {
        return "Rook Endgame";
    }

    if queen_count > 0 && material.rooks <= 1 && minor_count <= 2 {
        return "Queen Endgame";
    }

    if minor_count > 0 && material.rooks == 0 && queen_count == 0 {
        return "Minor-Piece Endgame";
    }

    "Technical Endgame"
}

fn material_snapshot(position: &Position) -> MaterialSnapshot {
    let mut snapshot = MaterialSnapshot::default();

    for (_, piece) in position.pieces() {
        snapshot.total_piece_count = snapshot.total_piece_count.saturating_add(1);
        match piece.kind {
            PieceKind::Pawn => snapshot.pawns = snapshot.pawns.saturating_add(1),
            PieceKind::Knight => {
                snapshot.knights = snapshot.knights.saturating_add(1);
                snapshot.non_pawn_material += 320;
            }
            PieceKind::Bishop => {
                snapshot.bishops = snapshot.bishops.saturating_add(1);
                snapshot.non_pawn_material += 330;
            }
            PieceKind::Rook => {
                snapshot.rooks = snapshot.rooks.saturating_add(1);
                snapshot.non_pawn_material += 500;
            }
            PieceKind::Queen => {
                if piece.color == Color::White {
                    snapshot.white_queens = snapshot.white_queens.saturating_add(1);
                } else {
                    snapshot.black_queens = snapshot.black_queens.saturating_add(1);
                }
                snapshot.non_pawn_material += 900;
            }
            PieceKind::King => {}
        }
    }

    snapshot
}

fn has_opposite_side_castling(position: &Position) -> bool {
    let white_king = king_square(position, Color::White);
    let black_king = king_square(position, Color::Black);
    let (Some(white_king), Some(black_king)) = (white_king, black_king) else {
        return false;
    };

    let white_side = castled_side(white_king, Color::White);
    let black_side = castled_side(black_king, Color::Black);

    matches!((white_side, black_side), (Some(a), Some(b)) if a != b)
}

fn castled_side(king_square: u8, color: Color) -> Option<i8> {
    match (color, king_square) {
        (Color::White, 6) | (Color::Black, 62) => Some(1),
        (Color::White, 2) | (Color::Black, 58) => Some(-1),
        _ => None,
    }
}

fn king_square(position: &Position, color: Color) -> Option<u8> {
    position.pieces().find_map(|(square, piece)| {
        if piece.color == color && piece.kind == PieceKind::King {
            Some(square)
        } else {
            None
        }
    })
}

fn center_is_open(position: &Position) -> bool {
    let central_pawns = position
        .pieces()
        .filter(|(square, piece)| {
            piece.kind == PieceKind::Pawn && ((*square % 8) == 3 || (*square % 8) == 4)
        })
        .count();
    central_pawns <= 1
}

fn center_is_closed(position: &Position) -> bool {
    let d4_locked =
        is_pawn_at(position, 27, Color::White) && is_pawn_at(position, 35, Color::Black);
    let e4_locked =
        is_pawn_at(position, 28, Color::White) && is_pawn_at(position, 36, Color::Black);
    d4_locked || e4_locked
}

fn is_pawn_at(position: &Position, square: u8, color: Color) -> bool {
    position.piece_at(square)
        == Some(Piece {
            color,
            kind: PieceKind::Pawn,
        })
}

fn is_opposite_colored_bishop_endgame(position: &Position) -> bool {
    let material = material_snapshot(position);
    if material.rooks > 0
        || material.white_queens + material.black_queens > 0
        || material.knights > 0
    {
        return false;
    }

    let mut white_bishop_square = None;
    let mut black_bishop_square = None;

    for (square, piece) in position.pieces() {
        if piece.kind != PieceKind::Bishop {
            continue;
        }

        match piece.color {
            Color::White => {
                if white_bishop_square.is_some() {
                    return false;
                }
                white_bishop_square = Some(square);
            }
            Color::Black => {
                if black_bishop_square.is_some() {
                    return false;
                }
                black_bishop_square = Some(square);
            }
        }
    }

    let (Some(white_square), Some(black_square)) = (white_bishop_square, black_bishop_square)
    else {
        return false;
    };

    square_color(white_square) != square_color(black_square)
}

fn square_color(square: u8) -> u8 {
    let file = square % 8;
    let rank = square / 8;
    (file + rank) % 2
}

fn load_piece_textures(ctx: &egui::Context) -> HashMap<Piece, egui::TextureHandle> {
    let mut textures = HashMap::new();

    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::White,
            kind: PieceKind::King,
        },
        "piece_wk",
        include_bytes!("../assets/pieces/wK.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::White,
            kind: PieceKind::Queen,
        },
        "piece_wq",
        include_bytes!("../assets/pieces/wQ.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::White,
            kind: PieceKind::Rook,
        },
        "piece_wr",
        include_bytes!("../assets/pieces/wR.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::White,
            kind: PieceKind::Bishop,
        },
        "piece_wb",
        include_bytes!("../assets/pieces/wB.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::White,
            kind: PieceKind::Knight,
        },
        "piece_wn",
        include_bytes!("../assets/pieces/wN.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::White,
            kind: PieceKind::Pawn,
        },
        "piece_wp",
        include_bytes!("../assets/pieces/wP.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::Black,
            kind: PieceKind::King,
        },
        "piece_bk",
        include_bytes!("../assets/pieces/bK.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::Black,
            kind: PieceKind::Queen,
        },
        "piece_bq",
        include_bytes!("../assets/pieces/bQ.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::Black,
            kind: PieceKind::Rook,
        },
        "piece_br",
        include_bytes!("../assets/pieces/bR.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::Black,
            kind: PieceKind::Bishop,
        },
        "piece_bb",
        include_bytes!("../assets/pieces/bB.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::Black,
            kind: PieceKind::Knight,
        },
        "piece_bn",
        include_bytes!("../assets/pieces/bN.svg"),
    );
    register_piece_texture(
        &mut textures,
        ctx,
        Piece {
            color: Color::Black,
            kind: PieceKind::Pawn,
        },
        "piece_bp",
        include_bytes!("../assets/pieces/bP.svg"),
    );

    textures
}

fn register_piece_texture(
    textures: &mut HashMap<Piece, egui::TextureHandle>,
    ctx: &egui::Context,
    piece: Piece,
    texture_name: &str,
    svg_data: &[u8],
) {
    let texture = render_svg_texture(ctx, texture_name, svg_data);
    textures.insert(piece, texture);
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
