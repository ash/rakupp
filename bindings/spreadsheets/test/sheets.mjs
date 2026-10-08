// sheets.mjs — runs the generated Google Sheets project the way Apps Script
// does: every .gs file in one bare V8 context, with none of the browser's or
// Node's globals (no TextDecoder, performance, crypto, setTimeout, process),
// in an order of its own. SpreadsheetApp is a stand-in holding a Raku sheet.
//
//   node bindings/spreadsheets/test/sheets.mjs [dist/google-sheets]
//   node bindings/spreadsheets/test/sheets.mjs dist/google-sheets-addon --addon
//
// --addon tests the Marketplace add-on's build instead of the attached
// script's: its menu under Extensions, onInstall, and no log of what formulas
// print.

import vm from 'node:vm';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const addon = process.argv.includes('--addon');
const dir = process.argv.slice(2).find(a => !a.startsWith('--'))
  ?? path.join(here, '..', 'dist', addon ? 'google-sheets-addon' : 'google-sheets');
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

// The menus a script adds and the dialogs it shows go into `ui`.
function userInterface(ui) {
  const menu = kind => {
    const m = { kind, items: [] };
    m.addItem = (label, fn) => { m.items.push([label, fn]); return m; };
    m.addSeparator = () => { m.items.push(['-']); return m; };
    m.addToUi = () => { ui.menus.push(m); };
    return m;
  };
  return {
    createMenu: name => menu(`menu ${name}`),
    createAddonMenu: () => menu('add-on menu'),
    ButtonSet: { OK: 'OK' },
    alert: (...a) => { ui.alerts.push(a); },
  };
}

function spreadsheet(lines, ui) {
  const sheet = lines && {
    getLastRow: () => lines.length,
    getRange: (row, col, rows) => ({
      getDisplayValues: () => lines.slice(row - 1, row - 1 + rows).map(l => [l]),
    }),
  };
  return {
    getActiveSpreadsheet: () => ({ getSheetByName: name => (name === 'Raku' ? sheet : null) }),
    getUi: () => userInterface(ui),
  };
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
function project({ lines = RAKU_SHEET, edit, app } = {}) {
  const logs = [];
  const ui = { menus: [], alerts: [] };
  const ctx = vm.createContext({
    console: { log: (...a) => logs.push(a.join(' ')), warn() {}, error() {}, info() {} },
    SpreadsheetApp: app ?? spreadsheet(lines, ui),
  });
  for (const f of ['TextDecoder', 'performance', 'crypto', 'setTimeout', 'process', 'window']) {
    if (vm.runInContext(`typeof ${f}`, ctx) !== 'undefined') throw new Error(`the sandbox has ${f}`);
  }
  // Newer than some Apps Script runtimes; the engine gets a shim for it.
  vm.runInContext('delete Array.prototype.at', ctx);
  for (const f of shuffle(files)) {
    let src = fs.readFileSync(path.join(dir, f), 'utf8');
    if (edit) src = edit(f, src);
    if (src !== null) vm.runInContext(src, ctx, { filename: f });
  }
  return { ctx, logs, ui };
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

// ---- what Apps Script's parser refuses -----------------------------------------------
// It reads every file when the project is saved, with a parser older than V8:
// logical assignment and class fields are a syntax error there, though V8,
// and so this test, runs them. build.raku rewrites both out of the engine.
for (const f of files) {
  const src = fs.readFileSync(path.join(dir, f), 'utf8');
  check(`${f}: no ??=, ||= or &&=`, (src.match(/\?\?=|\|\|=|&&=/g) ?? []).length, 0);
  check(`${f}: no class fields`, (src.match(/\bclass\b[\w$\s.]*\{\s*[\w$]+\s*=/g) ?? []).length, 0);
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

// `say` goes to the execution log, not into the cell; the add-on keeps none,
// since an add-on's log is its developer's.
r = await raku(p, 'say "from the formula"; 7');
check('say', [r, p.logs.includes('from the formula')], [{ value: 7 }, !addon]);

// ---- the menu, and the help it offers ---------------------------------------------------
const MENU = ['Add the Raku sheet', 'Recalculate RAKU formulas', '-', 'How RAKU formulas work'];
const m = project();
vm.runInContext('onOpen({ authMode: "NONE" })', m.ctx);
check('the menu', m.ui.menus.map(x => [x.kind, x.items.map(i => i[0])]), [[addon ? 'add-on menu' : 'menu Raku', MENU]]);
check('every menu item has its function',
  m.ui.menus[0].items.filter(i => i[1]).map(i => typeof m.ctx[i[1]]), ['function', 'function', 'function']);
check('onInstall', typeof m.ctx.onInstall, addon ? 'function' : 'undefined');
if (addon && typeof m.ctx.onInstall === 'function') {
  vm.runInContext('onInstall({ authMode: "FULL" })', m.ctx);
  check('onInstall adds the menu to the open spreadsheet', m.ui.menus.length, 2);
}
vm.runInContext('rakuHelp()', m.ctx);
check('the help is a dialog', m.ui.alerts.map(a => [a[0], /=RAKU\("\$\^a \* 2", A1\)/.test(a[1])]), [['RAKU formulas', true]]);

// ---- Recalculate takes the RAKU formulas out and puts them back ----------------------------
function formulaBook(grid, failAt) {
  const done = [], toasts = [];
  let writes = 0;
  const sheet = {
    getDataRange: () => ({ getFormulas: () => grid.map(row => row.slice()) }),
    getRange: (r, c) => ({
      setFormula: f => {
        if (++writes === failAt) throw new Error('Service invoked too many times');
        grid[r - 1][c - 1] = f;
        done.push(f === '' ? `clear ${r},${c}` : `put ${r},${c}`);
      },
    }),
  };
  const app = {
    getActiveSpreadsheet: () => ({ getSheets: () => [sheet], getSheetByName: () => null, toast: t => toasts.push(t) }),
    flush: () => done.push('flush'),
  };
  return { app, grid, done, toasts };
}
const GRID = () => [['=RAKU("1")', '', '=SUM(A1)'], ['', '=raku("[+] @_", A1:A3)', '']];
let book = formulaBook(GRID());
vm.runInContext('rakuRecalculate()', project({ app: book.app }).ctx);
check('recalculate', [book.done, book.grid, book.toasts],
  [['clear 1,1', 'clear 2,2', 'flush', 'put 1,1', 'put 2,2'], GRID(), ['2 RAKU formulas recalculated']]);
book = formulaBook(GRID(), 2);
let threw = false;
try { vm.runInContext('rakuRecalculate()', project({ app: book.app }).ctx); } catch (e) { threw = /too many/.test(e.message); }
check('a failed recalculation puts back what it took out', [threw, book.done, book.grid], [true, ['clear 1,1', 'put 1,1'], GRID()]);

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
