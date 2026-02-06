use crate::chess::{Color, Piece, PieceKind, Position};

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
pub struct PhaseNames {
    pub current_phase: &'static str,
    pub opening_name: &'static str,
    pub middlegame_name: &'static str,
    pub endgame_name: &'static str,
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

pub fn phase_names(position: &Position, move_log: &[String]) -> PhaseNames {
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
