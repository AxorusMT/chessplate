use std::time::{Duration, Instant};

use rand::{Rng, thread_rng};

use crate::chess::{Color, Move, PieceKind, Position};

const INF: i32 = 1_000_000;
const MATE_SCORE: i32 = 100_000;

#[derive(Clone, Copy, Debug)]
pub struct EngineConfig {
    pub elo: u16,
}

#[derive(Clone, Copy, Debug)]
pub struct EloProfile {
    pub depth: u8,
    pub time_ms: u64,
    pub node_limit: u64,
    pub randomness_cp: i32,
}

#[derive(Clone, Copy, Debug)]
pub struct ScoredMove {
    pub mv: Move,
    pub score_cp: i32,
}

#[derive(Clone, Debug)]
pub struct SearchSnapshot {
    pub depth: u8,
    pub elapsed_ms: u128,
    pub nodes: u64,
    pub best_move: Move,
    pub best_score_cp: i32,
    pub top_moves: Vec<ScoredMove>,
}

#[derive(Clone, Copy, Debug)]
struct SearchLimits {
    max_depth: u8,
    deadline: Instant,
    node_limit: u64,
    randomness_cp: i32,
}

pub fn profile_for_elo(elo: u16) -> EloProfile {
    let clamped = elo.clamp(600, 2800);

    let depth = match clamped {
        600..=899 => 2,
        900..=1199 => 3,
        1200..=1499 => 4,
        1500..=1799 => 5,
        1800..=2099 => 6,
        2100..=2399 => 7,
        _ => 8,
    };

    let time_ms = match clamped {
        600..=899 => 150,
        900..=1199 => 250,
        1200..=1499 => 400,
        1500..=1799 => 600,
        1800..=2099 => 900,
        2100..=2399 => 1_300,
        _ => 1_800,
    };

    let node_limit = match clamped {
        600..=899 => 35_000,
        900..=1199 => 70_000,
        1200..=1499 => 150_000,
        1500..=1799 => 300_000,
        1800..=2099 => 700_000,
        2100..=2399 => 1_300_000,
        _ => 2_200_000,
    };

    let randomness_cp = ((2800_i32 - i32::from(clamped)) / 8).max(0);

    EloProfile {
        depth,
        time_ms,
        node_limit,
        randomness_cp,
    }
}

impl EngineConfig {
    pub fn new(elo: u16) -> Self {
        Self {
            elo: elo.clamp(600, 2800),
        }
    }

    fn limits(self) -> SearchLimits {
        let profile = profile_for_elo(self.elo);
        SearchLimits {
            max_depth: profile.depth,
            deadline: Instant::now() + Duration::from_millis(profile.time_ms),
            node_limit: profile.node_limit,
            randomness_cp: profile.randomness_cp,
        }
    }
}

pub fn best_move_with_telemetry<F>(
    position: &Position,
    config: EngineConfig,
    mut on_snapshot: F,
) -> Option<Move>
where
    F: FnMut(SearchSnapshot),
{
    let mut searcher = Searcher::new(config.limits());
    searcher.search(position, &mut on_snapshot)
}

struct Searcher {
    limits: SearchLimits,
    nodes: u64,
    stopped: bool,
    started_at: Instant,
}

impl Searcher {
    fn new(limits: SearchLimits) -> Self {
        Self {
            limits,
            nodes: 0,
            stopped: false,
            started_at: Instant::now(),
        }
    }

    fn search<F>(&mut self, position: &Position, on_snapshot: &mut F) -> Option<Move>
    where
        F: FnMut(SearchSnapshot),
    {
        let mut root_moves = position.legal_moves();
        if root_moves.is_empty() {
            return None;
        }

        self.order_moves(position, &mut root_moves);

        let mut completed_scores: Option<Vec<(Move, i32)>> = None;

        for depth in 1..=self.limits.max_depth {
            if self.should_stop() {
                break;
            }

            let mut depth_scores = Vec::with_capacity(root_moves.len());
            let mut aborted = false;
            let mut alpha = -INF;
            let beta = INF;

            for mv in root_moves.iter().copied() {
                if self.should_stop() {
                    aborted = true;
                    break;
                }

                let mut next = position.clone();
                next.make_move(mv);
                let score = -self.negamax(&next, depth.saturating_sub(1), -beta, -alpha, 1);
                depth_scores.push((mv, score));
                if score > alpha {
                    alpha = score;
                }
            }

            if aborted {
                break;
            }

            depth_scores.sort_by(|a, b| b.1.cmp(&a.1));
            if let Some((best_move, best_score_cp)) = depth_scores.first().copied() {
                on_snapshot(SearchSnapshot {
                    depth,
                    elapsed_ms: self.started_at.elapsed().as_millis(),
                    nodes: self.nodes,
                    best_move,
                    best_score_cp,
                    top_moves: depth_scores
                        .iter()
                        .take(4)
                        .map(|(mv, score_cp)| ScoredMove {
                            mv: *mv,
                            score_cp: *score_cp,
                        })
                        .collect(),
                });
            }
            root_moves = depth_scores.iter().map(|(mv, _)| *mv).collect();
            completed_scores = Some(depth_scores);
        }

        if let Some(scores) = completed_scores {
            Some(self.pick_with_noise(&scores))
        } else {
            Some(root_moves[0])
        }
    }

    fn negamax(
        &mut self,
        position: &Position,
        depth: u8,
        mut alpha: i32,
        beta: i32,
        ply: u8,
    ) -> i32 {
        self.touch_node();
        if self.stopped {
            return evaluate(position);
        }

        if depth == 0 {
            return self.quiescence(position, alpha, beta, ply);
        }

        let mut moves = position.legal_moves();
        if moves.is_empty() {
            if position.is_in_check(position.side_to_move) {
                return -MATE_SCORE + i32::from(ply);
            }
            return 0;
        }

        self.order_moves(position, &mut moves);

        let mut best_score = -INF;
        for mv in moves {
            if self.should_stop() {
                break;
            }

            let mut next = position.clone();
            next.make_move(mv);
            let score = -self.negamax(&next, depth - 1, -beta, -alpha, ply.saturating_add(1));

            if score > best_score {
                best_score = score;
            }
            if score > alpha {
                alpha = score;
            }
            if alpha >= beta {
                break;
            }
        }

        if best_score == -INF {
            evaluate(position)
        } else {
            best_score
        }
    }

    fn quiescence(&mut self, position: &Position, mut alpha: i32, beta: i32, ply: u8) -> i32 {
        self.touch_node();
        if self.stopped {
            return evaluate(position);
        }

        let in_check = position.is_in_check(position.side_to_move);
        if !in_check {
            let stand_pat = evaluate(position);
            if stand_pat >= beta {
                return beta;
            }
            if stand_pat > alpha {
                alpha = stand_pat;
            }
        }

        let mut moves = if in_check {
            position.legal_moves()
        } else {
            position.tactical_moves()
        };

        if moves.is_empty() {
            if in_check {
                return -MATE_SCORE + i32::from(ply);
            }
            return alpha;
        }

        self.order_moves(position, &mut moves);

        for mv in moves {
            if self.should_stop() {
                break;
            }

            let mut next = position.clone();
            next.make_move(mv);
            let score = -self.quiescence(&next, -beta, -alpha, ply.saturating_add(1));
            if score >= beta {
                return beta;
            }
            if score > alpha {
                alpha = score;
            }
        }

        alpha
    }

    fn order_moves(&self, position: &Position, moves: &mut [Move]) {
        moves.sort_by(|a, b| move_score(position, *b).cmp(&move_score(position, *a)));
    }

    fn pick_with_noise(&self, scored_moves: &[(Move, i32)]) -> Move {
        let randomness = self.limits.randomness_cp;
        if randomness <= 0 {
            return scored_moves[0].0;
        }

        let mut rng = thread_rng();
        let candidate_count = scored_moves.len().min(5);
        let mut best_move = scored_moves[0].0;
        let mut best_adjusted = i32::MIN;

        for (mv, score) in scored_moves.iter().take(candidate_count).copied() {
            let noise = rng.gen_range(-randomness..=randomness);
            let adjusted = score + noise;
            if adjusted > best_adjusted {
                best_adjusted = adjusted;
                best_move = mv;
            }
        }

        best_move
    }

    fn touch_node(&mut self) {
        self.nodes = self.nodes.saturating_add(1);
        if self.nodes >= self.limits.node_limit {
            self.stopped = true;
            return;
        }

        if self.nodes & 0x3ff == 0 {
            self.stopped = Instant::now() >= self.limits.deadline;
        }
    }

    fn should_stop(&mut self) -> bool {
        if self.stopped {
            return true;
        }

        if self.nodes >= self.limits.node_limit {
            self.stopped = true;
            return true;
        }

        if Instant::now() >= self.limits.deadline {
            self.stopped = true;
            return true;
        }

        false
    }
}

fn move_score(position: &Position, mv: Move) -> i32 {
    let mut score = 0;

    if let Some(promotion) = mv.promotion {
        score += 20_000 + piece_value(promotion);
    }

    if mv.is_castling {
        score += 500;
    }

    if position.is_capture(mv) {
        let attacker = position
            .piece_at(mv.from)
            .map(|p| p.kind)
            .unwrap_or(PieceKind::Pawn);
        let captured = if mv.is_en_passant {
            PieceKind::Pawn
        } else {
            position
                .piece_at(mv.to)
                .map(|p| p.kind)
                .unwrap_or(PieceKind::Pawn)
        };
        score += 10_000 + piece_value(captured) - (piece_value(attacker) / 10);
    }

    score
}

fn evaluate(position: &Position) -> i32 {
    let mut white_score = 0;
    let mut black_score = 0;
    let mut white_bishops = 0;
    let mut black_bishops = 0;

    for (square, piece) in position.pieces() {
        let sq = square as usize;
        let base = piece_value(piece.kind);
        let psqt = piece_square_value(piece.kind, piece.color, sq);
        let score = base + psqt;

        match piece.color {
            Color::White => {
                white_score += score;
                if piece.kind == PieceKind::Bishop {
                    white_bishops += 1;
                }
            }
            Color::Black => {
                black_score += score;
                if piece.kind == PieceKind::Bishop {
                    black_bishops += 1;
                }
            }
        }
    }

    if white_bishops >= 2 {
        white_score += 30;
    }
    if black_bishops >= 2 {
        black_score += 30;
    }

    let mut score = white_score - black_score;

    if position.side_to_move == Color::White {
        score += 10;
        score
    } else {
        score -= 10;
        -score
    }
}

fn piece_value(kind: PieceKind) -> i32 {
    match kind {
        PieceKind::Pawn => 100,
        PieceKind::Knight => 320,
        PieceKind::Bishop => 330,
        PieceKind::Rook => 500,
        PieceKind::Queen => 900,
        PieceKind::King => 20_000,
    }
}

fn piece_square_value(kind: PieceKind, color: Color, square: usize) -> i32 {
    let index = if color == Color::White {
        square
    } else {
        mirror_square(square)
    };

    match kind {
        PieceKind::Pawn => PAWN_PSQ[index],
        PieceKind::Knight => KNIGHT_PSQ[index],
        PieceKind::Bishop => BISHOP_PSQ[index],
        PieceKind::Rook => ROOK_PSQ[index],
        PieceKind::Queen => QUEEN_PSQ[index],
        PieceKind::King => KING_PSQ[index],
    }
}

fn mirror_square(square: usize) -> usize {
    let file = square % 8;
    let rank = square / 8;
    let mirrored_rank = 7 - rank;
    (mirrored_rank * 8) + file
}

const PAWN_PSQ: [i32; 64] = [
    0, 0, 0, 0, 0, 0, 0, 0, 50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20, 10, 10, 5, 5,
    10, 27, 27, 10, 5, 5, 0, 0, 0, 25, 25, 0, 0, 0, 5, -5, -10, 0, 0, -10, -5, 5, 5, 10, 10, -25,
    -25, 10, 10, 5, 0, 0, 0, 0, 0, 0, 0, 0,
];

const KNIGHT_PSQ: [i32; 64] = [
    -50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0, 0, 0, 0, -20, -40, -30, 0, 10, 15, 15, 10,
    0, -30, -30, 5, 15, 20, 20, 15, 5, -30, -30, 0, 15, 20, 20, 15, 0, -30, -30, 5, 10, 15, 15, 10,
    5, -30, -40, -20, 0, 5, 5, 0, -20, -40, -50, -40, -30, -30, -30, -30, -40, -50,
];

const BISHOP_PSQ: [i32; 64] = [
    -20, -10, -10, -10, -10, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10, -10, 0, 5, 10, 10, 5, 0,
    -10, -10, 5, 5, 10, 10, 5, 5, -10, -10, 0, 10, 10, 10, 10, 0, -10, -10, 10, 10, 10, 10, 10, 10,
    -10, -10, 5, 0, 0, 0, 0, 5, -10, -20, -10, -10, -10, -10, -10, -10, -20,
];

const ROOK_PSQ: [i32; 64] = [
    0, 0, 0, 0, 0, 0, 0, 0, 5, 10, 10, 10, 10, 10, 10, 5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0,
    0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, 0, 0,
    0, 5, 5, 0, 0, 0,
];

const QUEEN_PSQ: [i32; 64] = [
    -20, -10, -10, -5, -5, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10, -10, 0, 5, 5, 5, 5, 0, -10,
    -5, 0, 5, 5, 5, 5, 0, -5, 0, 0, 5, 5, 5, 5, 0, -5, -10, 5, 5, 5, 5, 5, 0, -10, -10, 0, 5, 0, 0,
    0, 0, -10, -20, -10, -10, -5, -5, -10, -10, -20,
];

const KING_PSQ: [i32; 64] = [
    -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40,
    -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -20, -30, -30, -40, -40, -30,
    -30, -20, -10, -20, -20, -20, -20, -20, -20, -10, 20, 20, 0, 0, 0, 0, 20, 20, 20, 30, 10, 0, 0,
    10, 30, 20,
];
