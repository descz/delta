#!/usr/bin/env node
/**
 * ascii-lambda — Λ / λ em ASCII (3 linhas vertical, ate 6 colunas horizontal)
 *
 * Estilos:
 *   line   (padrao) traco de barras  / \ _        -> ASCII puro
 *   solid            preenchido com #              -> ASCII puro
 *   block            meios blocos ▀▄█              -> mesmo desenho da logo do Zeno
 *
 * uso:
 *   node assets/ascii-lambda.mjs                  # catalogo completo (line)
 *   node assets/ascii-lambda.mjs --style=solid
 *   node assets/ascii-lambda.mjs --style=block --cols=6
 *   node assets/ascii-lambda.mjs --gap=1 --mode=alt
 *   node assets/ascii-lambda.mjs --only=glyphs     # so os dois glifos
 */

const H = 3; // altura do glifo (linhas de texto)
const W = 6; // largura maxima do glifo (colunas de texto)

/* ------------------------------------------------------------------ estilos */

const ASCII = {
  // traco fino: / \ _  —  lambda minusculo tem o pe' virando para a direita
  line: {
    UP: ["  /\\  ", " /  \\ ", "/    \\"],
    LO: ["  /\\  ", " /  \\ ", "/   /\\"],
  },
  // cheio: cada traco vira uma faixa de #
  solid: {
    UP: ["  ##  ", " #### ", "######"],
    LO: ["  ##  ", " ## # ", "##  ##"],
  },
};

// pixel art 8x6 (a mesma usada na logo em meios blocos)
const PIXELS = {
  UP: [
    "...##...",
    "..####..",
    ".##..##.",
    ".##..##.",
    "##....##",
    "##....##",
  ],
  LO: [
    "...##...",
    "..####..",
    ".##..##.",
    ".##...##",
    "##....##",
    "##...###",
  ],
};

/* ---------------------------------------------------------------- renderers */

const pad = (s) => s.padEnd(W);
const trim = (s) => s.replace(/\s+$/, "");

/** 6 linhas de pixel -> 3 linhas de meios blocos */
function halfBlock(px) {
  const cell = (top, bot) =>
    top && bot ? "█" : top ? "▀" : bot ? "▄" : " ";
  const rows = [];
  for (let r = 0; r < px.length; r += 2) {
    let line = "";
    for (let c = 0; c < px[r].length; c++) {
      line += cell(px[r][c] === "#", px[r + 1][c] === "#");
    }
    rows.push(line);
  }
  return rows;
}

/* ------------------------------------------------------------------- catalogo */

const STYLES = {
  line: ASCII.line,
  solid: ASCII.solid,
  block: { UP: halfBlock(PIXELS.UP), LO: halfBlock(PIXELS.LO) },
};

const argv = Object.fromEntries(
  process.argv.slice(2).map((a) => {
    const [k, v = "true"] = a.replace(/^--/, "").split("=");
    return [k, v];
  })
);

const STYLE = STYLES[argv.style] ?? STYLES.line;
const GAP = " ".repeat(Number(argv.gap ?? 2));
const MODE = argv.mode ?? "all";

const pick = {
  up: () => STYLE.UP,
  lo: () => STYLE.LO,
  alt: (r, c) => ((r + c) % 2 ? STYLE.LO : STYLE.UP),
}[MODE] ?? ((r, c) => ((r + c) % 2 ? STYLE.LO : STYLE.UP));

function grid(rows, cols, f) {
  const out = [];
  for (let r = 0; r < rows; r++) {
    for (let line = 0; line < H; line++) {
      const cells = [];
      for (let c = 0; c < cols; c++) cells.push(pad(f(r, c)[line]));
      out.push(trim(cells.join(GAP)));
    }
  }
  return out;
}

const head = (t) => `== ${t} ==`;

console.log(head("Λ maiusculo"));
console.log(STYLE.UP.map(trim).join("\n"));
console.log();
console.log(head("λ minusculo"));
console.log(STYLE.LO.map(trim).join("\n"));

if (argv.only !== "glyphs") {
  console.log();
  for (const [name, f] of [
    ["Λ", () => STYLE.UP],
    ["λ", () => STYLE.LO],
    ["Λ/λ", (r, c) => ((r + c) % 2 ? STYLE.LO : STYLE.UP)],
  ]) {
    console.log(head(`3 vertical x ${argv.cols ?? 6} horizontal — ${name}`));
    console.log(grid(3, Number(argv.cols ?? 6), f).join("\n"));
    console.log();
  }
  for (let c = 1; c <= W; c++) {
    console.log(head(`3 vertical x ${c} horizontal — Λ`));
    console.log(grid(3, c, () => STYLE.UP).join("\n"));
    console.log();
  }
}
