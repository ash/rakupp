// taskpane.js — the Raku pane: engine status, examples, the Raku sheet.

/* global Office, Excel, RakuExcel, RakuSheet */

(function () {
  'use strict';

  var EXAMPLES = [
    { what: 'Exact decimals: 0.5 − 0.4 − 0.1 is 0', formula: '=RAKU.EVAL("0.5 - 0.4 - 0.1")' },
    { what: 'A big integer, all its digits, as text', formula: '=RAKU.EVAL("[*] 1..$^n", 30)' },
    { what: 'The sum of a range', formula: '=RAKU.EVAL("[+] @_", A1:A10)' },
    { what: 'A list fills a column', formula: '=RAKU.EVAL("(1..10).map(* ** 2)")' },
    { what: 'A regex on text', formula: '=RAKU.EVAL("~($^s ~~ / \\d+ \' kg\' /)", "Box of 12 kg flour")' },
    { what: 'A sub from the Raku sheet', formula: '=RAKU.EVAL("iban-ok($^s)", "GB82 WEST 1234 5698 7654 32")' }
  ];

  function $(id) { return document.getElementById(id); }

  function showStatus(s) {
    var state = $('state');
    state.className = 'state ' + s.state;
    if (s.state === 'ready') {
      state.textContent = 'Ready: Raku++ ' + s.version + ', loaded in ' + (s.loadMs / 1000).toFixed(1) + ' s';
    } else if (s.state === 'error') {
      state.textContent = 'The engine did not load: ' + s.error;
    } else {
      state.textContent = 'Loading the engine…';
    }
    $('formulas').textContent = s.formulas;
    $('batches').textContent = s.batches;
    $('last').textContent = s.batches ? s.lastMs + ' ms' : '–';
    $('printed').textContent = s.printed.length ? s.printed.join('\n') : 'Nothing yet.';
  }

  function listExamples(canInsert) {
    var list = $('examples');
    EXAMPLES.forEach(function (ex) {
      var li = document.createElement('li');
      var what = document.createElement('div');
      what.className = 'what';
      what.textContent = ex.what;
      var row = document.createElement('div');
      row.className = 'row';
      var code = document.createElement('code');
      code.textContent = ex.formula;
      row.appendChild(code);
      if (canInsert) {
        var b = document.createElement('button');
        b.className = 'small';
        b.textContent = 'Insert';
        b.onclick = function () { insert(ex.formula); };
        row.appendChild(b);
      }
      li.appendChild(what);
      li.appendChild(row);
      list.appendChild(li);
    });
  }

  function insert(formula) {
    return Excel.run(function (ctx) {
      ctx.workbook.getActiveCell().formulas = [[formula]];
      return ctx.sync();
    }).catch(report);
  }

  function addSheet() {
    return Excel.run(function (ctx) {
      var existing = ctx.workbook.worksheets.getItemOrNullObject('Raku');
      return ctx.sync().then(function () {
        if (!existing.isNullObject) {
          existing.activate();
          return ctx.sync();
        }
        var lines = RakuSheet.exampleDefinitions('RAKU.EVAL');
        var sheet = ctx.workbook.worksheets.add('Raku');
        // Text, so that a line Excel would read as a number, a date or a
        // formula stays a line of code.
        var rows = 500;
        var formats = [];
        for (var i = 0; i < rows; i++) formats.push(['@']);
        sheet.getRange('A1:A' + rows).numberFormat = formats;
        sheet.getRange('A1:A' + lines.length).values = lines.map(function (l) { return [l]; });
        var column = sheet.getRange('A:A');
        column.format.columnWidth = 560;
        column.format.font.name = 'Consolas';
        sheet.activate();
        return ctx.sync();
      });
    }).catch(report);
  }

  function recalculate() {
    return Excel.run(function (ctx) {
      ctx.workbook.application.calculate(Excel.CalculationType.full);
      return ctx.sync();
    }).catch(report);
  }

  function report(e) {
    $('printed').textContent = 'Excel refused: ' + (e && e.message || e);
  }

  Office.onReady(function (info) {
    var inExcel = info && info.host === Office.HostType.Excel;
    listExamples(inExcel);
    $('add-sheet').onclick = addSheet;
    $('recalc').onclick = recalculate;
    $('add-sheet').disabled = $('recalc').disabled = !inExcel;
    RakuExcel.onChange(showStatus);
  });
})();
