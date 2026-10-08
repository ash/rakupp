// sheets.mjs — runs the generated Google Sheets project the way Apps Script
// does: every .gs file in one bare V8 context, with none of the browser's or
// Node's globals (no TextDecoder, performance, crypto, setTimeout, process),
// in an order of its own. SpreadsheetApp is a stand-in holding a Raku sheet.
//
//   node bindings/spreadsheets/test/sheets.mjs [dist/google-sheets]

import vm from 'node:vm';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const dir = process.argv[2] ?? path.join(here, '..', 'dist', 'google-sheets');
const files = fs.readdirSync(dir).filter(f => f.endsWith('.gs'));

const RAKU_SHEET = [
  '# the Raku sheet of this test',
  'sub double($x) { $x * 2 }',
  'sub iban-ok(Str $iban) {',
  '    my $t = $iban.uc.comb(/<alnum>/).join;',
  '    my $digits = ($t.substr(4) ~ $t.substr(0, 4)).comb.map({ /\\d/ ?? $_ !! .ord - 55 }).join;',
  '    $digits % 97 == 1',
  '}',
];

function spreadsheet(lines) {
  const sheet = lines && {
    getLastRow: () => lines.length,
    getRange: (row, col, rows) => ({
      getDisplayValues: () => lines.slice(row - 1, row - 1 + rows).map(l => [l]),
    }),
  };
  return { getActiveSpreadsheet: () => ({ getSheetByName: name => (name === 'Raku' ? sheet : null) }) };
}

function shuffle(a) {
  const b = a.slice();
  for (let i = b.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [b[i], b[j]] = [b[j], b[i]];
  }
  return b;
}

// A fresh project, as a cold Apps Script execution sees it.
function project({ lines = RAKU_SHEET, edit } = {}) {
  const logs = [];
  const ctx = vm.createContext({
    console: { log: (...a) => logs.push(a.join(' ')), warn() {}, error() {}, info() {} },
    SpreadsheetApp: spreadsheet(lines),
  });
  for (const f of ['TextDecoder', 'performance', 'crypto', 'setTimeout', 'process', 'window']) {
    if (vm.runInContext(`typeof ${f}`, ctx) !== 'undefined') throw new Error(`the sandbox has ${f}`);
  }
  for (const f of shuffle(files)) {
    let src = fs.readFileSync(path.join(dir, f), 'utf8');
    if (edit) src = edit(f, src);
    if (src !== null) vm.runInContext(src, ctx, { filename: f });
  }
  return { ctx, logs };
}

// A RegExp in `want` matches a string; an engine's own wording may change
// between releases where the test only cares about its start.
function matches(got, want) {
  if (want instanceof RegExp) return typeof got === 'string' && want.test(got);
  if (want && typeof want === 'object' && !Array.isArray(want)) {
    return got && typeof got === 'object' && Object.keys(want).length === Object.keys(got).length
      && Object.keys(want).every(k => matches(got[k], want[k]));
  }
  return JSON.stringify(got) === JSON.stringify(want);
}

let failed = 0, passed = 0;
function check(name, got, want) {
  if (matches(got, want)) { passed++; return; }
  failed++;
  console.log(`not ok - ${name}\n    got:  ${JSON.stringify(got)}\n    want: ${want instanceof RegExp ? want : JSON.stringify(want)}`);
}

async function raku(p, code, ...values) {
  try {
    return { value: await p.ctx.RAKU(code, ...values) };
  } catch (e) {
    return { error: e.message };
  }
}

// ---- a cold load, timed ------------------------------------------------------------
let t0 = Date.now();
const p = project();
let r = await raku(p, '$^a * 2', 21);
const cold = Date.now() - t0;
check('first formula', r, { value: 42 });

t0 = Date.now();
for (let i = 0; i < 20; i++) await raku(p, '$^a * 2', i);
const warm = (Date.now() - t0) / 20;

// ---- values in and out ----------------------------------------------------------------
const date = vm.runInContext('new Date(Date.UTC(2026, 9, 8))', p.ctx);
const cases = [
  ['the 43.1 example from Microsoft', ['($^a - $^b) + 1', 43.1, 43.2], { value: 0.9 }],
  ['exact decimals', ['$^a - $^b - $^c', 0.5, 0.4, 0.1], { value: 0 }],
  ['a column range', ['[+] @_', [[0.1], [0.1], [0.1], [0.1], [0.1], [0.1], [0.1], [0.1], [0.1], [0.1]]], { value: 1 }],
  ['a block range flattens in @_', ['[+] @_', [[1, 2], [3, 4]], 10], { value: 20 }],
  ['a row is one list', ['$^a.elems ~ " " ~ $^a.sum', [[1, 2, 3]]], { value: '3 6' }],
  ['a block is a list of rows', ['$^a.map(*.sum).join(",")', [[1, 2], [3, 4]]], { value: '3,7' }],
  ['empty cells are Any', ['@_.grep(*.defined).elems', [[1], [''], [3]]], { value: 2 }],
  ['a rational rounds once', ['1/3 + 1/6'], { value: 0.5 }],
  ['exactly, as text', ['(1/3 + 1/7).raku'], { value: '<10/21>' }],
  ['a big integer is text', ['[*] 1..$^n', 30], { value: '265252859812191058636308480000000' }],
  ['2**53 - 1 stays a number', ['2 ** 53 - 1'], { value: 9007199254740991 }],
  ['a list fills a column', ['(1..4).map(* ** 2)'], { value: [[1], [4], [9], [16]] }],
  ['a list of lists fills a table', ['(1..2).map({ ($_, $_ * 10) })'], { value: [[1, 10], [2, 20]] }],
  ['ragged rows are padded', ['((1, 2, 3), (4,))'], { value: [[1, 2, 3], [4, '', '']] }],
  ['a hash is two columns', ['{ b => 2, a => 1 }'], { value: [['a', 1], ['b', 2]] }],
  ['booleans', ['$^a > 1', 2], { value: true }],
  ['Unicode', ['"élan " ~ $^a.uc', 'ünï'], { value: 'élan ÜNÏ' }],
  ['a date arrives as ISO text', ['$^d.substr(0, 10)', date], { value: '2026-10-08' }],
  ['a sub from the Raku sheet', ['double($^a)', 4], { value: 8 }],
  ['an IBAN', ['iban-ok($^s)', 'GB82 WEST 1234 5698 7654 32'], { value: true }],
  ['a wrong IBAN', ['iban-ok($^s)', 'GB82 WEST 1234 5698 7654 33'], { value: false }],
  ['a regex', ['~($^s ~~ / \\d+ " kg" /)', 'Box of 12 kg flour'], { value: '12 kg' }],
  ['a syntax error', ['$^a +* 2', 1], { error: 'Two terms in a row (missing semicolon?)' }],
  ['an unknown sub', ['nope()'], { error: /^Undefined routine 'nope'/ }],
  ['division by zero', ['1/0'], { error: '#DIV/0: the result divides by zero' }],
  ['infinity', ['1e300 * 1e300'], { error: '#NUM: the result is Inf' }],
  ['a lazy list', ['1..*'], { error: 'the result is a lazy list; keep part of it with .head(N)' }],
  ['code left over', ['* + 1'], { error: 'the formula returned code, not a value (is a * left over?)' }],
  ['die', ['die "no such account"'], { error: 'no such account' }],
];
for (const [name, args, want] of cases) check(name, await raku(p, ...args), want);

// `say` goes to the execution log, not into the cell.
r = await raku(p, 'say "from the formula"; 7');
check('say', [r, p.logs.includes('from the formula')], [{ value: 7 }, true]);

// ---- the Raku sheet itself -----------------------------------------------------------
check('no Raku sheet', await raku(project({ lines: null }), 'double(2)'), { error: /^Undefined routine 'double'/ });
r = await raku(project({ lines: ['sub ok() { 1 }', 'sub broken( { }'] }), '1 + 1');
check('an error on the Raku sheet names its row', /^Raku sheet: .*row 2/.test(r.error) ? 'row 2' : r, 'row 2');

// ---- damaged and missing engine files --------------------------------------------------
const flip = s => { const at = s.indexOf('"') + 5000; return s.slice(0, at) + (s[at] === 'A' ? 'B' : 'A') + s.slice(at + 1); };
r = await raku(project({ edit: (f, s) => (f === 'RakuWasm03.gs' ? flip(s) : s) }), '1');
check('a damaged engine file', /damaged/.test(r.error) ? 'damaged' : r, 'damaged');
r = await raku(project({ edit: (f, s) => (f === 'RakuWasm02.gs' ? null : s) }), '1');
check('a missing engine file', r, { error: 'the Raku engine is incomplete: file RakuWasm02 is missing' });

console.log(`${failed ? 'FAIL' : 'PASS'}: ${passed} passed, ${failed} failed; cold start ${cold} ms, then ${warm.toFixed(1)} ms a formula`);
process.exit(failed ? 1 : 0);
