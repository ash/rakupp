// sidebar.mjs — the round trip of the Google Sheets sidebar mode, with the
// sidebar played by the engine of the attached-script build: formulas in a
// stand-in spreadsheet wait, the sidebar's calls (rakuPending, rakuStore)
// run against a stand-in cache, and the formulas, entered again, find their
// results. The sidebar page itself is test/sidebar-harness.html.
//
//   node bindings/spreadsheets/test/sidebar.mjs [dist]

import vm from 'node:vm';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const dist = process.argv[2] ?? path.join(here, '..', 'dist');

let failed = 0, passed = 0;
function check(name, got, want) {
  const ok = want instanceof RegExp ? typeof got === 'string' && want.test(got) : JSON.stringify(got) === JSON.stringify(want);
  if (ok) { passed++; return; }
  failed++;
  console.log(`not ok - ${name}\n    got:  ${JSON.stringify(got)}\n    want: ${want instanceof RegExp ? want : JSON.stringify(want)}`);
}

// ---- the engine, as the sidebar holds it -----------------------------------------------
const engineCtx = vm.createContext({ console: { log() {}, warn() {}, error() {}, info() {} } });
const sheetsDir = path.join(dist, 'google-sheets');
for (const f of fs.readdirSync(sheetsDir).filter(f => f.endsWith('.gs') && f !== 'Raku.gs')) {
  vm.runInContext(fs.readFileSync(path.join(sheetsDir, f), 'utf8'), engineCtx, { filename: f });
}
const engine = await engineCtx.rakuSheetEngine();
const RakuSheet = vm.runInContext('RakuSheet', engineCtx);

// What the sidebar does with what rakuPending hands it.
function sidebar(p) {
  const result = engine.run(p.definitions, p.requests.map(r => ({ code: r.code, args: r.args })));
  return p.requests.map((r, i) => {
    const a = result.answers[i];
    if ('err' in a) return { key: r.key, err: a.err };
    try { return { key: r.key, cells: RakuSheet.cells(a.ok) }; } catch (e) { return { key: r.key, err: e.message }; }
  });
}

// ---- a spreadsheet, a cache and Utilities ---------------------------------------------
function world() {
  const cache = new Map();
  const sheets = new Map();
  let ctx;
  const evaluate = cell => {
    if (!cell.formula) { cell.shown = ''; return; }
    try {
      const v = ctx.RAKU(cell.code, ...cell.args);
      cell.value = v;
      cell.shown = Array.isArray(v) ? String(v[0][0]) : String(v);
    } catch (e) {
      cell.value = { error: e.message };
      cell.shown = '#ERROR!';
    }
  };
  const sheetOf = (name, rows) => {
    const sheet = {
      rows,
      getName: () => name,
      getDataRange: () => ({
        getDisplayValues: () => rows.map(r => r.map(c => c.shown ?? String(c.text ?? ''))),
        getFormulas: () => rows.map(r => r.map(c => c.formula ?? '')),
      }),
      getRange: (row, col, n) => typeof row === 'string' ? { setFontFamily() {} } : n === undefined
        ? {
            setFormula: f => {
              const cell = rows[row - 1][col - 1];
              cell.formula = f;
              cell.shown = '';
              if (f) evaluate(cell);
            },
          }
        : { getDisplayValues: () => rows.slice(row - 1, row - 1 + n).map(r => [String(r[0].text ?? '')]),
            setNumberFormat() {}, setValues(v) { v.forEach((l, i) => { rows[i] = [{ text: l[0] }]; }); } },
      getLastRow: () => rows.length,
      setColumnWidth() {},
    };
    return sheet;
  };
  const app = {
    getActiveSpreadsheet: () => ({
      getSheets: () => [...sheets.values()],
      getSheetByName: name => sheets.get(name) ?? null,
      insertSheet: name => { const s = sheetOf(name, []); sheets.set(name, s); return s; },
      toast() {},
    }),
    flush() {},
  };
  const Utilities = {
    DigestAlgorithm: { SHA_256: 'sha256' }, Charset: { UTF_8: 'utf8' },
    computeDigest: (alg, text) => Array.from(crypto.createHash(alg).update(text, 'utf8').digest(), b => (b << 24) >> 24),
  };
  const CacheService = {
    getDocumentCache: () => ({
      get: k => (cache.has(k) ? cache.get(k) : null),
      put: (k, v) => { cache.set(k, v); },
      getAll: ks => Object.fromEntries(ks.filter(k => cache.has(k)).map(k => [k, cache.get(k)])),
      putAll: o => { for (const [k, v] of Object.entries(o)) cache.set(k, v); },
    }),
  };
  ctx = vm.createContext({ console: { log() {} }, SpreadsheetApp: app, Utilities, CacheService });
  vm.runInContext(fs.readFileSync(path.join(dist, 'google-sheets-sidebar', 'Raku.gs'), 'utf8'), ctx, { filename: 'Raku.gs' });
  // A cell's formula, with the values Sheets would hand the custom function.
  const raku = (code, ...args) => ({ formula: `=RAKU(${JSON.stringify(code)}${args.map(() => ', X').join('')})`, code, args });
  const addSheet = (name, rows) => { const s = sheetOf(name, rows); sheets.set(name, s); rows.flat().forEach(evaluate); return s; };
  return { ctx, cache, sheets, raku, addSheet };
}

// ---- the round trip -----------------------------------------------------------------
const w = world();
const data = w.addSheet('Data', [
  [w.raku('6 * 7'), w.raku('($^a - $^b) + 1', 43.1, 43.2)],
  [w.raku('[+] @_', [[0.1], [0.1], [0.1], [0.1], [0.1], [0.1], [0.1], [0.1], [0.1], [0.1]]), w.raku('(1..3).map(* ** 2)')],
  [w.raku('1/0'), w.raku('6 * 7')],
  [{ text: 'not a formula' }, w.raku('$^d.substr(0, 10)', new Date(Date.UTC(2026, 9, 8)))],
]);
const cell = (r, c) => data.rows[r][c];
check('a new formula waits for the sidebar', /^⏳ Raku sidebar [0-9a-f]{16}$/.test(cell(0, 0).shown), true);
check('the same formula has the same key', cell(2, 1).shown, cell(0, 0).shown);

let p = w.ctx.rakuPending();
check('rakuPending: one request a key', p.requests.length, 6);
check('rakuPending: the request carries the code and values', p.requests.find(r => r.code === '($^a - $^b) + 1').args, [43.1, 43.2]);
check('a date reaches the sidebar as ISO 8601 text', p.requests.find(r => r.code.startsWith('$^d')).args, ['2026-10-08T00:00:00.000Z']);

const entered = w.ctx.rakuStore(sidebar(p));
check('rakuStore enters the waiting formulas again', entered, 7);
check('6 * 7', cell(0, 0).value, 42);
check('the same formula elsewhere', cell(2, 1).value, 42);
check('the 43.1 example', cell(0, 1).value, 0.9);
check('a column range', cell(1, 0).value, 1);
check('a list fills a column', cell(1, 1).value, [[1], [4], [9]]);
check('an error', cell(2, 0).value, { error: '#DIV/0: the result divides by zero' });
check('a date', cell(3, 1).value, '2026-10-08');
check('nothing waits now', w.ctx.rakuPending().requests.length, 0);

// ---- the Raku sheet: its own key, so the sidebar sees it change ------------------------
const before = w.ctx.rakuPending().definitionsKey;
check('rakuAddSheet', w.ctx.rakuAddSheet(['sub double($x) { $x * 2 }']), 'The Raku sheet is added.');
check('rakuAddSheet, twice', w.ctx.rakuAddSheet(['x']), 'There is a Raku sheet already.');
check('the Raku sheet changes the definitions key', w.ctx.rakuPending().definitionsKey !== before, true);
check('rakuRecalculate enters every RAKU formula again', w.ctx.rakuRecalculate(), 7);
check('after it, they wait again: the key holds the Raku sheet', /^⏳/.test(cell(0, 0).shown), true);
const uses = w.addSheet('Uses', [[w.raku('double($^a)', 21)]]);
p = w.ctx.rakuPending();
check('the definitions travel with the requests', p.definitions, 'sub double($x) { $x * 2 }');
w.ctx.rakuStore(sidebar(p));
check('a sub from the Raku sheet', uses.rows[0][0].value, 42);
check('and the old formulas again', cell(0, 0).value, 42);

// ---- a request the cache lost is asked for again ---------------------------------------
w.addSheet('Lost', [[w.raku('1 + 1')]]);
for (const k of [...w.cache.keys()]) if (k.startsWith('raku:req:')) w.cache.delete(k);
p = w.ctx.rakuPending();
check('a lost request: none this time', p.requests.length, 0);
check('a lost request: its formula is entered again, and asks again', w.ctx.rakuPending().requests.map(r => r.code), ['1 + 1']);

// ---- what the cache cannot hold ---------------------------------------------------------
const big = 'x'.repeat(100000);
const t = w.addSheet('Big', [[w.raku('$^s.chars', big)]]);
check('values too large for the cache', t.rows[0][0].value, { error: 'the values are too large to hand to the Raku sidebar' });

console.log(`${failed ? 'FAIL' : 'PASS'}: ${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
