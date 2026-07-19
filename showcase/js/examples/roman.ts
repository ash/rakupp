// Roman numerals in both directions, driven by a tuple table.
type Pair = [number, string];

const TABLE: Pair[] = [
  [1000, "M"], [900, "CM"], [500, "D"], [400, "CD"],
  [100, "C"], [90, "XC"], [50, "L"], [40, "XL"],
  [10, "X"], [9, "IX"], [5, "V"], [4, "IV"], [1, "I"],
];

function toRoman(n: number): string {
  let out = "";
  for (const [value, symbol] of TABLE) {
    while (n >= value) {
      out += symbol;
      n -= value;
    }
  }
  return out;
}

function fromRoman(s: string): number {
  const values: Record<string, number> = {
    I: 1, V: 5, X: 10, L: 50, C: 100, D: 500, M: 1000,
  };
  let total = 0;
  for (let i = 0; i < s.length; i++) {
    const cur = values[s[i]];
    const nxt = i + 1 < s.length ? values[s[i + 1]] : 0;
    total += cur < nxt ? -cur : cur;
  }
  return total;
}

for (const n of [4, 9, 14, 40, 90, 444, 1994, 2024]) {
  const r = toRoman(n);
  const back = fromRoman(r);
  const ok = back === n ? "ok" : "MISMATCH";
  console.log(`${n} -> ${r} -> ${back} (${ok})`);
}
