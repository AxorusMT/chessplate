const boardEl = document.getElementById("hero-board");
const arrowsEl = document.getElementById("hero-arrows");
const canvas = document.getElementById("hero-prism");
const candidateList = document.getElementById("hero-candidates");
const themeRail = document.getElementById("theme-rail");

const PIECE_UNICODE = {
  wK: "\u2654",
  wQ: "\u2655",
  wR: "\u2656",
  wB: "\u2657",
  wN: "\u2658",
  wP: "\u2659",
  bK: "\u265A",
  bQ: "\u265B",
  bR: "\u265C",
  bB: "\u265D",
  bN: "\u265E",
  bP: "\u265F",
};

const HERO_POSITION = {
  a8: "bR",
  d8: "bQ",
  f8: "bB",
  g8: "bK",
  a7: "bP",
  b7: "bP",
  f7: "bP",
  g7: "bP",
  h7: "bP",
  c6: "bN",
  d6: "bP",
  e6: "bP",
  a2: "wP",
  b2: "wP",
  f2: "wP",
  g2: "wP",
  h2: "wP",
  a1: "wR",
  d1: "wQ",
  g1: "wK",
  c4: "wB",
  d4: "wP",
  e5: "wP",
  f3: "wN",
};

const HERO_ARROWS = [
  ["d4", "e5"],
  ["c4", "f7"],
  ["d1", "h5"],
];

const THEME_PRESETS = [
  {
    name: "Aurora Circuit",
    light: "#dce7f7",
    dark: "#557299",
    accent: "#1787ff",
    accent2: "#ff7a52",
    heroA: "#11253d",
    heroB: "#1f3e64",
    heroC: "#2f628f",
    glow: "#99ffe0",
  },
  {
    name: "Ember Sand",
    light: "#f2d9b7",
    dark: "#9f6b4a",
    accent: "#f28c22",
    accent2: "#d9534f",
    heroA: "#322018",
    heroB: "#6a3d2a",
    heroC: "#92694e",
    glow: "#ffd9a4",
  },
  {
    name: "Ocean Glass",
    light: "#d8eef2",
    dark: "#3d7d90",
    accent: "#1693b6",
    accent2: "#ff6f6f",
    heroA: "#102a36",
    heroB: "#1c4a5d",
    heroC: "#2b6f84",
    glow: "#9ff4ff",
  },
  {
    name: "Moss Cathedral",
    light: "#dde7d0",
    dark: "#628058",
    accent: "#5ea93f",
    accent2: "#d57f3e",
    heroA: "#1b291a",
    heroB: "#2b4a2a",
    heroC: "#4c6f43",
    glow: "#cbf8a8",
  },
  {
    name: "Neon Arcade",
    light: "#dfd2f9",
    dark: "#5d4ca5",
    accent: "#00cf89",
    accent2: "#ff5f96",
    heroA: "#1f1738",
    heroB: "#31265e",
    heroC: "#4f3c87",
    glow: "#adffec",
  },
  {
    name: "Cherry Blossom",
    light: "#f7dce8",
    dark: "#a05d7f",
    accent: "#e96a98",
    accent2: "#ffa462",
    heroA: "#361d2f",
    heroB: "#5a3048",
    heroC: "#874d6d",
    glow: "#ffd5e5",
  },
  {
    name: "Glacier Night",
    light: "#d7e3f6",
    dark: "#486191",
    accent: "#5fa7ff",
    accent2: "#ff7f66",
    heroA: "#13203b",
    heroB: "#253f6a",
    heroC: "#365d94",
    glow: "#add7ff",
  },
  {
    name: "Golden Reef",
    light: "#f5ddb0",
    dark: "#9f7a40",
    accent: "#e0a72a",
    accent2: "#db613c",
    heroA: "#35290f",
    heroB: "#66481b",
    heroC: "#917029",
    glow: "#ffe3a2",
  },
];

const CANDIDATE_MOVES = [
  "Nf3",
  "c4",
  "e4",
  "Bc4",
  "Qh5",
  "d4",
  "g3",
  "Re1",
  "O-O",
  "h4",
  "Bb5+",
  "Nc3",
  "exd5",
  "f4",
];

const searchState = {
  history: [],
  evalCp: 22,
  depth: 11,
  trend: 1,
  nodes: 420000,
  tick: 0,
};

function squareToIndex(square) {
  const file = square.charCodeAt(0) - 97;
  const rank = Number(square[1]);
  const row = 8 - rank;
  return row * 8 + file;
}

function buildHeroBoard() {
  const fragment = document.createDocumentFragment();

  for (let index = 0; index < 64; index += 1) {
    const square = document.createElement("div");
    const file = index % 8;
    const rank = Math.floor(index / 8);
    const isLight = (file + rank) % 2 === 0;
    square.className = `square ${isLight ? "light" : "dark"}`;

    const notation = `${String.fromCharCode(97 + file)}${8 - rank}`;
    const pieceCode = HERO_POSITION[notation];

    if (pieceCode) {
      const img = document.createElement("img");
      img.src = `../assets/pieces/${pieceCode}.svg`;
      img.alt = pieceCode;
      img.loading = "lazy";
      img.onerror = () => {
        img.replaceWith(document.createTextNode(PIECE_UNICODE[pieceCode] || ""));
      };
      square.appendChild(img);
    }

    fragment.appendChild(square);
  }

  boardEl.appendChild(fragment);
}

function drawArrow(fromSquare, toSquare, color) {
  const from = squareToIndex(fromSquare);
  const to = squareToIndex(toSquare);

  const fromFile = from % 8;
  const fromRank = Math.floor(from / 8);
  const toFile = to % 8;
  const toRank = Math.floor(to / 8);

  const fromX = fromFile * 100 + 50;
  const fromY = fromRank * 100 + 50;
  const toX = toFile * 100 + 50;
  const toY = toRank * 100 + 50;

  const dx = toX - fromX;
  const dy = toY - fromY;
  const length = Math.hypot(dx, dy);
  if (length < 1) return;

  const ux = dx / length;
  const uy = dy / length;

  const startX = fromX + ux * 16;
  const startY = fromY + uy * 16;
  const tipX = toX - ux * 16;
  const tipY = toY - uy * 16;

  const leftX = tipX - ux * 18 + -uy * 10;
  const leftY = tipY - uy * 18 + ux * 10;
  const rightX = tipX - ux * 18 - -uy * 10;
  const rightY = tipY - uy * 18 - ux * 10;

  const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
  line.setAttribute("x1", String(startX));
  line.setAttribute("y1", String(startY));
  line.setAttribute("x2", String(tipX - ux * 4));
  line.setAttribute("y2", String(tipY - uy * 4));
  line.setAttribute("stroke", color);
  line.setAttribute("stroke-width", "10");
  line.setAttribute("stroke-linecap", "round");
  line.setAttribute("opacity", "0.8");

  const head = document.createElementNS("http://www.w3.org/2000/svg", "polygon");
  head.setAttribute("points", `${tipX},${tipY} ${leftX},${leftY} ${rightX},${rightY}`);
  head.setAttribute("fill", color);
  head.setAttribute("opacity", "0.86");

  arrowsEl.appendChild(line);
  arrowsEl.appendChild(head);
}

function paintHeroArrows() {
  arrowsEl.innerHTML = "";
  const accent = getComputedStyle(document.documentElement)
    .getPropertyValue("--theme-accent")
    .trim();

  HERO_ARROWS.forEach((pair, idx) => {
    const alpha = idx === 0 ? "D8" : idx === 1 ? "A8" : "88";
    drawArrow(pair[0], pair[1], `${accent}${alpha}`);
  });
}

function sampleCandidates(baseScore) {
  const picks = new Set();
  while (picks.size < 3) {
    picks.add(CANDIDATE_MOVES[Math.floor(Math.random() * CANDIDATE_MOVES.length)]);
  }
  const moves = [...picks];

  const secondDrop = 12 + Math.floor(Math.random() * 25);
  const thirdDrop = secondDrop + 8 + Math.floor(Math.random() * 28);

  return [
    { move: moves[0], score: baseScore },
    { move: moves[1], score: baseScore - secondDrop },
    { move: moves[2], score: baseScore - thirdDrop },
  ];
}

function renderCandidates(candidates) {
  candidateList.innerHTML = "";
  const best = Math.max(...candidates.map((candidate) => Math.abs(candidate.score)), 1);

  candidates.forEach((candidate) => {
    const row = document.createElement("div");
    row.className = "candidate-item";

    const move = document.createElement("span");
    move.textContent = candidate.move;

    const meter = document.createElement("div");
    meter.className = "candidate-meter";
    const fill = document.createElement("span");
    const ratio = Math.max(0.12, Math.abs(candidate.score) / best);
    fill.style.width = `${Math.round(ratio * 100)}%`;
    meter.appendChild(fill);

    const score = document.createElement("span");
    score.textContent = `${candidate.score >= 0 ? "+" : ""}${candidate.score}cp`;

    row.appendChild(move);
    row.appendChild(meter);
    row.appendChild(score);
    candidateList.appendChild(row);
  });
}

function drawPrism() {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  const w = canvas.width;
  const h = canvas.height;

  const styles = getComputedStyle(document.documentElement);
  const accent = styles.getPropertyValue("--theme-accent").trim() || "#51f3d2";
  const gridColor = "rgba(177, 212, 247, 0.18)";

  ctx.clearRect(0, 0, w, h);

  ctx.fillStyle = "rgba(2, 12, 24, 0.9)";
  ctx.fillRect(0, 0, w, h);

  ctx.strokeStyle = gridColor;
  ctx.lineWidth = 1;
  for (let i = 1; i < 8; i += 1) {
    const x = (w * i) / 8;
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, h);
    ctx.stroke();
  }

  const zeroY = h / 2;
  ctx.beginPath();
  ctx.moveTo(0, zeroY);
  ctx.lineTo(w, zeroY);
  ctx.strokeStyle = "rgba(197, 220, 247, 0.28)";
  ctx.stroke();

  const history = searchState.history;
  if (history.length < 2) return;

  const maxDepth = Math.max(...history.map((entry) => entry.depth), 1);

  history.forEach((entry, idx) => {
    const x = (idx / (history.length - 1)) * w;
    const barHeight = (entry.depth / maxDepth) * (h * 0.36);
    ctx.fillStyle = "rgba(95, 163, 250, 0.42)";
    ctx.fillRect(x - 1.7, h - barHeight, 3.4, barHeight);
  });

  const points = history.map((entry, idx) => {
    const x = (idx / (history.length - 1)) * w;
    const cp = Math.max(-420, Math.min(420, entry.evalCp));
    const y = zeroY - (cp / 420) * (h * 0.42);
    return { x, y };
  });

  ctx.beginPath();
  points.forEach((point, idx) => {
    if (idx === 0) ctx.moveTo(point.x, point.y);
    else ctx.lineTo(point.x, point.y);
  });
  ctx.strokeStyle = "rgba(196, 235, 255, 0.3)";
  ctx.lineWidth = 7;
  ctx.stroke();

  const trendUp = history[history.length - 1].evalCp >= 0;
  ctx.beginPath();
  points.forEach((point, idx) => {
    if (idx === 0) ctx.moveTo(point.x, point.y);
    else ctx.lineTo(point.x, point.y);
  });
  ctx.strokeStyle = trendUp ? accent : "#ff8a6f";
  ctx.lineWidth = 2.3;
  ctx.stroke();

  const last = points[points.length - 1];
  ctx.beginPath();
  ctx.arc(last.x, last.y, 4.2, 0, Math.PI * 2);
  ctx.fillStyle = trendUp ? accent : "#ff8a6f";
  ctx.fill();
}

function stepSearch() {
  searchState.tick += 1;
  searchState.depth = Math.min(34, searchState.depth + (Math.random() > 0.35 ? 1 : 0));
  if (searchState.depth > 33 && Math.random() > 0.72) searchState.depth = 18;

  if (Math.random() > 0.82) searchState.trend *= -1;

  const wave = Math.sin(searchState.tick / 3.6) * 28;
  const drift = searchState.trend * (8 + Math.random() * 16);
  const noise = (Math.random() - 0.5) * 34;
  searchState.evalCp = Math.round((searchState.evalCp * 0.72) + wave + drift + noise);
  searchState.evalCp = Math.max(-260, Math.min(260, searchState.evalCp));

  searchState.nodes += Math.floor(30000 + Math.random() * 60000);
  searchState.history.push({
    depth: searchState.depth,
    evalCp: searchState.evalCp,
  });

  if (searchState.history.length > 90) {
    searchState.history.shift();
  }

  drawPrism();
  renderCandidates(sampleCandidates(searchState.evalCp));
}

function hexToRgba(hex, alpha) {
  const cleaned = hex.replace("#", "");
  const bigint = Number.parseInt(cleaned, 16);
  const r = (bigint >> 16) & 255;
  const g = (bigint >> 8) & 255;
  const b = bigint & 255;
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function applyTheme(theme) {
  const root = document.documentElement.style;
  root.setProperty("--theme-light", theme.light);
  root.setProperty("--theme-dark", theme.dark);
  root.setProperty("--theme-accent", theme.glow);
  root.setProperty("--accent", theme.accent);
  root.setProperty("--accent-2", theme.accent2);
  root.setProperty("--hero-a", theme.heroA);
  root.setProperty("--hero-b", theme.heroB);
  root.setProperty("--hero-c", theme.heroC);
  root.setProperty("--bg-a", hexToRgba(theme.light, 0.64));
  root.setProperty("--bg-b", hexToRgba(theme.dark, 0.18));

  paintHeroArrows();
  drawPrism();
}

function buildThemeRail() {
  const fragment = document.createDocumentFragment();

  THEME_PRESETS.forEach((theme, index) => {
    const button = document.createElement("button");
    button.className = `theme-chip ${index === 0 ? "is-active" : ""}`;
    button.type = "button";
    button.setAttribute("aria-label", `Use ${theme.name} preview theme`);

    const swatches = document.createElement("div");
    swatches.className = "theme-chip-swatches";
    [theme.light, theme.dark, theme.accent].forEach((color) => {
      const swatch = document.createElement("span");
      swatch.style.background = color;
      swatches.appendChild(swatch);
    });

    const name = document.createElement("p");
    name.textContent = theme.name;

    button.appendChild(swatches);
    button.appendChild(name);

    button.addEventListener("click", () => {
      document.querySelectorAll(".theme-chip").forEach((chip) => chip.classList.remove("is-active"));
      button.classList.add("is-active");
      applyTheme(theme);
    });

    fragment.appendChild(button);
  });

  themeRail.appendChild(fragment);
}

function setupRevealAnimation() {
  const sections = document.querySelectorAll(".reveal");
  const observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add("is-visible");
          observer.unobserve(entry.target);
        }
      });
    },
    { threshold: 0.18 }
  );

  sections.forEach((section) => observer.observe(section));
}

function initSearchWarmup() {
  for (let i = 0; i < 30; i += 1) {
    stepSearch();
  }
  setInterval(stepSearch, 900);
}

function init() {
  buildHeroBoard();
  buildThemeRail();
  paintHeroArrows();
  initSearchWarmup();
  setupRevealAnimation();
}

init();
