/**
 * @OnlyCurrentDoc
 */

// Raku.gs — =RAKU(code, values...) for Google Sheets, computed in the Raku
// sidebar.
//
// Apps Script keeps nothing from one run to the next, so a formula that loads
// the engine itself (google-sheets/Raku.js) pays for it in every cell. Here
// the engine runs in the sidebar, a page in the browser that stays open and
// keeps it loaded, and this project holds no engine at all. A formula looks
// for its result in the document's cache. The first time, it leaves its code
// and values there instead, and shows RAKU_PENDING with the key it left them
// under. The sidebar polls for such cells (rakuPending), runs them all as one
// batch, hands the results back (rakuStore), and enters those formulas again:
// Sheets runs a custom function again only when its formula changes, and this
// time it finds its result.

var RAKU_PENDING = '⏳ Raku sidebar';
var RAKU_CACHE_SECONDS = 21600;   // the longest CacheService keeps a value
var RAKU_MAX_VALUE = 90000;       // characters; CacheService takes 100 KB a value

/**
 * Runs Raku code. $^a, $^b, ... are the values after the code, in order, and
 * @_ is all of them, ranges flattened. Subs written on the sheet named Raku
 * (column A) can be called by name. The Raku sidebar computes it: keep it
 * open (Raku → Open the Raku sidebar).
 *
 * @param {string} code Raku code, for example "$^a * 2" or "[+] @_".
 * @param {any} values Cells, ranges or values the code reads.
 * @return The result. A list fills a column; a list of lists fills a table.
 * @customfunction
 */
function RAKU(code, ...values) {
  var key = rakuKey(code, values, rakuSheetDefinitions());
  var cache = CacheService.getDocumentCache();
  var hit = cache.get('raku:res:' + key);
  if (hit !== null) {
    var answer = JSON.parse(hit);
    if ('err' in answer) throw new Error(answer.err);
    var cells = answer.cells;
    return cells.length === 1 && cells[0].length === 1 ? cells[0][0] : cells;
  }
  var request = JSON.stringify({ code: String(code), args: values });
  if (request.length > RAKU_MAX_VALUE) throw new Error('the values are too large to hand to the Raku sidebar');
  cache.put('raku:req:' + key, request, RAKU_CACHE_SECONDS);
  return RAKU_PENDING + ' ' + key;
}

// What a formula's result depends on: its code, its values and the Raku
// sheet. Dates are ISO 8601 text in the JSON, as the engine receives them.
function rakuKey(code, values, definitions) {
  var digest = Utilities.computeDigest(Utilities.DigestAlgorithm.SHA_256,
    String(code) + '\u0000' + JSON.stringify(values) + '\u0000' + definitions, Utilities.Charset.UTF_8);
  return digest.slice(0, 8).map(function (b) { return ('0' + (b & 255).toString(16)).slice(-2); }).join('');
}

// Column A of the sheet named Raku, one line per row, so that a line number
// in an error message is a row number there.
function rakuSheetDefinitions() {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName('Raku');
  if (!sheet) return '';
  var last = Math.min(sheet.getLastRow(), 10000);
  if (last < 1) return '';
  return sheet.getRange(1, 1, last, 1).getDisplayValues()
    .map(function (row) { return row[0]; })
    .join('\n');
}

// ---- for the sidebar -------------------------------------------------------------

// The formulas waiting for the sidebar, with the code and values they left.
// A request the cache has lost is asked for again by entering its formula
// again.
function rakuPending() {
  var waiting = {};
  rakuEachFormula(function (shown) {
    if (shown.indexOf(RAKU_PENDING + ' ') === 0) waiting[shown.slice(RAKU_PENDING.length + 1)] = true;
  });
  var keys = Object.keys(waiting);
  var found = keys.length ? CacheService.getDocumentCache().getAll(keys.map(function (k) { return 'raku:req:' + k; })) : {};
  var requests = [], lost = {};
  keys.forEach(function (k) {
    var r = found['raku:req:' + k];
    if (r) {
      var q = JSON.parse(r);
      requests.push({ key: k, code: q.code, args: q.args });
    } else {
      lost[k] = true;
    }
  });
  if (Object.keys(lost).length) {
    rakuReenter(function (shown) {
      return shown.indexOf(RAKU_PENDING + ' ') === 0 && lost[shown.slice(RAKU_PENDING.length + 1)];
    });
  }
  var definitions = rakuSheetDefinitions();
  return { definitions: definitions, definitionsKey: rakuKey('', [], definitions), requests: requests };
}

// The sidebar's answers, [{key, cells} or {key, err}], into the cache, and
// the formulas that waited for them entered again.
function rakuStore(answers) {
  var put = {}, done = {};
  answers.forEach(function (a) {
    var json = JSON.stringify('err' in a ? { err: a.err } : { cells: a.cells });
    if (json.length > RAKU_MAX_VALUE) json = JSON.stringify({ err: 'the result is too large for the Raku sidebar to hand back' });
    put['raku:res:' + a.key] = json;
    done[a.key] = true;
  });
  CacheService.getDocumentCache().putAll(put, RAKU_CACHE_SECONDS);
  return rakuReenter(function (shown) {
    return shown.indexOf(RAKU_PENDING + ' ') === 0 && done[shown.slice(RAKU_PENDING.length + 1)];
  });
}

function rakuShowSidebar() {
  SpreadsheetApp.getUi().showSidebar(HtmlService.createHtmlOutputFromFile('RakuSidebar').setTitle('Raku'));
}

// The two example subs come from the sidebar, whose engine files have them.
function rakuAddSheet(lines) {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  if (ss.getSheetByName('Raku')) return 'There is a Raku sheet already.';
  var sheet = ss.insertSheet('Raku');
  // Plain text, so that a line Sheets would read as a number or a date stays code.
  var range = sheet.getRange(1, 1, lines.length, 1);
  range.setNumberFormat('@');
  range.setValues(lines.map(function (l) { return [l]; }));
  sheet.setColumnWidth(1, 900);
  sheet.getRange('A:A').setFontFamily('Roboto Mono');
  return 'The Raku sheet is added.';
}

// ---- the menu ----------------------------------------------------------------------

function onOpen() {
  SpreadsheetApp.getUi()
    .createMenu('Raku')
    .addItem('Open the Raku sidebar', 'rakuShowSidebar')
    .addItem('Recalculate RAKU formulas', 'rakuRecalculate')
    .addSeparator()
    .addItem('How RAKU formulas work', 'rakuHelp')
    .addToUi();
}

function rakuHelp() {
  var ui = SpreadsheetApp.getUi();
  ui.alert('RAKU formulas', [
    '=RAKU(code, values…) runs Raku code in a cell. $^a, $^b, … are the values after the code, in order, and @_ is all of them:',
    '',
    '    =RAKU("$^a * 2", A1)',
    '    =RAKU("[+] @_", A1:A10)',
    '    =RAKU("(1..4).map(* ** 2)")    fills a column',
    '',
    'The Raku sidebar computes the formulas: keep it open (Raku → Open the Raku sidebar). A formula waiting for it shows ' + RAKU_PENDING + '.',
    '',
    'Subs written in column A of a sheet named Raku can be called from any formula; the sidebar can add that sheet, with two examples. When the Raku sheet changes, the sidebar calculates the formulas again.',
    '',
    'The engine runs in your browser, loaded from raku.online: formulas send nothing anywhere else. More at raku.online/embed/spreadsheets.'
  ].join('\n'), ui.ButtonSet.OK);
}

function rakuRecalculate() {
  var n = rakuReenter(function (shown, formula) { return /\bRAKU\s*\(/i.test(formula); });
  SpreadsheetApp.getActiveSpreadsheet().toast(n + ' RAKU formula' + (n === 1 ? '' : 's') + ' recalculated', 'Raku');
  return n;
}

// ---- formulas in the spreadsheet ------------------------------------------------------

function rakuEachFormula(f) {
  SpreadsheetApp.getActiveSpreadsheet().getSheets().forEach(function (sheet) {
    var range = sheet.getDataRange();
    var shown = range.getDisplayValues(), formulas = range.getFormulas();
    formulas.forEach(function (row, r) {
      row.forEach(function (formula, c) {
        if (formula) f(shown[r][c], formula, sheet, r + 1, c + 1);
      });
    });
  });
}

// Takes out the formulas `wanted` picks and puts them back, which makes
// Sheets run them again, and puts back whatever it took out even when
// something fails in between.
function rakuReenter(wanted) {
  var hits = [];
  rakuEachFormula(function (shown, formula, sheet, row, col) {
    if (wanted(shown, formula)) hits.push({ cell: sheet.getRange(row, col), formula: formula });
  });
  var cleared = 0;
  try {
    hits.forEach(function (h) { h.cell.setFormula(''); cleared++; });
    SpreadsheetApp.flush();
  } finally {
    hits.slice(0, cleared).forEach(function (h) { h.cell.setFormula(h.formula); });
  }
  return hits.length;
}
