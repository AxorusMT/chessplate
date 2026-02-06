# Chessplate

Chessplate is a native Rust chess application with a built-in engine, live search telemetry, a rich board UI, and a companion static website.

It is designed for practical local play and analysis:
- Play against a configurable-strength bot.
- Inspect what the search is doing while the engine thinks.
- Use board arrows and premoves during active games.
- Export positions and games quickly (FEN and PGN).
- Customize visuals through a large generated theme catalog.

---

## Table of Contents
- [Highlights](#highlights)
- [Repository Layout](#repository-layout)
- [Getting Started](#getting-started)
- [Desktop App Usage](#desktop-app-usage)
- [How To Play Chess (Quick Guide)](#how-to-play-chess-quick-guide)
- [Engine Documentation](#engine-documentation)
- [Search Prism Metrics](#search-prism-metrics)
- [Notation and Export Behavior](#notation-and-export-behavior)
- [Website](#website)
- [Build, Test, and Release](#build-test-and-release)
- [Troubleshooting](#troubleshooting)
- [Known Limitations](#known-limitations)
- [Contributing](#contributing)
- [License](#license)

---

## Highlights

### Desktop app
- Native desktop GUI via `eframe`/`egui`.
- Local engine (no network dependency).
- Elo slider range: `600` to `2800`.
- Live engine panel ("Search Prism") rendered to the right of the board.
- Opening/middlegame/endgame naming in sidebar.
- Right-drag board arrows (chess.com style).
- Arrow clear behavior: `Esc` or any primary-click event.
- Premove support (single queued premove with auto-validation).
- Copy tools:
  - `Copy State` -> FEN of current position.
  - `Copy Game` -> PGN with SAN move text.
- Large generated board theme catalog (90 themes).
- App window icon loaded from project SVG logo.

### Website (`site/`)
- Pure HTML/CSS/JS marketing/documentation site.
- Interactive hero board preview and animated telemetry mock.
- Theme-swapping visual preview.
- Documentation sections for engine internals and chess basics.

---

## Repository Layout

```text
.
|-- assets/
|   |-- logo/
|   |   `-- chessplate-logo.svg
|   `-- pieces/
|       |-- wK.svg ... wP.svg
|       `-- bK.svg ... bP.svg
|-- site/
|   |-- index.html
|   |-- styles.css
|   |-- script.js
|   `-- logo.svg
|-- src/
|   |-- app.rs
|   |-- chess.rs
|   |-- engine.rs
|   |-- icon.rs
|   |-- main.rs
|   `-- app/
|       |-- game_insight.rs
|       |-- metrics.rs
|       |-- pieces.rs
|       `-- theme.rs
|-- Cargo.toml
`-- Cargo.lock
```

---

## Getting Started

### Prerequisites
- Rust toolchain (stable)
- Cargo (bundled with Rust)

Project currently targets:
- Edition: `2024`
- Dependencies:
  - `eframe = "0.30"`
  - `rand = "0.8.5"`
  - `resvg = "0.45.1"`

### Run the desktop app

```bash
cargo run
```

### Run optimized build

```bash
cargo run --release
```

### Run the website locally

From repository root:

```bash
cd site
python -m http.server 8000
```

Then open:
- `http://localhost:8000`

---

## Desktop App Usage

### Main controls
- `New Game`: reset to standard starting position.
- `Play as White` / `Play as Black`: sets human side and restarts.
- `Bot Elo` slider: adjusts search depth/time/node limits and noise model.
- `Color Theme`: choose from generated theme catalog.
- `Copy State`: copies current FEN.
- `Copy Game`: copies PGN (SAN moves + result tag).

### Move input
- Click a piece to select legal targets.
- Click a highlighted destination to play move.
- Promotion auto-selects queen by default if multiple promotions exist.

### Analysis arrows
- Right mouse drag from source square to target square to add/remove an arrow.
- Arrow clearing:
  - Press `Esc`, or
  - Trigger a primary-click event (left-click).

### Premove behavior
Premove is enabled when it is not your turn.

How it works:
1. During engine turn, select your piece and destination.
2. A single premove is queued and displayed in sidebar.
3. After engine move resolves, queued premove is checked against new legal moves.
4. If legal, it is played automatically.
5. If illegal, it is dropped silently (board changed in a way that invalidated it).

Notes:
- Only one premove is queued at a time.
- Queueing the same premove again toggles it off.
- Sidebar provides quick clear action for queued premove.

### Sidebar insight panel
- Game status text (`to move`, check, checkmate, draw).
- Game phase labels:
  - Current phase
  - Opening name
  - Middlegame theme
  - Endgame theme
- Move list in coordinate notation.
- Search profile summary derived from Elo.

### Board orientation
- Board orientation follows chosen player color:
  - Human White: White perspective.
  - Human Black: Black perspective.

---

## How To Play Chess (Quick Guide)

### Objective
Checkmate the opponent king:
- The king is in check.
- No legal move can remove that check.

### Piece movement
- King: one square any direction.
- Queen: any number of squares, straight or diagonal.
- Rook: any number of squares orthogonally.
- Bishop: any number of squares diagonally.
- Knight: L-shape (2 then 1), can jump over pieces.
- Pawn:
  - Moves forward one square.
  - From start square may move forward two.
  - Captures diagonally forward one.
  - Promotes on last rank.

### Special rules
- Castling:
  - King and rook move together.
  - Requires unchanged castling rights, clear path, and no attacked crossing squares.
- En passant:
  - Pawn can capture a pawn that just moved two squares as if it moved one.
  - Available only immediately on next move.
- Promotion:
  - Pawn reaching back rank promotes to queen, rook, bishop, or knight.

### Draw conditions implemented
- Stalemate.
- Fifty-move rule.

### Fast practical principles
- Develop pieces early.
- Control center squares.
- Castle to protect your king.
- Before every move, check:
  - Opponent threats
  - Your checks/captures/threats
  - Tactical blunders (hanging pieces)

---

## Engine Documentation

This engine is a custom local engine, not Stockfish-derived code.

### Core model
- Board representation: fixed `[Option<Piece>; 64]` array.
- Move representation: compact `Move` struct (`from`, `to`, promotion, EP, castling flags).
- Legal move generation:
  - Generate pseudo-legal moves by piece type.
  - Apply move on cloned position.
  - Keep move only if side-to-move king is not left in check.

### Game-state tracking
Position state tracks:
- Side to move
- Castling rights
- En passant target square
- Halfmove clock
- Fullmove number

Game status includes:
- Ongoing
- Checkmate
- Stalemate
- Draw by fifty-move rule

### Search algorithm
Main pipeline:
1. Generate legal root moves.
2. Iterative deepening from depth `1..=max_depth`.
3. Negamax with alpha-beta pruning.
4. Quiescence search at leaf depth to reduce tactical horizon noise.
5. Move ordering at each node.
6. Stop when any limit is hit:
   - Depth limit
   - Time budget
   - Node budget

### Move ordering heuristic
Moves are scored to improve pruning:
- Promotions receive a large bonus.
- Captures receive bonus with attacker/captured piece weighting.
- Castling gets an additional bonus.
- Root move list is reordered each completed depth by resulting score.

### Evaluation function
Static evaluation combines:
- Material values:
  - Pawn `100`
  - Knight `320`
  - Bishop `330`
  - Rook `500`
  - Queen `900`
  - King `20000` (search utility scale)
- Piece-square tables (per piece type, mirrored by color).
- Bishop pair bonus.
- Small initiative bonus for side to move.

### Elo simulation profile
Elo does not connect to an external rating pool; it maps to search limits and noise injection.

| Elo Band | Depth | Time (ms) | Node Limit | Randomness (cp) |
|---|---:|---:|---:|---:|
| 600-899 | 2 | 150 | 35,000 | `(2800 - elo) / 8` |
| 900-1199 | 3 | 250 | 70,000 | `(2800 - elo) / 8` |
| 1200-1499 | 4 | 400 | 150,000 | `(2800 - elo) / 8` |
| 1500-1799 | 5 | 600 | 300,000 | `(2800 - elo) / 8` |
| 1800-2099 | 6 | 900 | 700,000 | `(2800 - elo) / 8` |
| 2100-2399 | 7 | 1300 | 1,300,000 | `(2800 - elo) / 8` |
| 2400-2800 | 8 | 1800 | 2,200,000 | `(2800 - elo) / 8` |

Randomness behavior:
- At lower Elo, top candidate moves are perturbed by bounded random centipawn noise.
- At high Elo, noise approaches zero and best move becomes deterministic more often.

### Telemetry snapshots
Search exposes snapshots used by UI:
- current depth
- elapsed milliseconds
- nodes searched
- best move and score
- top candidate moves with scores

---

## Search Prism Metrics

The right-side visualization is intended to provide actionable feedback, not decoration only.

Displayed metrics:
- **Depth**: current completed iterative depth.
- **Nodes**: total explored nodes.
- **NPS**: nodes per second (`nodes * 1000 / elapsed_ms`).
- **Score**: best line evaluation in pawns (`cp / 100`).
- **Stability**: consistency of best move over recent snapshots.
- **Volatility**: average absolute cp swing over recent snapshots.
- **Lead gap**: cp difference between best and second-best candidates.

Visual channels:
- Bars for depth progression.
- Trend line for eval movement.
- Candidate cards for top moves with cp and relative bars.

---

## Notation and Export Behavior

### In-app move list
- Sidebar move list displays coordinate notation (UCI-like strings such as `e2e4`).

### Copy State
- Copies current board state as FEN via `Position::to_fen()`.

### Copy Game
- Replays internal move history and exports PGN move text in SAN format.
- Includes PGN headers:
  - Event: `Chessplate`
  - Site: `Local`
  - Result tag

---

## Website

Location:
- `site/index.html`
- `site/styles.css`
- `site/script.js`

Properties:
- Pure static stack (no framework).
- Uses local `site/logo.svg` and piece SVGs from `assets/pieces` for hero preview.
- Includes dedicated sections:
  - Engine documentation
  - Chess learning guide
  - Feature overview
  - Theme preview rail

---

## Build, Test, and Release

### Format

```bash
cargo fmt
```

### Type-check and compile

```bash
cargo check
```

### Run tests

```bash
cargo test
```

### Build release binary

```bash
cargo build --release
```

Binary output:
- `target/release/chessplate.exe` (Windows)

---

## Troubleshooting

### Piece images not visible
- Confirm SVG files exist under `assets/pieces/`.
- App falls back to Unicode chess glyphs if texture load fails.

### Arrows disappear unexpectedly
- Expected behavior: any primary-click event or `Esc` clears arrows.

### Premove did not play
- Most likely the opponent move made it illegal.
- Premove is validated only after engine move and dropped if invalid.

### App seems slow at high Elo
- Higher Elo uses larger time/node budgets by design.
- Reduce Elo if you prefer faster response.

### Website preview has broken piece paths
- Serve from repository with static server (`cd site && python -m http.server`).
- Opening `site/index.html` directly as a file can produce path issues in some environments.

---

## Known Limitations

- Promotion UI is not interactive yet (queen is auto-preferred).
- Draw detection currently includes:
  - stalemate
  - fifty-move rule
  - (no explicit threefold repetition or insufficient-material adjudication yet)
- Engine strength is practical and configurable, but not a Stockfish-level search stack.
- Single premove queue only.

---

## Contributing

If you want to extend Chessplate:
- Keep engine and UI changes separated when possible.
- Run `cargo fmt`, `cargo check`, `cargo test` before opening PRs.
- Update docs (`README.md` and `site/`) when behavior changes.

Useful extension targets:
- Interactive promotion chooser.
- Additional draw-rule completeness.
- Opening book expansion and richer phase classifiers.
- Stronger search features (transposition table, null move pruning, etc.).

---

## License

No license file is currently present in the repository.
If you intend to distribute or accept outside contributions, add a top-level license file (for example, MIT, Apache-2.0, or dual-license).
