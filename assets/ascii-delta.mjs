#!/usr/bin/env node
/**
 * ascii-delta — Δ em ASCII (3 linhas vertical, ate 6 colunas horizontal)
 *
 * Estilos:
 *   line   (padrao) traco de barras  / \ _        -> ASCII puro
 *   solid            preenchido com #              -> ASCII puro
 *   block            meios blocos ▀▄█              -> a logo do Delta Agent na TUI
 *
 * uso:
 *   node assets/ascii-delta.mjs                  # catalogo completo (line)
 *   node assets/ascii-delta.mjs --style=block
 *   node assets/ascii-delta.mjs --style=solid --cols=6
 *   node assets/ascii-delta.mjs --gap=1 --only=glyphs
 */

const W = 6; // largura maxima do glifo (colunas de texto)

const ASCII = {
  line: { DELTA: ["  /\\  ", " /  \\ ", "/____\\"] },
};

// pixel art 8x6 do Δ (fonte unica; a mesma usada na logo da TUI).
// A primeira linha e' o respiro que o meio bloco exige: o Δ tem 5 linhas de
// pixel, com 2 px de traco nos lados e base cheia.
const PIXELS = [
  "........",
  "   ##   ",
  "  ####  ",
  " ##  ## ",
  "##    ##",
  "########",
];

const pad = (s) => s.padEnd(W);
const trim = (s) => s.replace(/\s+$/, "");

function halfBlock(px) {
  const cell = (t, b) => (t && b ? "█" : t ? "▀" : b ? "▄" : " ");
  const rows = [];
  for (let r = 0; r < px.length; r += 2) {
    let line = "";
    for (let c = 0; c < px[r].length; c++) line += cell(px[r][c] === "#", px[r + 1][c] === "#");
    rows.push(line);
  }
  return rows;
}

const STYLES = {
  line: ASCII.line,
  solid: { DELTA: PIXELS.slice(1) },     // o Δ em # puro (5 linhas)
  block: { DELTA: halfBlock(PIXELS) },   // ▀ ▄ █ (3 linhas)
};

const argv = Object.fromEntries(
  process.argv.slice(2).map((a) => {
    const [k, v = "true"] = a.replace(/^--/, "").split("=");
    return [k, v];
  })
);

const STYLE = STYLES[argv.style] ?? STYLES.line;
const H = STYLE.DELTA.length;   // 3 nas barras / meio bloco, 5 no #
const GAP = " ".repeat(Number(argv.gap ?? 2));

function grid(rows, cols) {
  const out = [];
  for (let r = 0; r < rows; r++) {
    for (let line = 0; line < H; line++) {
      const cells = [];
      for (let c = 0; c < cols; c++) cells.push(pad(STYLE.DELTA[line]));
      out.push(trim(cells.join(GAP)));
    }
  }
  return out;
}

const head = (t) => `== ${t} ==`;

console.log(head("Δ maiusculo"));
console.log(STYLE.DELTA.map(trim).join("\n"));

if (argv.only !== "glyphs") {
  console.log();
  const cols = Number(argv.cols ?? 6);
  console.log(head(`3 vertical x ${cols} horizontal — Δ`));
  console.log(grid(3, cols).join("\n"));
  console.log();
  for (let c = 1; c <= W; c++) {
    console.log(head(`3 vertical x ${c} horizontal — Δ`));
    console.log(grid(3, c).join("\n"));
    console.log();
  }
}
