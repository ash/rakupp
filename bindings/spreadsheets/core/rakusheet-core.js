// rakusheet-core.js — what the Excel add-in and the Google Sheets script share.
//
// A plain script that defines one global, RakuSheet, because Apps Script has
// no modules and a Web Worker loads it with importScripts. It turns
// spreadsheet values into the JSON request rakusheet.raku reads, runs one
// batch of formulas through Raku.js's rakupp_run, and turns the answer back
// into cell values. Each host keeps only what is its own: how the engine is
// loaded, where the definitions come from, and how an error is shown.

var RakuSheet = (function () {
  'use strict';

  // build.raku puts rakusheet.raku here as a JSON string.
  var DRIVER = '@RAKUSHEET_DRIVER@';

  var RS = '\x1E';               // marks the driver's answer line
  var MAX_REQUEST = 8 * 1024 * 1024;   // ccall copies strings onto a 16 MB stack

  // What a new "Raku" sheet starts with, in column A, one line per row.
  // `fn` is how the host spells the formula: RAKU.EVAL in Excel, RAKU in Sheets.
  function exampleDefinitions(fn) {
    return [
    '# Raku definitions: every ' + fn + ' formula in this file can call these subs.',
    '# One line of Raku per row, in column A. Start a line with \' when it begins with = + or -.',
    '',
    '# =' + fn + '("iban-ok($^s)", A2) is TRUE for a valid IBAN: 30 digits, modulo 97, exactly.',
    'sub iban-ok(Str $iban) {',
    '    my $t = $iban.uc.comb(/<alnum>/).join;',
    '    my $digits = ($t.substr(4) ~ $t.substr(0, 4)).comb.map({ /\\d/ ?? $_ !! .ord - 55 }).join;',
    '    $digits % 97 == 1',
    '}',
    '',
    '# =' + fn + '("split-cents($^a, $^n)", 100, 3) fills three cells: 33.34, 33.33, 33.33.',
    'sub split-cents($amount, $n) {',
    '    my $cents = ($amount * 100).round;',
    '    (^$n).map({ ($cents div $n + ($_ < $cents mod $n ?? 1 !! 0)) / 100 })',
    '}'
    ];
  }

  // ---- values in -----------------------------------------------------------

  // One cell. An empty cell (Excel passes null, Sheets '') is Raku's Any. A
  // number goes as JSON prints it, the shortest decimal that is the same
  // double, which the driver reads as that exact decimal: 0.1 is 1/10.
  function scalar(v) {
    if (v === null || v === undefined || v === '') return null;
    if (typeof v === 'number') return isFinite(v) ? v : null;
    if (typeof v === 'boolean' || typeof v === 'string') return v;
    if (v instanceof Date) return isNaN(v.getTime()) ? null : v.toISOString();
    return String(v);
  }

  // One argument: a single cell is its value, a row or a column one list,
  // and anything wider a list of rows.
  function arg(v) {
    if (!Array.isArray(v)) return scalar(v);
    var rows = v.map(function (r) { return Array.isArray(r) ? r : [r]; });
    if (rows.length === 0) return [];
    if (rows.length === 1 && rows[0].length === 1) return scalar(rows[0][0]);
    if (rows.length === 1) return rows[0].map(scalar);
    if (rows.every(function (r) { return r.length === 1; }))
      return rows.map(function (r) { return scalar(r[0]); });
    return rows.map(function (r) { return r.map(scalar); });
  }

  // ---- one batch ------------------------------------------------------------

  // The definitions come first, so that line N of an error is row N of the
  // Raku sheet.
  function program(definitions) {
    return (definitions ? definitions + '\n' : '') + DRIVER;
  }

  function request(calls) {
    return JSON.stringify({
      calls: calls.map(function (c) {
        return { code: String(c.code == null ? '' : c.code), args: (c.args || []).map(arg) };
      })
    });
  }

  // The first line of what the engine printed to stderr that says something,
  // for a batch that never got as far as answering.
  function failure(lines) {
    var text = lines.map(function (l) { return l.replace(/\x1B\[[0-9;]*m/g, '').trim(); })
      .filter(function (l) { return l && !/^===SORRY!===\s*$/.test(l); });
    var first = (text[0] || 'the program stopped without an answer').replace(/^===SORRY!===\s*/, '');
    var where = text.filter(function (l) { return /^at web:\d+/.test(l); })[0];
    return first + (where ? ' (' + where.replace(/^at web:/, 'row ') + ')' : '');
  }

  // An engine is a loaded Raku.js module plus the sink its print callbacks
  // write to. run() is synchronous, as rakupp_run is.
  function Engine(module, sink) {
    this.module = module;
    this.sink = sink;
    this.broken = false;
    this.version = module.ccall('rakupp_version', 'string', [], []);
  }

  // Runs one batch. Returns { answers, printed, warnings, ms }: one answer
  // per call, each { ok: matrix } or { err: message, kind }. A throw out of
  // here (a JavaScript stack overflow, an abort) leaves the engine unusable;
  // `broken` says so, and the host makes a new one.
  Engine.prototype.run = function (definitions, calls) {
    var src = program(definitions);
    var req = request(calls);
    if (src.length + req.length > MAX_REQUEST) {
      return everyone(calls, 'too much data for one batch: ' + Math.round((src.length + req.length) / 1048576) + ' MB');
    }
    var out = [], err = [];
    var t0 = Date.now();
    this.sink.out = function (t) { out.push(t); };
    this.sink.err = function (t) { err.push(t); };
    try {
      this.module.ccall('rakupp_run', 'number', ['string', 'string'], [src, req]);
    } catch (e) {
      this.broken = true;
      throw e;
    } finally {
      this.sink.out = this.sink.err = function () {};
    }
    var answer = null, printed = [];
    out.forEach(function (line) {
      if (line.charAt(0) === RS) answer = line.slice(1); else printed.push(line);
    });
    var ms = Date.now() - t0;
    if (answer === null) {
      var r = everyone(calls, 'Raku sheet: ' + failure(err));
      r.printed = printed; r.warnings = err; r.ms = ms;
      return r;
    }
    return { answers: JSON.parse(answer), printed: printed, warnings: err, ms: ms };
  };

  function everyone(calls, message) {
    return {
      answers: calls.map(function () { return { err: message, kind: 'value' }; }),
      printed: [], warnings: [], ms: 0
    };
  }

  // Loads the engine. `factory` is the RakuJS function rakujs.js defines.
  // In a browser it fetches rakujs.wasm itself (options.locateFile says
  // where); where there is nothing to fetch from, options.wasmBytes holds the
  // module. The glue reads no wasmBinary option, so the bytes go in through
  // its instantiateWasm hook, whose failure would otherwise leave the load
  // waiting forever.
  function start(factory, options) {
    var sink = { out: function () {}, err: function () {} };
    var opts = {}, failed;
    var failure = new Promise(function (resolve, reject) { failed = reject; });
    Object.keys(options || {}).forEach(function (k) { if (k !== 'wasmBytes') opts[k] = options[k]; });
    opts.print = function (t) { sink.out(t); };
    opts.printErr = function (t) { sink.err(t); };
    if (options && options.wasmBytes) {
      var bytes = options.wasmBytes;
      opts.instantiateWasm = function (imports, done) {
        WebAssembly.instantiate(bytes, imports).then(function (r) { done(r.instance, r.module); }, failed);
        return {};
      };
    }
    return Promise.race([factory(opts), failure]).then(function (module) { return new Engine(module, sink); });
  }

  // ---- values out -------------------------------------------------------------

  // A matrix with Raku's Any (null) as an empty cell.
  function cells(matrix) {
    return matrix.map(function (row) {
      return row.map(function (v) { return v === null ? '' : v; });
    });
  }

  // ---- what a runtime without a browser lacks -----------------------------------

  // Apps Script's V8 has no TextDecoder, performance or crypto, all of which
  // the Emscripten glue reaches for. Each is defined only where it is missing.
  function installShims(g) {
    // The engine's C++ exceptions rethrow through exceptionCaught.at(-1).
    if (!g.Array.prototype.at) {
      Object.defineProperty(g.Array.prototype, 'at', {
        configurable: true, writable: true,
        value: function (i) { i = Math.trunc(i) || 0; return this[i < 0 ? i + this.length : i]; }
      });
    }
    if (typeof g.TextDecoder === 'undefined') g.TextDecoder = Utf8Decoder;
    if (typeof g.performance === 'undefined') g.performance = { now: function () { return Date.now(); } };
    if (typeof g.crypto === 'undefined') {
      // Seeds the engine's own random numbers; nothing here needs a secure source.
      g.crypto = {
        getRandomValues: function (view) {
          for (var i = 0; i < view.length; i++) view[i] = Math.floor(Math.random() * 256);
          return view;
        }
      };
    }
  }

  function Utf8Decoder() {}
  Utf8Decoder.prototype.decode = function (bytes) {
    if (!bytes) return '';
    var s = '', units = [], i = 0, n = bytes.length;
    while (i < n) {
      var c = bytes[i++], cp;
      if (c < 0x80) cp = c;
      else if (c < 0xE0) cp = ((c & 0x1F) << 6) | (bytes[i++] & 0x3F);
      else if (c < 0xF0) cp = ((c & 0x0F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F);
      else cp = ((c & 0x07) << 18) | ((bytes[i++] & 0x3F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F);
      if (cp > 0xFFFF) { cp -= 0x10000; units.push(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF)); }
      else units.push(cp);
      if (units.length >= 8192) { s += String.fromCharCode.apply(null, units); units = []; }
    }
    return s + String.fromCharCode.apply(null, units);
  };

  return {
    start: start,
    cells: cells,
    installShims: installShims,
    exampleDefinitions: exampleDefinitions,
    // exposed for the tests
    arg: arg,
    program: program,
    request: request
  };
})();
