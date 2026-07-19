// Conway's Game of Life on a wrap-around grid — a glider crossing a torus.
const W = 12, H = 8;

function makeGrid() {
  const g = [];
  for (let y = 0; y < H; y++) {
    const row = [];
    for (let x = 0; x < W; x++) { row.push(0); }
    g.push(row);
  }
  return g;
}

function render(g) {
  let out = "";
  for (let y = 0; y < H; y++) {
    let line = "";
    for (let x = 0; x < W; x++) { line += g[y][x] ? "#" : "."; }
    out += line + "\n";
  }
  return out;
}

function neighbours(g, y, x) {
  let n = 0;
  for (const dy of [-1, 0, 1]) {
    for (const dx of [-1, 0, 1]) {
      if (dx === 0 && dy === 0) { continue; }
      const ny = (y + dy + H) % H;
      const nx = (x + dx + W) % W;
      n += g[ny][nx];
    }
  }
  return n;
}

function step(g) {
  const next = makeGrid();
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const n = neighbours(g, y, x);
      const alive = g[y][x] === 1;
      next[y][x] = (alive && (n === 2 || n === 3)) || (!alive && n === 3) ? 1 : 0;
    }
  }
  return next;
}

let grid = makeGrid();
// seed a glider in the top-left corner
grid[0][1] = 1;
grid[1][2] = 1;
grid[2][0] = 1;
grid[2][1] = 1;
grid[2][2] = 1;

for (let gen = 0; gen <= 4; gen++) {
  console.log("generation " + gen + ":");
  console.log(render(grid));
  grid = step(grid);
}
