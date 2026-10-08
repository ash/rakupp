/**
 * @OnlyCurrentDoc
 */

// Raku.gs — =RAKU(code, values...) for Google Sheets.
//
// Every formula is its own Apps Script execution, so each one loads the
// engine (RakuLoader.gs) unless Sheets happens to reuse an execution. Give one
// formula a whole range rather than writing one formula per cell: a range
// in, a column or a table out, one load.

/**
 * Runs Raku code. $^a, $^b, ... are the values after the code, in order, and
 * @_ is all of them, ranges flattened. Subs written on the sheet named Raku
 * (column A) can be called by name.
 *
 * @param {string} code Raku code, for example "$^a * 2" or "[+] @_".
 * @param {any} values Cells, ranges or values the code reads.
 * @return The result. A list fills a column; a list of lists fills a table.
 * @customfunction
 */
async function RAKU(code, ...values) {
  var engine = await rakuSheetEngine();
  var result;
  try {
    result = engine.run(rakuSheetDefinitions(), [{ code: code, args: values }]);
  } catch (e) {
    RAKUSHEET_ENGINE = null;
    throw new Error(/call stack/i.test(String(e))
      ? 'the Raku code recursed deeper than Apps Script allows'
      : 'the Raku engine stopped: ' + e);
  }
  result.printed.forEach(function (line) { console.log(line); });
  var answer = result.answers[0];
  if ('err' in answer) throw new Error(answer.err);
  var cells = RakuSheet.cells(answer.ok);
  return cells.length === 1 && cells[0].length === 1 ? cells[0][0] : cells;
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

function onOpen() {
  SpreadsheetApp.getUi()
    .createMenu('Raku')
    .addItem('Add the Raku sheet', 'rakuAddSheet')
    .addItem('Recalculate RAKU formulas', 'rakuRecalculate')
    .addToUi();
}

function rakuAddSheet() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  if (ss.getSheetByName('Raku')) {
    SpreadsheetApp.getUi().alert('There is a Raku sheet already.');
    return;
  }
  var lines = RakuSheet.exampleDefinitions('RAKU');
  var sheet = ss.insertSheet('Raku');
  // Plain text, so that a line Sheets would read as a number or a date stays code.
  var range = sheet.getRange(1, 1, lines.length, 1);
  range.setNumberFormat('@');
  range.setValues(lines.map(function (l) { return [l]; }));
  sheet.setColumnWidth(1, 900);
  sheet.getRange('A:A').setFontFamily('Roboto Mono');
}

// Sheets recalculates a custom function only when its arguments change, so
// an edit on the Raku sheet does not reach the formulas that use it. This
// takes every RAKU formula out and puts it back.
function rakuRecalculate() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var found = 0;
  ss.getSheets().forEach(function (sheet) {
    var range = sheet.getDataRange();
    var formulas = range.getFormulas();
    var hits = [];
    formulas.forEach(function (row, r) {
      row.forEach(function (f, c) {
        if (/\bRAKU\s*\(/i.test(f)) hits.push({ row: r + 1, col: c + 1, formula: f });
      });
    });
    hits.forEach(function (h) { sheet.getRange(h.row, h.col).setFormula(''); });
    SpreadsheetApp.flush();
    hits.forEach(function (h) { sheet.getRange(h.row, h.col).setFormula(h.formula); });
    found += hits.length;
  });
  ss.toast(found + ' RAKU formula' + (found === 1 ? '' : 's') + ' recalculated', 'Raku');
}
