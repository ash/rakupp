// runner.js — a tiny client around worker.js shared by all three apps.
//
// `new Raku()` spins up the worker and instantiates the wasm. `raku.run(src)`
// returns a Promise of { out, err, rc, ms, deep }. Runs are serialised (one at a
// time); a `debounced(fn, ms)` helper coalesces keystrokes for live editing.

class Raku {
  constructor() {
    this.worker = new Worker('worker.js');
    this.version = null;
    this._cur = null;          // { resolve } for the in-flight run
    this._queue = [];
    this._out = '';
    this._err = '';
    this.ready = new Promise((res, rej) => { this._readyRes = res; this._readyRej = rej; });
    this.worker.onmessage = (e) => this._onmsg(e.data);
    this.worker.onerror = (e) => { if (this._readyRej) this._readyRej(String(e.message || e)); };
  }

  _onmsg(m) {
    switch (m.type) {
      case 'ready':     this.version = m.version; this._readyRes(m.version); break;
      case 'loaderror': if (this._readyRej) this._readyRej(m.message);
                        if (this._cur) { this._finish({ out: '', err: m.message, rc: -1 }); } break;
      case 'start':     this._out = ''; this._err = ''; break;
      case 'out':       (m.cls === 'err' ? this._err += m.text : this._out += m.text); break;
      case 'done':      this._finish({ out: this._out, err: this._err, rc: m.rc, ms: m.ms }); break;
      case 'runerror':  this._finish({ out: this._out, err: this._err + m.message, rc: 1, deep: m.deep }); break;
    }
  }

  _finish(result) {
    const cur = this._cur;
    this._cur = null;
    if (cur) cur.resolve(result);
    this._next();
  }

  run(src) {
    return new Promise((resolve) => { this._queue.push({ src, resolve }); this._next(); });
  }

  _next() {
    if (this._cur || !this._queue.length) return;
    this._cur = this._queue.shift();
    this.worker.postMessage({ type: 'run', src: this._cur.src });
  }
}

function debounced(fn, ms = 250) {
  let t = null;
  return (...args) => { clearTimeout(t); t = setTimeout(() => fn(...args), ms); };
}

// Emit a Raku statement that binds `varName` to arbitrary user text, using a
// non-interpolating heredoc (so nothing in the text is treated as Raku). The `;`
// goes on the opening line, as heredoc syntax requires; the terminator sits alone
// on its own line. If the text somehow contains the terminator we nudge it.
function rakuHeredoc(varName, text, term = '__RAKU_WEB_INPUT__') {
  let body = String(text);
  if (body.split('\n').some(l => l.trim() === term)) body = body.replaceAll(term, term + '_');
  return `my ${varName} = q:to/${term}/;\n${body}\n${term}\n`;
}
