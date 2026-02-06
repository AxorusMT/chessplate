use std::collections::HashMap;
use std::sync::mpsc::{self, Receiver};
use std::thread;
use std::time::{Duration, Instant};

use eframe::egui::{self, Color32, PointerButton, Pos2, Rect, RichText, Stroke, Vec2};

use crate::chess::{
    Color, GameStatus, Move, Piece, PieceKind, Position, move_to_coord, move_to_san,
    piece_to_unicode,
};
use crate::engine::{EngineConfig, SearchSnapshot, best_move_with_telemetry, profile_for_elo};

mod game_insight;
mod metrics;
mod pieces;
mod theme;

use self::game_insight::phase_names;
use self::metrics::{best_move_stability, eval_volatility_cp, format_big_number};
use self::pieces::load_piece_textures;
use self::theme::{BoardTheme, ThemePalette, all_themes};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct DrawnArrow {
    from: u8,
    to: u8,
}

enum EngineEvent {
    Snapshot(SearchSnapshot),
    Done(Option<Move>),
}

const SEARCH_SNAPSHOT_LIMIT: usize = 256;
const COPY_FEEDBACK_SECONDS: f32 = 1.8;

pub struct ChessApp {
    position: Position,
    selected_square: Option<u8>,
    selected_moves: Vec<Move>,
    theme_index: usize,
    human_color: Color,
    bot_elo: u16,
    status: GameStatus,
    move_log: Vec<String>,
    move_history: Vec<Move>,
    thinking: bool,
    engine_rx: Option<Receiver<EngineEvent>>,
    piece_textures: HashMap<Piece, egui::TextureHandle>,
    drawn_arrows: Vec<DrawnArrow>,
    arrow_drag_start: Option<u8>,
    arrow_drag_current: Option<u8>,
    search_snapshots: Vec<SearchSnapshot>,
    search_root: Option<Position>,
    copy_feedback: Option<(String, Instant)>,
    queued_premove: Option<Move>,
}

impl ChessApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        let position = Position::startpos();
        Self {
            status: position.game_status(),
            position,
            selected_square: None,
            selected_moves: Vec::new(),
            theme_index: 0,
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
            search_snapshots: Vec::new(),
            search_root: None,
            copy_feedback: None,
            queued_premove: None,
        }
    }

    fn bot_color(&self) -> Color {
        self.human_color.opposite()
    }

    fn current_theme(&self) -> &'static BoardTheme {
        let themes = all_themes();
        themes
            .get(self.theme_index)
            .or_else(|| themes.first())
            .expect("theme catalog must not be empty")
    }

    fn new_game(&mut self) {
        self.position = Position::startpos();
        self.clear_selection();
        self.move_log.clear();
        self.move_history.clear();
        self.thinking = false;
        self.engine_rx = None;
        self.drawn_arrows.clear();
        self.arrow_drag_start = None;
        self.arrow_drag_current = None;
        self.search_snapshots.clear();
        self.search_root = None;
        self.copy_feedback = None;
        self.queued_premove = None;
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

        let mut finished_move = None;
        while let Ok(event) = receiver.try_recv() {
            match event {
                EngineEvent::Snapshot(snapshot) => {
                    self.search_snapshots.push(snapshot);
                    if self.search_snapshots.len() > SEARCH_SNAPSHOT_LIMIT {
                        let excess = self.search_snapshots.len() - SEARCH_SNAPSHOT_LIMIT;
                        self.search_snapshots.drain(0..excess);
                    }
                }
                EngineEvent::Done(mv) => {
                    finished_move = Some(mv);
                }
            }
        }

        if let Some(engine_move) = finished_move {
            self.engine_rx = None;
            self.thinking = false;
            if let Some(mv) = engine_move {
                self.apply_move(mv);
                self.try_execute_premove();
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

        self.search_root = Some(position.clone());
        self.search_snapshots.clear();
        self.engine_rx = Some(rx);
        self.thinking = true;

        thread::spawn(move || {
            let mv = best_move_with_telemetry(&position, config, |snapshot| {
                let _ = tx.send(EngineEvent::Snapshot(snapshot));
            });
            let _ = tx.send(EngineEvent::Done(mv));
        });
    }

    fn apply_move(&mut self, mv: Move) {
        let notation = move_to_coord(mv);
        self.position.make_move(mv);
        self.move_log.push(notation);
        self.move_history.push(mv);
        self.clear_selection();
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

    fn set_copy_feedback(&mut self, message: &str) {
        self.copy_feedback = Some((message.to_owned(), Instant::now()));
    }

    fn clear_selection(&mut self) {
        self.selected_square = None;
        self.selected_moves.clear();
    }

    fn preferred_move(candidates: &[Move]) -> Move {
        candidates
            .iter()
            .copied()
            .find(|mv| mv.promotion == Some(PieceKind::Queen))
            .unwrap_or(candidates[0])
    }

    fn set_selection_from_legal(&mut self, square: u8, legal: Vec<Move>) {
        self.selected_square = Some(square);
        self.selected_moves = legal
            .into_iter()
            .filter(|mv| mv.from == square)
            .collect::<Vec<_>>();

        if self.selected_moves.is_empty() {
            self.clear_selection();
        }
    }

    fn set_selection(&mut self, square: u8) {
        self.set_selection_from_legal(square, self.position.legal_moves());
    }

    fn set_premove_selection(&mut self, square: u8) {
        let mut preview = self.position.clone();
        preview.side_to_move = self.human_color;
        self.set_selection_from_legal(square, preview.legal_moves());
    }

    fn try_execute_premove(&mut self) -> bool {
        let Some(premove) = self.queued_premove else {
            return false;
        };

        self.queued_premove = None;
        self.clear_selection();

        if self.is_game_over() || !self.is_human_turn() {
            return false;
        }

        let legal = self.position.legal_moves();
        let candidates = legal
            .into_iter()
            .filter(|mv| mv.from == premove.from && mv.to == premove.to)
            .collect::<Vec<_>>();

        if candidates.is_empty() {
            return false;
        }

        let chosen = if let Some(promotion) = premove.promotion {
            candidates
                .iter()
                .copied()
                .find(|mv| mv.promotion == Some(promotion))
                .unwrap_or_else(|| Self::preferred_move(&candidates))
        } else {
            Self::preferred_move(&candidates)
        };

        self.apply_move(chosen);
        self.set_copy_feedback("Premove played");
        true
    }

    fn handle_live_turn_click(&mut self, square: u8) {
        if self.thinking || self.is_game_over() || !self.is_human_turn() {
            return;
        }

        if let Some(from_square) = self.selected_square {
            if from_square == square {
                self.clear_selection();
                return;
            }

            let candidates = self
                .selected_moves
                .iter()
                .copied()
                .filter(|mv| mv.from == from_square && mv.to == square)
                .collect::<Vec<_>>();

            if !candidates.is_empty() {
                let chosen = Self::preferred_move(&candidates);
                self.queued_premove = None;
                self.apply_move(chosen);
                return;
            }
        }

        if let Some(piece) = self.position.piece_at(square) {
            if piece.color == self.human_color {
                self.set_selection(square);
            } else {
                self.clear_selection();
            }
        } else {
            self.clear_selection();
        }
    }

    fn handle_premove_click(&mut self, square: u8) {
        if self.is_game_over() || self.is_human_turn() {
            return;
        }

        if let Some(from_square) = self.selected_square {
            if from_square == square {
                self.clear_selection();
                return;
            }

            let candidates = self
                .selected_moves
                .iter()
                .copied()
                .filter(|mv| mv.from == from_square && mv.to == square)
                .collect::<Vec<_>>();

            if !candidates.is_empty() {
                let chosen = Self::preferred_move(&candidates);
                if self.queued_premove == Some(chosen) {
                    self.queued_premove = None;
                    self.set_copy_feedback("Premove cleared");
                } else {
                    self.queued_premove = Some(chosen);
                    self.set_copy_feedback("Premove queued");
                }
                self.clear_selection();
                return;
            }
        }

        if let Some(piece) = self.position.piece_at(square) {
            if piece.color == self.human_color {
                self.set_premove_selection(square);
            } else {
                self.clear_selection();
            }
        } else {
            self.clear_selection();
        }
    }

    fn handle_square_click(&mut self, square: u8) {
        if self.is_human_turn() {
            self.handle_live_turn_click(square);
        } else {
            self.handle_premove_click(square);
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

    fn paint_arrows(
        &self,
        ui: &egui::Ui,
        board_rect: Rect,
        square_size: f32,
        palette: ThemePalette,
    ) {
        let painter = ui.painter_at(board_rect);

        for arrow in &self.drawn_arrows {
            let from = self.square_center(arrow.from, board_rect, square_size);
            let to = self.square_center(arrow.to, board_rect, square_size);
            Self::draw_arrow(&painter, from, to, palette.arrow, 6.0);
        }

        if let (Some(from_square), Some(to_square)) =
            (self.arrow_drag_start, self.arrow_drag_current)
        {
            let from = self.square_center(from_square, board_rect, square_size);
            let to = self.square_center(to_square, board_rect, square_size);
            Self::draw_arrow(&painter, from, to, palette.arrow_preview, 5.0);
        }
    }

    fn paint_premove_overlay(
        &self,
        ui: &egui::Ui,
        board_rect: Rect,
        square_size: f32,
        palette: ThemePalette,
    ) {
        let Some(premove) = self.queued_premove else {
            return;
        };

        let painter = ui.painter_at(board_rect);
        let from = self.square_center(premove.from, board_rect, square_size);
        let to = self.square_center(premove.to, board_rect, square_size);
        Self::draw_arrow(
            &painter,
            from,
            to,
            palette.arrow_preview.gamma_multiply(0.85),
            4.0,
        );

        let marker_size = Vec2::splat((square_size - 8.0).max(12.0));
        let stroke = Stroke::new(2.0, palette.arrow_preview.gamma_multiply(0.95));
        for square in [premove.from, premove.to] {
            let center = self.square_center(square, board_rect, square_size);
            let marker = Rect::from_center_size(center, marker_size);
            painter.rect_stroke(marker, 6.0, stroke);
        }
    }

    fn draw_thinking_visualization(&self, ui: &mut egui::Ui, panel_size: Vec2) {
        let palette = self.current_theme().palette;
        let (rect, _) = ui.allocate_exact_size(panel_size, egui::Sense::hover());
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 12.0, palette.viz_bg);

        let inner = rect.shrink2(Vec2::new(10.0, 10.0));
        let chart_height = inner.height() * 0.54;
        let chart_rect = Rect::from_min_max(
            inner.min,
            Pos2::new(inner.max.x, inner.min.y + chart_height),
        );
        let cards_rect =
            Rect::from_min_max(Pos2::new(inner.min.x, chart_rect.max.y + 8.0), inner.max);

        self.paint_search_chart(&painter, chart_rect, palette);
        self.paint_search_cards(&painter, cards_rect, palette);
    }

    fn paint_search_chart(&self, painter: &egui::Painter, rect: Rect, palette: ThemePalette) {
        painter.rect_filled(rect, 8.0, Color32::from_rgba_unmultiplied(8, 10, 16, 40));

        let zero_y = rect.center().y;
        painter.line_segment(
            [Pos2::new(rect.min.x, zero_y), Pos2::new(rect.max.x, zero_y)],
            Stroke::new(1.0, palette.viz_grid),
        );

        for idx in 1..8 {
            let t = idx as f32 / 8.0;
            let x = rect.min.x + rect.width() * t;
            painter.line_segment(
                [Pos2::new(x, rect.min.y), Pos2::new(x, rect.max.y)],
                Stroke::new(1.0, palette.viz_grid),
            );
        }

        if self.search_snapshots.is_empty() {
            let idle_text = if self.thinking {
                "Engine warming up..."
            } else {
                "No active search"
            };
            painter.text(
                rect.center(),
                egui::Align2::CENTER_CENTER,
                idle_text,
                egui::FontId::monospace(14.0),
                palette.viz_text_muted,
            );
            return;
        }

        let history = self
            .search_snapshots
            .iter()
            .rev()
            .take(96)
            .collect::<Vec<_>>();
        let history = history.into_iter().rev().collect::<Vec<_>>();
        let max_depth = history.iter().map(|s| s.depth).max().unwrap_or(1) as f32;

        let mut points = Vec::with_capacity(history.len());
        for (idx, snapshot) in history.iter().enumerate() {
            let x = if history.len() == 1 {
                rect.center().x
            } else {
                rect.min.x + rect.width() * (idx as f32 / (history.len() as f32 - 1.0))
            };

            let depth_height = (snapshot.depth as f32 / max_depth) * (rect.height() * 0.36);
            let bar_rect = Rect::from_min_max(
                Pos2::new(x - 1.5, rect.max.y - depth_height),
                Pos2::new(x + 1.5, rect.max.y),
            );
            painter.rect_filled(bar_rect, 1.0, palette.viz_depth);

            let score = snapshot.best_score_cp.clamp(-600, 600) as f32;
            let y = zero_y - (score / 600.0) * (rect.height() * 0.42);
            points.push(Pos2::new(x, y));
        }

        if points.len() >= 2 {
            painter.add(egui::Shape::line(
                points.clone(),
                Stroke::new(6.0, palette.viz_eval_glow),
            ));
        }

        let trend_color = if history.last().is_some_and(|s| s.best_score_cp >= 0) {
            palette.viz_eval_pos
        } else {
            palette.viz_eval_neg
        };
        painter.add(egui::Shape::line(
            points.clone(),
            Stroke::new(2.2, trend_color),
        ));

        if let Some(last) = points.last().copied() {
            painter.circle_filled(last, 4.5, trend_color);
            painter.circle_stroke(last, 7.5, Stroke::new(1.2, palette.viz_eval_glow));
        }
    }

    fn paint_search_cards(&self, painter: &egui::Painter, rect: Rect, palette: ThemePalette) {
        let Some(latest) = self.search_snapshots.last() else {
            painter.text(
                rect.center(),
                egui::Align2::CENTER_CENTER,
                "Waiting for search data",
                egui::FontId::monospace(13.0),
                palette.viz_text_muted,
            );
            return;
        };

        let elapsed_ms = latest.elapsed_ms.max(1);
        let nps = latest.nodes.saturating_mul(1000) / elapsed_ms as u64;
        let eval_pawns = latest.best_score_cp as f32 / 100.0;
        let stability = best_move_stability(&self.search_snapshots);
        let volatility = eval_volatility_cp(&self.search_snapshots);
        let gap_cp = latest
            .top_moves
            .get(1)
            .map(|second| latest.best_score_cp - second.score_cp)
            .unwrap_or(0);

        let left = rect.min.x;
        let mut y = rect.min.y + 2.0;
        let line_h = 15.0;

        painter.text(
            Pos2::new(left, y),
            egui::Align2::LEFT_TOP,
            "Search Prism",
            egui::FontId::proportional(16.0),
            palette.viz_text_main,
        );
        y += line_h + 2.0;

        let insight_lines = [
            format!("Depth: {}", latest.depth),
            format!("Nodes: {}", format_big_number(latest.nodes)),
            format!("NPS: {}", format_big_number(nps)),
            format!("Score: {:+.2} pawns", eval_pawns),
            format!("Stability: {:.0}%", stability * 100.0),
            format!("Volatility: {} cp", volatility.round() as i32),
            format!("Lead gap: {} cp", gap_cp),
        ];

        for line in insight_lines {
            painter.text(
                Pos2::new(left, y),
                egui::Align2::LEFT_TOP,
                line,
                egui::FontId::monospace(12.0),
                palette.viz_text_muted,
            );
            y += line_h;
        }

        y += 4.0;
        painter.text(
            Pos2::new(left, y),
            egui::Align2::LEFT_TOP,
            "Top Candidates",
            egui::FontId::proportional(14.0),
            palette.viz_text_main,
        );
        y += line_h;

        let root_position = self.search_root.as_ref();
        let best_abs = latest
            .top_moves
            .first()
            .map(|m| m.score_cp.abs())
            .unwrap_or(1)
            .max(1) as f32;

        for candidate in latest.top_moves.iter().take(3) {
            let card_rect =
                Rect::from_min_max(Pos2::new(left, y), Pos2::new(rect.max.x - 2.0, y + 18.0));
            painter.rect_filled(card_rect, 6.0, palette.viz_card);

            let move_text = if let Some(root) = root_position {
                move_to_san(root, candidate.mv)
            } else {
                move_to_coord(candidate.mv)
            };
            let ratio = (candidate.score_cp.abs() as f32 / best_abs).clamp(0.0, 1.0);
            let meter_width = (card_rect.width() - 72.0) * ratio;
            let meter_rect = Rect::from_min_max(
                Pos2::new(
                    card_rect.right() - meter_width - 4.0,
                    card_rect.top() + 13.0,
                ),
                Pos2::new(card_rect.right() - 4.0, card_rect.bottom() - 4.0),
            );
            let move_color = if candidate.score_cp >= 0 {
                palette.viz_eval_pos
            } else {
                palette.viz_eval_neg
            };
            painter.rect_filled(meter_rect, 2.0, move_color);

            painter.text(
                Pos2::new(card_rect.left() + 6.0, card_rect.top() + 2.0),
                egui::Align2::LEFT_TOP,
                format!("{move_text}  ({:+}cp)", candidate.score_cp),
                egui::FontId::monospace(11.5),
                palette.viz_text_main,
            );

            y += 22.0;
            if y > rect.max.y - 20.0 {
                break;
            }
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
        egui::ComboBox::from_label("Color Theme")
            .selected_text(self.current_theme().name.as_str())
            .show_ui(ui, |ui| {
                for (idx, theme) in all_themes().iter().enumerate() {
                    ui.selectable_value(&mut self.theme_index, idx, theme.name.as_str());
                }
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
                self.set_copy_feedback("Copied FEN state");
            }
            if ui.button("Copy Game").clicked() {
                ui.ctx().copy_text(self.game_to_pgn());
                self.set_copy_feedback("Copied PGN game");
            }
        });
        if let Some((message, _)) = &self.copy_feedback {
            ui.label(RichText::new(message).small().italics());
        }
        if let Some(premove) = self.queued_premove {
            ui.horizontal_wrapped(|ui| {
                ui.label(format!("Queued premove: {}", move_to_coord(premove)));
                if ui.small_button("Clear").clicked() {
                    self.queued_premove = None;
                    self.clear_selection();
                }
            });
        } else if !self.is_human_turn() && !self.is_game_over() {
            ui.label(RichText::new("Premove: queue your move while the engine thinks.").small());
        }

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

    fn draw_board(&mut self, ui: &mut egui::Ui, square_size: f32) {
        let piece_image_size = Vec2::splat((square_size - 10.0).max(30.0));
        let palette = self.current_theme().palette;

        let selected = self.selected_square;
        let mut selected_targets = [false; 64];
        for mv in &self.selected_moves {
            selected_targets[mv.to as usize] = true;
        }

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
                                    palette.board_light
                                } else {
                                    palette.board_dark
                                };

                                if Some(square) == selected {
                                    fill = palette.selected;
                                } else if selected_targets[square as usize] {
                                    fill = palette.target;
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
        self.paint_premove_overlay(ui, board_rect, square_size, palette);
        self.paint_arrows(ui, board_rect, square_size, palette);
    }
}

impl eframe::App for ChessApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if self
            .copy_feedback
            .as_ref()
            .is_some_and(|(_, at)| at.elapsed().as_secs_f32() > COPY_FEEDBACK_SECONDS)
        {
            self.copy_feedback = None;
        }

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
                let gap = 12.0;
                let total_width = ui.available_width();
                let max_square = 72.0;
                let min_square = 46.0;
                let desired_viz_width = 260.0;
                let min_viz_width = 150.0;

                let mut square_size = max_square;
                let mut viz_width = desired_viz_width;
                if total_width < (max_square * 8.0 + desired_viz_width + gap) {
                    square_size = ((total_width - desired_viz_width - gap) / 8.0)
                        .floor()
                        .clamp(min_square, max_square);
                    viz_width = (total_width - (square_size * 8.0) - gap)
                        .max(min_viz_width)
                        .min(desired_viz_width);
                }

                let board_size = square_size * 8.0;
                ui.horizontal(|ui| {
                    self.draw_board(ui, square_size);
                    ui.add_space(gap);
                    self.draw_thinking_visualization(ui, Vec2::new(viz_width, board_size));
                });
            });

        if self.thinking {
            ctx.request_repaint_after(Duration::from_millis(30));
        }
    }
}
