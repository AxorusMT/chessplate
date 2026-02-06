use std::fmt;

pub const CASTLE_WHITE_KINGSIDE: u8 = 0b0001;
pub const CASTLE_WHITE_QUEENSIDE: u8 = 0b0010;
pub const CASTLE_BLACK_KINGSIDE: u8 = 0b0100;
pub const CASTLE_BLACK_QUEENSIDE: u8 = 0b1000;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum Color {
    White,
    Black,
}

impl Color {
    pub fn opposite(self) -> Self {
        match self {
            Self::White => Self::Black,
            Self::Black => Self::White,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::White => "White",
            Self::Black => "Black",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PieceKind {
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Piece {
    pub color: Color,
    pub kind: PieceKind,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Move {
    pub from: u8,
    pub to: u8,
    pub promotion: Option<PieceKind>,
    pub is_en_passant: bool,
    pub is_castling: bool,
}

impl Move {
    pub fn new(from: u8, to: u8) -> Self {
        Self {
            from,
            to,
            promotion: None,
            is_en_passant: false,
            is_castling: false,
        }
    }
}

impl fmt::Display for Move {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&move_to_coord(*self))
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GameStatus {
    Ongoing,
    Checkmate(Color),
    Stalemate,
    DrawFiftyMove,
}

#[derive(Clone)]
pub struct Position {
    board: [Option<Piece>; 64],
    pub side_to_move: Color,
    pub castling_rights: u8,
    pub en_passant: Option<u8>,
    pub halfmove_clock: u16,
    pub fullmove_number: u16,
}

impl Position {
    pub fn startpos() -> Self {
        let mut board = [None; 64];

        let white_back_rank = [
            PieceKind::Rook,
            PieceKind::Knight,
            PieceKind::Bishop,
            PieceKind::Queen,
            PieceKind::King,
            PieceKind::Bishop,
            PieceKind::Knight,
            PieceKind::Rook,
        ];

        let black_back_rank = white_back_rank;

        for (file, kind) in white_back_rank.into_iter().enumerate() {
            board[file] = Some(Piece {
                color: Color::White,
                kind,
            });
            board[8 + file] = Some(Piece {
                color: Color::White,
                kind: PieceKind::Pawn,
            });
        }

        for (file, kind) in black_back_rank.into_iter().enumerate() {
            board[56 + file] = Some(Piece {
                color: Color::Black,
                kind,
            });
            board[48 + file] = Some(Piece {
                color: Color::Black,
                kind: PieceKind::Pawn,
            });
        }

        Self {
            board,
            side_to_move: Color::White,
            castling_rights: CASTLE_WHITE_KINGSIDE
                | CASTLE_WHITE_QUEENSIDE
                | CASTLE_BLACK_KINGSIDE
                | CASTLE_BLACK_QUEENSIDE,
            en_passant: None,
            halfmove_clock: 0,
            fullmove_number: 1,
        }
    }

    pub fn piece_at(&self, square: u8) -> Option<Piece> {
        self.board[square as usize]
    }

    pub fn pieces(&self) -> impl Iterator<Item = (u8, Piece)> + '_ {
        self.board
            .iter()
            .enumerate()
            .filter_map(|(index, piece)| piece.map(|p| (index as u8, p)))
    }

    pub fn legal_moves(&self) -> Vec<Move> {
        let mut pseudo = Vec::with_capacity(64);
        self.generate_pseudo_moves(&mut pseudo);

        let mut legal = Vec::with_capacity(pseudo.len());
        for mv in pseudo {
            let mut next = self.clone();
            if !next.make_move(mv) {
                continue;
            }
            if !next.is_in_check(self.side_to_move) {
                legal.push(mv);
            }
        }

        legal
    }

    pub fn tactical_moves(&self) -> Vec<Move> {
        self.legal_moves()
            .into_iter()
            .filter(|mv| self.is_capture(*mv) || mv.promotion.is_some())
            .collect()
    }

    pub fn is_capture(&self, mv: Move) -> bool {
        if mv.is_en_passant {
            return true;
        }
        self.piece_at(mv.to).is_some()
    }

    pub fn is_in_check(&self, color: Color) -> bool {
        let Some(king_square) = self.king_square(color) else {
            return false;
        };
        self.square_attacked(king_square, color.opposite())
    }

    pub fn game_status(&self) -> GameStatus {
        if self.halfmove_clock >= 100 {
            return GameStatus::DrawFiftyMove;
        }

        let legal_moves = self.legal_moves();
        if legal_moves.is_empty() {
            if self.is_in_check(self.side_to_move) {
                GameStatus::Checkmate(self.side_to_move.opposite())
            } else {
                GameStatus::Stalemate
            }
        } else {
            GameStatus::Ongoing
        }
    }

    pub fn make_move(&mut self, mv: Move) -> bool {
        let Some(mut moving_piece) = self.piece_at(mv.from) else {
            return false;
        };

        if moving_piece.color != self.side_to_move {
            return false;
        }

        let mover_color = moving_piece.color;

        let captured_piece = if mv.is_en_passant {
            let capture_square = match mover_color {
                Color::White => mv.to.saturating_sub(8),
                Color::Black => mv.to.saturating_add(8),
            };
            let captured = self.piece_at(capture_square);
            self.set_piece(capture_square, None);
            captured
        } else {
            self.piece_at(mv.to)
        };

        if let Some(captured) = captured_piece {
            if captured.kind == PieceKind::Rook {
                self.clear_castling_right_by_rook_square(mv.to);
            }
        }

        if moving_piece.kind == PieceKind::King {
            self.clear_castling_for_color(mover_color);
        }

        if moving_piece.kind == PieceKind::Rook {
            self.clear_castling_right_by_rook_square(mv.from);
        }

        self.set_piece(mv.from, None);

        if mv.is_castling {
            self.apply_castling_rook_move(mv);
        }

        if let Some(promoted_kind) = mv.promotion {
            moving_piece.kind = promoted_kind;
        }

        self.set_piece(mv.to, Some(moving_piece));

        self.en_passant = None;
        if moving_piece.kind == PieceKind::Pawn {
            let distance = (mv.to as i16 - mv.from as i16).abs();
            if distance == 16 {
                let ep_square = if mover_color == Color::White {
                    mv.from + 8
                } else {
                    mv.from - 8
                };
                self.en_passant = Some(ep_square);
            }
        }

        if moving_piece.kind == PieceKind::Pawn || captured_piece.is_some() {
            self.halfmove_clock = 0;
        } else {
            self.halfmove_clock = self.halfmove_clock.saturating_add(1);
        }

        if mover_color == Color::Black {
            self.fullmove_number = self.fullmove_number.saturating_add(1);
        }

        self.side_to_move = self.side_to_move.opposite();
        true
    }

    fn set_piece(&mut self, square: u8, piece: Option<Piece>) {
        self.board[square as usize] = piece;
    }

    fn generate_pseudo_moves(&self, moves: &mut Vec<Move>) {
        for (square, piece) in self.pieces() {
            if piece.color != self.side_to_move {
                continue;
            }

            match piece.kind {
                PieceKind::Pawn => self.generate_pawn_moves(square, piece.color, moves),
                PieceKind::Knight => self.generate_knight_moves(square, piece.color, moves),
                PieceKind::Bishop => self.generate_slider_moves(
                    square,
                    piece.color,
                    &[(1, 1), (1, -1), (-1, 1), (-1, -1)],
                    moves,
                ),
                PieceKind::Rook => self.generate_slider_moves(
                    square,
                    piece.color,
                    &[(1, 0), (-1, 0), (0, 1), (0, -1)],
                    moves,
                ),
                PieceKind::Queen => self.generate_slider_moves(
                    square,
                    piece.color,
                    &[
                        (1, 1),
                        (1, -1),
                        (-1, 1),
                        (-1, -1),
                        (1, 0),
                        (-1, 0),
                        (0, 1),
                        (0, -1),
                    ],
                    moves,
                ),
                PieceKind::King => {
                    self.generate_king_moves(square, piece.color, moves);
                    self.generate_castling_moves(square, piece.color, moves);
                }
            }
        }
    }

    fn generate_pawn_moves(&self, from: u8, color: Color, moves: &mut Vec<Move>) {
        let file = file_of(from);
        let rank = rank_of(from);

        let direction = if color == Color::White { 1 } else { -1 };
        let start_rank = if color == Color::White { 1 } else { 6 };
        let promotion_rank = if color == Color::White { 7 } else { 0 };

        let one_step_rank = rank + direction;
        if let Some(one_step_square) = to_square(file, one_step_rank) {
            if self.piece_at(one_step_square).is_none() {
                if one_step_rank == promotion_rank {
                    self.push_promotions(from, one_step_square, moves);
                } else {
                    moves.push(Move::new(from, one_step_square));

                    if rank == start_rank {
                        let two_step_rank = rank + (2 * direction);
                        if let Some(two_step_square) = to_square(file, two_step_rank) {
                            if self.piece_at(two_step_square).is_none() {
                                moves.push(Move::new(from, two_step_square));
                            }
                        }
                    }
                }
            }
        }

        for delta_file in [-1, 1] {
            let target_file = file + delta_file;
            let target_rank = rank + direction;
            let Some(target_square) = to_square(target_file, target_rank) else {
                continue;
            };

            if let Some(target_piece) = self.piece_at(target_square) {
                if target_piece.color != color {
                    if target_rank == promotion_rank {
                        self.push_promotions(from, target_square, moves);
                    } else {
                        moves.push(Move::new(from, target_square));
                    }
                }
                continue;
            }

            if self.en_passant == Some(target_square) {
                let mut mv = Move::new(from, target_square);
                mv.is_en_passant = true;
                moves.push(mv);
            }
        }
    }

    fn push_promotions(&self, from: u8, to: u8, moves: &mut Vec<Move>) {
        for promotion in [
            PieceKind::Queen,
            PieceKind::Rook,
            PieceKind::Bishop,
            PieceKind::Knight,
        ] {
            let mut mv = Move::new(from, to);
            mv.promotion = Some(promotion);
            moves.push(mv);
        }
    }

    fn generate_knight_moves(&self, from: u8, color: Color, moves: &mut Vec<Move>) {
        let file = file_of(from);
        let rank = rank_of(from);
        let jumps = [
            (1, 2),
            (2, 1),
            (2, -1),
            (1, -2),
            (-1, -2),
            (-2, -1),
            (-2, 1),
            (-1, 2),
        ];

        for (df, dr) in jumps {
            let target_file = file + df;
            let target_rank = rank + dr;
            let Some(target_square) = to_square(target_file, target_rank) else {
                continue;
            };

            if let Some(target_piece) = self.piece_at(target_square) {
                if target_piece.color == color {
                    continue;
                }
            }

            moves.push(Move::new(from, target_square));
        }
    }

    fn generate_slider_moves(
        &self,
        from: u8,
        color: Color,
        directions: &[(i8, i8)],
        moves: &mut Vec<Move>,
    ) {
        let file = file_of(from);
        let rank = rank_of(from);

        for (df, dr) in directions {
            let mut target_file = file + df;
            let mut target_rank = rank + dr;

            while let Some(target_square) = to_square(target_file, target_rank) {
                if let Some(target_piece) = self.piece_at(target_square) {
                    if target_piece.color != color {
                        moves.push(Move::new(from, target_square));
                    }
                    break;
                }

                moves.push(Move::new(from, target_square));

                target_file += df;
                target_rank += dr;
            }
        }
    }

    fn generate_king_moves(&self, from: u8, color: Color, moves: &mut Vec<Move>) {
        let file = file_of(from);
        let rank = rank_of(from);

        for df in -1..=1 {
            for dr in -1..=1 {
                if df == 0 && dr == 0 {
                    continue;
                }

                let target_file = file + df;
                let target_rank = rank + dr;
                let Some(target_square) = to_square(target_file, target_rank) else {
                    continue;
                };

                if let Some(target_piece) = self.piece_at(target_square) {
                    if target_piece.color == color {
                        continue;
                    }
                }

                moves.push(Move::new(from, target_square));
            }
        }
    }

    fn generate_castling_moves(&self, from: u8, color: Color, moves: &mut Vec<Move>) {
        if self.is_in_check(color) {
            return;
        }

        match color {
            Color::White => {
                if from != 4 {
                    return;
                }

                if self.castling_rights & CASTLE_WHITE_KINGSIDE != 0
                    && self.piece_at(5).is_none()
                    && self.piece_at(6).is_none()
                    && self.piece_at(7)
                        == Some(Piece {
                            color,
                            kind: PieceKind::Rook,
                        })
                    && !self.square_attacked(5, Color::Black)
                    && !self.square_attacked(6, Color::Black)
                {
                    let mut mv = Move::new(4, 6);
                    mv.is_castling = true;
                    moves.push(mv);
                }

                if self.castling_rights & CASTLE_WHITE_QUEENSIDE != 0
                    && self.piece_at(1).is_none()
                    && self.piece_at(2).is_none()
                    && self.piece_at(3).is_none()
                    && self.piece_at(0)
                        == Some(Piece {
                            color,
                            kind: PieceKind::Rook,
                        })
                    && !self.square_attacked(3, Color::Black)
                    && !self.square_attacked(2, Color::Black)
                {
                    let mut mv = Move::new(4, 2);
                    mv.is_castling = true;
                    moves.push(mv);
                }
            }
            Color::Black => {
                if from != 60 {
                    return;
                }

                if self.castling_rights & CASTLE_BLACK_KINGSIDE != 0
                    && self.piece_at(61).is_none()
                    && self.piece_at(62).is_none()
                    && self.piece_at(63)
                        == Some(Piece {
                            color,
                            kind: PieceKind::Rook,
                        })
                    && !self.square_attacked(61, Color::White)
                    && !self.square_attacked(62, Color::White)
                {
                    let mut mv = Move::new(60, 62);
                    mv.is_castling = true;
                    moves.push(mv);
                }

                if self.castling_rights & CASTLE_BLACK_QUEENSIDE != 0
                    && self.piece_at(57).is_none()
                    && self.piece_at(58).is_none()
                    && self.piece_at(59).is_none()
                    && self.piece_at(56)
                        == Some(Piece {
                            color,
                            kind: PieceKind::Rook,
                        })
                    && !self.square_attacked(59, Color::White)
                    && !self.square_attacked(58, Color::White)
                {
                    let mut mv = Move::new(60, 58);
                    mv.is_castling = true;
                    moves.push(mv);
                }
            }
        }
    }

    fn king_square(&self, color: Color) -> Option<u8> {
        self.pieces().find_map(|(square, piece)| {
            if piece.color == color && piece.kind == PieceKind::King {
                Some(square)
            } else {
                None
            }
        })
    }

    pub fn square_attacked(&self, square: u8, by_color: Color) -> bool {
        let file = file_of(square);
        let rank = rank_of(square);

        let pawn_source_rank = if by_color == Color::White {
            rank - 1
        } else {
            rank + 1
        };
        for pawn_file in [file - 1, file + 1] {
            if let Some(source_square) = to_square(pawn_file, pawn_source_rank) {
                if self.piece_at(source_square)
                    == Some(Piece {
                        color: by_color,
                        kind: PieceKind::Pawn,
                    })
                {
                    return true;
                }
            }
        }

        let knight_offsets = [
            (1, 2),
            (2, 1),
            (2, -1),
            (1, -2),
            (-1, -2),
            (-2, -1),
            (-2, 1),
            (-1, 2),
        ];
        for (df, dr) in knight_offsets {
            if let Some(source_square) = to_square(file + df, rank + dr) {
                if self.piece_at(source_square)
                    == Some(Piece {
                        color: by_color,
                        kind: PieceKind::Knight,
                    })
                {
                    return true;
                }
            }
        }

        for (df, dr) in [
            (1, 1),
            (1, 0),
            (1, -1),
            (0, 1),
            (0, -1),
            (-1, 1),
            (-1, 0),
            (-1, -1),
        ] {
            if let Some(source_square) = to_square(file + df, rank + dr) {
                if self.piece_at(source_square)
                    == Some(Piece {
                        color: by_color,
                        kind: PieceKind::King,
                    })
                {
                    return true;
                }
            }
        }

        let bishop_like_directions = [(1, 1), (1, -1), (-1, 1), (-1, -1)];
        for (df, dr) in bishop_like_directions {
            let mut target_file = file + df;
            let mut target_rank = rank + dr;
            while let Some(source_square) = to_square(target_file, target_rank) {
                if let Some(piece) = self.piece_at(source_square) {
                    if piece.color == by_color
                        && (piece.kind == PieceKind::Bishop || piece.kind == PieceKind::Queen)
                    {
                        return true;
                    }
                    break;
                }
                target_file += df;
                target_rank += dr;
            }
        }

        let rook_like_directions = [(1, 0), (-1, 0), (0, 1), (0, -1)];
        for (df, dr) in rook_like_directions {
            let mut target_file = file + df;
            let mut target_rank = rank + dr;
            while let Some(source_square) = to_square(target_file, target_rank) {
                if let Some(piece) = self.piece_at(source_square) {
                    if piece.color == by_color
                        && (piece.kind == PieceKind::Rook || piece.kind == PieceKind::Queen)
                    {
                        return true;
                    }
                    break;
                }
                target_file += df;
                target_rank += dr;
            }
        }

        false
    }

    fn clear_castling_for_color(&mut self, color: Color) {
        match color {
            Color::White => {
                self.castling_rights &= !(CASTLE_WHITE_KINGSIDE | CASTLE_WHITE_QUEENSIDE);
            }
            Color::Black => {
                self.castling_rights &= !(CASTLE_BLACK_KINGSIDE | CASTLE_BLACK_QUEENSIDE);
            }
        }
    }

    fn clear_castling_right_by_rook_square(&mut self, rook_square: u8) {
        match rook_square {
            0 => self.castling_rights &= !CASTLE_WHITE_QUEENSIDE,
            7 => self.castling_rights &= !CASTLE_WHITE_KINGSIDE,
            56 => self.castling_rights &= !CASTLE_BLACK_QUEENSIDE,
            63 => self.castling_rights &= !CASTLE_BLACK_KINGSIDE,
            _ => {}
        }
    }

    fn apply_castling_rook_move(&mut self, mv: Move) {
        match (mv.from, mv.to) {
            (4, 6) => {
                let rook = self.piece_at(7);
                self.set_piece(7, None);
                self.set_piece(5, rook);
            }
            (4, 2) => {
                let rook = self.piece_at(0);
                self.set_piece(0, None);
                self.set_piece(3, rook);
            }
            (60, 62) => {
                let rook = self.piece_at(63);
                self.set_piece(63, None);
                self.set_piece(61, rook);
            }
            (60, 58) => {
                let rook = self.piece_at(56);
                self.set_piece(56, None);
                self.set_piece(59, rook);
            }
            _ => {}
        }
    }
}

pub fn piece_to_unicode(piece: Piece) -> &'static str {
    match (piece.color, piece.kind) {
        (Color::White, PieceKind::Pawn) => "?",
        (Color::White, PieceKind::Knight) => "?",
        (Color::White, PieceKind::Bishop) => "?",
        (Color::White, PieceKind::Rook) => "?",
        (Color::White, PieceKind::Queen) => "?",
        (Color::White, PieceKind::King) => "?",
        (Color::Black, PieceKind::Pawn) => "?",
        (Color::Black, PieceKind::Knight) => "?",
        (Color::Black, PieceKind::Bishop) => "?",
        (Color::Black, PieceKind::Rook) => "?",
        (Color::Black, PieceKind::Queen) => "?",
        (Color::Black, PieceKind::King) => "?",
    }
}

pub fn square_name(square: u8) -> String {
    let file = (b'a' + (square % 8)) as char;
    let rank = (b'1' + (square / 8)) as char;
    format!("{file}{rank}")
}

pub fn move_to_coord(mv: Move) -> String {
    let mut text = format!("{}{}", square_name(mv.from), square_name(mv.to));
    if let Some(promotion) = mv.promotion {
        text.push(match promotion {
            PieceKind::Queen => 'q',
            PieceKind::Rook => 'r',
            PieceKind::Bishop => 'b',
            PieceKind::Knight => 'n',
            PieceKind::Pawn | PieceKind::King => 'q',
        });
    }
    text
}

pub fn file_of(square: u8) -> i8 {
    (square % 8) as i8
}

pub fn rank_of(square: u8) -> i8 {
    (square / 8) as i8
}

pub fn to_square(file: i8, rank: i8) -> Option<u8> {
    if !(0..8).contains(&file) || !(0..8).contains(&rank) {
        return None;
    }
    Some((rank * 8 + file) as u8)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn play(position: &mut Position, uci: &str) {
        let legal = position.legal_moves();
        let mv = legal
            .iter()
            .copied()
            .find(|mv| move_to_coord(*mv) == uci)
            .unwrap_or_else(|| {
                let legal_list = legal
                    .iter()
                    .map(|mv| move_to_coord(*mv))
                    .collect::<Vec<_>>()
                    .join(", ");
                panic!("move {uci} not legal. legal: {legal_list}");
            });
        assert!(position.make_move(mv));
    }

    fn square(coord: &str) -> u8 {
        let bytes = coord.as_bytes();
        let file = bytes[0] - b'a';
        let rank = bytes[1] - b'1';
        (rank * 8 + file) as u8
    }

    #[test]
    fn start_position_has_20_legal_moves() {
        let position = Position::startpos();
        assert_eq!(position.legal_moves().len(), 20);
    }

    #[test]
    fn castling_rights_clear_after_king_move() {
        let mut position = Position::startpos();
        play(&mut position, "e2e4");
        play(&mut position, "a7a6");
        play(&mut position, "e1e2");

        assert_eq!(
            position.castling_rights & (CASTLE_WHITE_KINGSIDE | CASTLE_WHITE_QUEENSIDE),
            0
        );
    }

    #[test]
    fn en_passant_capture_works() {
        let mut position = Position::startpos();
        play(&mut position, "e2e4");
        play(&mut position, "a7a6");
        play(&mut position, "e4e5");
        play(&mut position, "d7d5");

        let ep_move = position
            .legal_moves()
            .into_iter()
            .find(|mv| move_to_coord(*mv) == "e5d6")
            .expect("missing en passant move e5d6");
        assert!(ep_move.is_en_passant);
        assert!(position.make_move(ep_move));

        assert_eq!(
            position.piece_at(square("d6")).map(|p| p.kind),
            Some(PieceKind::Pawn)
        );
        assert_eq!(position.piece_at(square("d5")), None);
    }

    #[test]
    fn fools_mate_is_checkmate() {
        let mut position = Position::startpos();
        play(&mut position, "f2f3");
        play(&mut position, "e7e5");
        play(&mut position, "g2g4");
        play(&mut position, "d8h4");

        assert_eq!(position.game_status(), GameStatus::Checkmate(Color::Black));
    }
}
