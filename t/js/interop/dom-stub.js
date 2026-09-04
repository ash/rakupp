// A small DOM stand-in for the --target=js interop goldens (t/js/run.raku):
// enough of document/element for the programs in this directory, loaded with
// `node -r` before the transpiled program. Not a browser; no layout, no events
// beyond what addEventListener/dispatch below do.
class Element {
    constructor(tag) { this.tagName = tag.toUpperCase(); this.children = []; this.attributes = {}; this.textContent = ''; this.listeners = {}; this.id = ''; this.className = ''; this.style = {}; }
    appendChild(c) { this.children.push(c); c.parentNode = this; return c; }
    setAttribute(k, v) { this.attributes[k] = String(v); if (k === 'id') this.id = String(v); }
    getAttribute(k) { return k in this.attributes ? this.attributes[k] : null; }
    addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
    dispatchEvent(ev) { for (const f of this.listeners[ev.type] || []) f(ev); return true; }
    querySelector(sel) { return findOne(this, sel); }
    querySelectorAll(sel) { const out = []; findAll(this, sel, out); return out; }
    get innerHTML() { return this.children.map(c => c.outerHTML).join('') + this.textContent; }
    set innerHTML(s) { this.children = []; this.textContent = s; }
    get outerHTML() { const attrs = Object.entries(this.attributes).map(([k, v]) => ` ${k}="${v}"`).join(''); return `<${this.tagName.toLowerCase()}${attrs}>${this.innerHTML}</${this.tagName.toLowerCase()}>`; }
}
function matches(el, sel) { if (sel.startsWith('#')) return el.id === sel.slice(1); if (sel.startsWith('.')) return el.className.split(' ').includes(sel.slice(1)); return el.tagName === sel.toUpperCase(); }
function findOne(root, sel) { for (const c of root.children) { if (matches(c, sel)) return c; const r = findOne(c, sel); if (r) return r; } return null; }
function findAll(root, sel, out) { for (const c of root.children) { if (matches(c, sel)) out.push(c); findAll(c, sel, out); } }
const document = {
    body: new Element('body'),
    createElement: tag => new Element(tag),
    getElementById(id) { return findOne(this.body, '#' + id); },
    querySelector(sel) { return findOne(this.body, sel); },
    querySelectorAll(sel) { const out = []; findAll(this.body, sel, out); return out; },
    title: 'stub',
};
globalThis.document = document;
globalThis.window = globalThis;
globalThis.Event = class Event { constructor(type) { this.type = type; } };
