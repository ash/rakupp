// rakusheet-worker.js — runs batches of Raku formulas off Excel's thread.
//
// rakupp_run is synchronous: a formula that loops forever would hold the
// add-in's page with it. In a worker it holds only the worker, which
// functions.js terminates after a timeout and replaces.

/* global RakuJS, RakuSheet, importScripts */

// The build stamp the page loaded us with (?v=...), passed on to the engine's
// files so that Office's cache never mixes two builds.
var V = self.location.search;
importScripts('rakujs.js' + V, 'rakusheet-core.js' + V);

var engine = null;
var ready = load();

function load() {
  return RakuSheet.start(RakuJS, { locateFile: function (p) { return p + V; } }).then(
    function (e) {
      engine = e;
      self.postMessage({ type: 'ready', version: e.version });
    },
    function (err) {
      self.postMessage({ type: 'loaderror', message: String(err && err.message || err) });
    });
}

self.onmessage = function (e) {
  var m = e.data;
  if (m.type !== 'run') return;
  ready.then(function () {
    if (!engine) {
      self.postMessage({ type: 'failed', id: m.id, message: 'the Raku engine did not load' });
      return;
    }
    try {
      self.postMessage({ type: 'done', id: m.id, result: engine.run(m.definitions, m.calls) });
    } catch (err) {
      // A stack overflow or an `exit` leaves the instance unusable: make a
      // new one for the next batch. `deep` asks the page to retry this one
      // on its own thread, whose stack is several times a worker's.
      var message = String(err && err.message || err);
      engine = null;
      ready = load();
      self.postMessage({ type: 'failed', id: m.id, message: message, deep: /call stack|too much recursion/i.test(message) });
    }
  });
};
