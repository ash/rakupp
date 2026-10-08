// functions.js — =RAKU.EVAL(code, values...) for Excel.
//
// Loaded by taskpane.html, which Excel keeps running in the background (the
// manifest's shared runtime), so the engine is loaded once per workbook
// session. Excel calls the function once per formula; the calls are
// collected for a moment and run as one batch: one Raku program, however
// many cells.

/* global CustomFunctions, Excel, RakuJS, RakuSheet */

var RakuExcel = (function () {
  'use strict';

  var TAG = '@TAG@';                 // build stamp; see rakusheet-worker.js
  var TIMEOUT_MS = 15000;            // one batch; a longer one is stopped
  var BATCH_DELAY_MS = 10;
  var MAX_BATCH = 500;
  var MAX_DEFINITION_ROWS = 10000;

  // Where this script was loaded from: the worker and the engine sit beside it.
  var HERE = new URL('.', document.currentScript ? document.currentScript.src : location.href).href;
  function url(file) { return HERE + file + '?v=' + TAG; }

  // ---- what the task pane shows ----------------------------------------------

  var status = {
    state: 'loading', version: '', loadMs: 0, error: '',
    batches: 0, formulas: 0, lastMs: 0, printed: []
  };
  var listeners = [];
  function changed() {
    listeners.forEach(function (f) { try { f(status); } catch (e) { /* a pane bug is not a formula's */ } });
  }

  // ---- the worker ---------------------------------------------------------------

  var worker = null, ready = null, nextId = 1, waiting = {};

  function startWorker() {
    var t0 = Date.now();
    worker = new Worker(url('rakusheet-worker.js'));
    ready = new Promise(function (resolve, reject) {
      worker.onmessage = function (e) {
        var m = e.data;
        if (m.type === 'ready') {
          status.state = 'ready';
          status.version = m.version;
          status.loadMs = status.loadMs || Date.now() - t0;
          status.error = '';
          changed();
          resolve();
        } else if (m.type === 'loaderror') {
          status.state = 'error';
          status.error = m.message;
          changed();
          // The next batch starts a new worker and tries again.
          worker.terminate();
          worker = null;
          reject(new Error('the Raku engine did not load: ' + m.message));
        } else if (waiting[m.id]) {
          var w = waiting[m.id];
          delete waiting[m.id];
          w(m);
        }
      };
      worker.onerror = function (e) {
        status.state = 'error';
        status.error = e.message || 'the worker failed';
        changed();
        worker.terminate();
        worker = null;
        reject(new Error('the Raku engine did not load: ' + status.error));
      };
    });
  }

  function stopWorker() {
    if (worker) worker.terminate();
    worker = null;
    ready = null;
    Object.keys(waiting).forEach(function (id) {
      var w = waiting[id];
      delete waiting[id];
      w({ type: 'failed', message: 'the Raku engine was restarted' });
    });
  }

  function inWorker(definitions, calls) {
    if (!worker) startWorker();
    return ready.then(function () {
      return new Promise(function (resolve) {
        var id = nextId++;
        var timer = setTimeout(function () {
          delete waiting[id];
          stopWorker();
          resolve({ type: 'timeout' });
        }, TIMEOUT_MS);
        waiting[id] = function (m) { clearTimeout(timer); resolve(m); };
        worker.postMessage({ type: 'run', id: id, definitions: definitions, calls: calls });
      });
    });
  }

  // The page's own thread: several times deeper recursion than a worker's,
  // but nothing can stop a run there, so only a batch that already
  // overflowed the worker comes here.
  var pageEngine = null;
  function onPage() {
    if (!pageEngine) {
      pageEngine = new Promise(function (resolve, reject) {
        var s = document.createElement('script');
        s.src = url('rakujs.js');
        s.onload = resolve;
        s.onerror = function () { reject(new Error('rakujs.js did not load')); };
        document.head.appendChild(s);
      }).then(function () {
        return RakuSheet.start(RakuJS, { locateFile: function (p) { return HERE + p + '?v=' + TAG; } });
      });
    }
    return pageEngine;
  }

  function everyone(calls, message, kind) {
    return {
      answers: calls.map(function () { return { err: message, kind: kind || 'value' }; }),
      printed: [], ms: 0
    };
  }

  function runBatch(definitions, calls) {
    return inWorker(definitions, calls).then(function (m) {
      if (m.type === 'done') return m.result;
      if (m.type === 'timeout') {
        if (calls.length === 1) {
          return everyone(calls, 'the Raku code ran longer than ' + TIMEOUT_MS / 1000 + ' seconds and was stopped', 'na');
        }
        // One at a time, so that only the slow formula fails.
        var answers = [], printed = [];
        return calls.reduce(function (p, call) {
          return p.then(function () {
            return runBatch(definitions, [call]).then(function (r) {
              answers.push(r.answers[0]);
              printed = printed.concat(r.printed);
            });
          });
        }, Promise.resolve()).then(function () { return { answers: answers, printed: printed, ms: 0 }; });
      }
      if (m.deep) {
        return onPage().then(function (engine) {
          try {
            return engine.run(definitions, calls);
          } catch (e) {
            pageEngine = null;
            return everyone(calls, 'the Raku code recursed deeper than Excel allows');
          }
        });
      }
      return everyone(calls, 'the Raku engine stopped: ' + m.message);
    }, function (e) {
      return everyone(calls, e.message);
    });
  }

  // ---- the Raku sheet ---------------------------------------------------------------

  // Column A of the worksheet named Raku, one line per row, so that a line
  // number in an error is a row number there.
  function definitions() {
    if (typeof Excel === 'undefined') return Promise.resolve('');
    return Excel.run(function (ctx) {
      var sheet = ctx.workbook.worksheets.getItemOrNullObject('Raku');
      return ctx.sync().then(function () {
        if (sheet.isNullObject) return '';
        var used = sheet.getUsedRangeOrNullObject(true);
        used.load('rowIndex,rowCount');
        return ctx.sync().then(function () {
          if (used.isNullObject) return '';
          var last = Math.min(used.rowIndex + used.rowCount, MAX_DEFINITION_ROWS);
          var column = sheet.getRange('A1:A' + last);
          column.load('values');
          return ctx.sync().then(function () {
            return column.values.map(function (row) {
              return row[0] === null || row[0] === undefined ? '' : String(row[0]);
            }).join('\n');
          });
        });
      });
    }).catch(function () { return ''; });
  }

  // ---- the custom function -------------------------------------------------------------

  var queue = [], timer = null, chain = Promise.resolve();

  // `values` is the repeating parameter: one matrix per argument. Excel
  // passes an invocation object last, which is not a value.
  function evaluate(code, values) {
    var args = Array.isArray(values) ? values : [];
    return new Promise(function (resolve, reject) {
      queue.push({ code: code, args: args, resolve: resolve, reject: reject });
      if (!timer) timer = setTimeout(flush, BATCH_DELAY_MS);
    });
  }

  function flush() {
    timer = null;
    var batch = queue.splice(0, MAX_BATCH);
    if (queue.length) timer = setTimeout(flush, 0);
    chain = chain.then(function () { return process(batch); });
  }

  function process(batch) {
    return definitions().then(function (defs) {
      return runBatch(defs, batch.map(function (c) { return { code: c.code, args: c.args }; }));
    }).then(function (result) {
      status.batches++;
      status.formulas += batch.length;
      status.lastMs = result.ms;
      if (result.printed.length) status.printed = status.printed.concat(result.printed).slice(-200);
      changed();
      batch.forEach(function (c, i) { settle(c, result.answers[i]); });
    }).catch(function (e) {
      batch.forEach(function (c) { settle(c, { err: String(e && e.message || e), kind: 'value' }); });
    });
  }

  // Only #VALUE! and #N/A carry a message, and #NAME? cannot be a result,
  // so an undeclared name is #VALUE! with its message.
  function settle(c, answer) {
    if (!answer) answer = { err: 'the batch came back without this formula', kind: 'value' };
    if ('ok' in answer) {
      c.resolve(RakuSheet.cells(answer.ok));
      return;
    }
    var code = CustomFunctions.ErrorCode;
    if (answer.kind === 'div0') c.reject(new CustomFunctions.Error(code.divisionByZero));
    else if (answer.kind === 'num') c.reject(new CustomFunctions.Error(code.invalidNumber));
    else if (answer.kind === 'na') c.reject(new CustomFunctions.Error(code.notAvailable, answer.err));
    else c.reject(new CustomFunctions.Error(code.invalidValue, answer.err));
  }

  if (typeof CustomFunctions !== 'undefined') CustomFunctions.associate('EVAL', evaluate);

  // Start loading at once, so that the first formula does not wait for it.
  startWorker();
  ready.catch(function () { /* reported through status */ });

  return {
    status: status,
    onChange: function (f) { listeners.push(f); f(status); },
    evaluate: evaluate,
    definitions: definitions
  };
})();
