# rakupp — Raku grammars from Python, over librakupp's C ABI.
#
# The shape of the thing (GRAMMAR-PLAN.md, G0):
#
#     import rakulang
#     log = rakulang.Grammar.from_file("log.raku", name="Log")
#     m = log.parse(text)                     # a handle, not data
#     for line in m["line"]:                  # lazy: one call per leaf
#         print(line["ip"].str(), line["path"].str())
#     everything = m.tree()                   # eager, opt-in: costs ~3x the parse
#
# The grammar stays a .raku file; this module never grows its own grammar
# syntax. Parsing happens in an embedded Raku++ interpreter — one per process,
# created on first use. Values cross the boundary through a small Raku shim
# (grammar_shim.raku) reached via rk_call.
#
# Threads: one interpreter, one thread. Raku code inside the interpreter may
# use as many threads as it likes; the HOST must not call into one interpreter
# from two Python threads at once.

import os
import re
import threading

from . import _abi
from ._abi import (RK_OK, RK_ANY, RK_BOOL, RK_INT, RK_NUM, RK_RAT, RK_STR,
                   RK_ARRAY, RK_HASH, RK_OTHER)

__all__ = ["Grammar", "Match", "RakuError", "ParseError", "Interp", "interpreter",
           "Object", "Module", "Method"]

_SHIM_ABI = 1  # must equal rk-shim-abi() in grammar_shim.raku
_OBJECT_ABI = 1  # must equal rk-py-abi() in object_shim.raku

# The module name at the front of a use() spec: what `use` takes before any
# adverbs (:ver<...>) or import arguments.
_MODULE_NAME = re.compile(r"[A-Za-z_][\w'-]*(?:::[A-Za-z_][\w'-]*)*")

# rk_int_get is an int64 and BigInt::toLL saturates there, so an Int arriving
# as either of these may be a wider one in disguise — see _int_of.
_INT64_MAX = 2 ** 63 - 1
_INT64_MIN = -(2 ** 63)


class RakuError(Exception):
    """A Raku-side failure: a die, a parse error in grammar source, a missing
    capture. Carries the interpreter's message text."""


class ParseError(RakuError):
    """A grammar did not match its input (raised by parse(strict=True)).
    Carries the engine's highwater diagnosis: .line and .column (1-based),
    .pos (0-based character offset), and .rule — the rule that was trying at
    the furthest point the parse reached. Rule-grained: the position is where
    that rule started, not the exact character."""

    def __init__(self, message, pos=None, line=None, column=None, rule=None):
        super().__init__(message)
        self.pos = pos
        self.line = line
        self.column = column
        self.rule = rule


class _Raw:
    """Marks an argument that is already an RkValue handle, so _call_raw does
    not mistake it for a Python int to convert."""

    __slots__ = ("v",)

    def __init__(self, v):
        self.v = v


class Interp:
    """An embedded Raku++ interpreter. One per process (the runtime refuses a
    second while one is live), so most callers never construct this — the
    module keeps a default instance behind interpreter()."""

    def __init__(self, lib_path=None):
        import ctypes
        self._lib = _abi.load(lib_path)
        # own_stack, which rk_new leaves off for a host that did not ask for a
        # thread. This binding asks: the engine recurses on whatever stack the
        # CALLING thread has, and on Windows that is about 1 MiB — roughly
        # twenty Raku frames at the ~50 KB of C++ stack each one costs. The
        # guide's own example walked a match tree with .tree() and took the
        # process down with it. The CLI, the MCP server and the Jupyter kernel
        # all take the big stack for the same reason.
        cfg = _abi.RkConfig()
        cfg.size = ctypes.sizeof(_abi.RkConfig)
        cfg.own_stack = 1
        self._rk = self._lib.rk_new(ctypes.byref(cfg))
        if not self._rk:
            raise RakuError(
                "rk_new refused: an interpreter is already live in this process"
            )
        self._ctx = self._lib.rk_ctx(self._rk)
        self._objects_loaded = False
        self._main = None
        shim = os.path.join(os.path.dirname(__file__), "grammar_shim.raku")
        with open(shim, "r", encoding="utf-8") as f:
            self.eval(f.read())
        got = self.call("rk-shim-abi")
        if got != _SHIM_ABI:
            raise RakuError(
                f"grammar_shim.raku speaks shim ABI {got}, this binding expects {_SHIM_ABI}"
            )

    # ---- lifecycle ----------------------------------------------------------

    def close(self):
        """Free the interpreter. Every Grammar and Match from it is dead
        afterwards; a fresh Interp may then be created."""
        if self._rk:
            self._lib.rk_free(self._rk)
            self._rk = None
            global _default
            if _default is self:
                _default = None

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _alive(self):
        if not self._rk:
            raise RakuError("this interpreter has been closed")

    # ---- eval and call ------------------------------------------------------

    def eval(self, source):
        """Evaluate Raku source in the interpreter's mainline scope and return
        the last statement's value as a Python value. State persists across
        calls, exactly like the REPL."""
        return self._to_py(self._eval_raw(source))

    def _eval_raw(self, source):
        self._alive()
        import ctypes
        out = ctypes.c_void_p()
        status = self._lib.rk_eval(self._rk, source.encode("utf-8"),
                                   ctypes.byref(out))
        if status != RK_OK:
            msg = self._lib.rk_last_error(self._rk)
            raise RakuError(msg.decode("utf-8", "replace") if msg else "rk_eval failed")
        return out.value

    def call(self, name, /, *args, **named):
        """Call a Raku routine by name with Python arguments, returning a
        Python value. Keyword arguments become Raku named arguments. The
        eager twin of _call_raw."""
        if named or any(_holds_object(a) for a in args):
            # rk_call takes positionals only, and an Object travels boxed:
            # the object shim unboxes and passes the named arguments
            self._load_objects()
            return self._to_py(self._call_raw("rk-py-call-sub", name,
                                              list(args), named))
        return self._to_py(self._call_raw(name, *args))

    # ---- modules and objects -------------------------------------------------

    def lib(self, *paths):
        """Add folders to where `use` looks for modules, as `use lib` does in
        Raku. Takes one folder, several, or a list of them; a folder is a str
        or a pathlib.Path:

            raku.lib("lib")
            raku.lib(["lib", "/opt/raku-modules"])

        Folders are searched in the order given, and before the ones added by
        an earlier lib() call. A relative folder is taken from the current
        directory at the time of the call."""
        flat = []
        for p in paths:
            if isinstance(p, (str, os.PathLike)):
                flat.append(p)
            elif isinstance(p, (list, tuple)):
                flat.extend(p)
            else:
                raise TypeError(
                    f"lib() takes folders as str or pathlib.Path, or a list "
                    f"of them, not {type(p).__name__}")
        if not flat:
            raise ValueError("lib() needs at least one folder")
        quoted = []
        for p in flat:
            if not isinstance(p, (str, os.PathLike)):
                raise TypeError(f"not a folder: {p!r}")
            path = os.path.abspath(os.fspath(p))
            # a single-quoted Raku string: only \\ and \' are special
            quoted.append("'" + path.replace("\\", "\\\\").replace("'", "\\'") + "'")
        # `use lib 'a', 'b'` searches b first; Python lists read left to right
        self._eval_raw("use lib " + ", ".join(reversed(quoted)) + ";")

    def use(self, module):
        """Load a Raku module, as `use` does, and return it as a Module.

        `module` is what follows `use` in Raku: "JSON::Fast", or with import
        arguments, "Geo :ALL". The module's exports also become visible to
        later eval() calls, exactly as after `use` in a Raku program."""
        spec = module.strip()
        m = _MODULE_NAME.match(spec)
        if not m:
            raise ValueError(f"not a module name: {module!r}")
        name = m.group(0)
        quoted = name.replace("'", "\\'")   # Raku names may hold an apostrophe
        self._load_objects()
        raw = self._eval_raw(
            f"use {spec};\n"
            f"do {{ use {spec}; rk-py-module('{quoted}', -> $n {{ ::($n) }}, MY::.keys) }}")
        return Module(self, self._root(raw), name)

    @property
    def main(self):
        """The mainline scope as a Module: every sub and class declared by
        eval(), and everything the core provides, reached from Python as on
        a module from use()."""
        if self._main is None:
            self._load_objects()
            raw = self._eval_raw("rk-py-module('', -> $n { ::($n) }, Any)")
            self._main = Module(self, self._root(raw), "")
        return self._main

    def _load_objects(self):
        if self._objects_loaded:
            return
        shim = os.path.join(os.path.dirname(__file__), "object_shim.raku")
        with open(shim, "r", encoding="utf-8") as f:
            self._eval_raw(f.read())
        got = self.call("rk-py-abi")
        if got != _OBJECT_ABI:
            raise RakuError(
                f"object_shim.raku speaks ABI {got}, this binding expects {_OBJECT_ABI}"
            )
        self._objects_loaded = True

    def _shim(self, sub, *args):
        """A call into the object shim, its result converted with objects."""
        return self._to_obj(self._call_raw(sub, *args))

    def _call_raw(self, name, *args):
        """rk_call, returning the raw RkValue (an int handle). The value is
        valid until the next eval/call — convert it or root it before then."""
        self._alive()
        import ctypes
        argv = (ctypes.c_void_p * len(args))(
            *[a.v if isinstance(a, _Raw) else self._from_py(a) for a in args]
        ) if args else None
        r = self._lib.rk_call(self._ctx, name.encode("utf-8"), argv, len(args))
        if not r:
            msg = self._lib.rk_error(self._ctx)
            self._lib.rk_clear_error(self._ctx)
            raise RakuError(msg.decode("utf-8", "replace") if msg else f"{name} failed")
        return r

    # ---- value conversion ----------------------------------------------------
    # _from_py results, like all unrooted values, live until the next
    # eval/call on this interpreter — build them, pass them, let go.

    def _from_py(self, x):
        c, lib = self._ctx, self._lib
        if isinstance(x, Object):
            if x._interp is not self:
                raise RakuError("this rakulang.Object belongs to another interpreter")
            if not x._h:
                raise RakuError("this rakulang.Object has been released (close())")
            return x._h
        if x is None:
            return lib.rk_any(c)
        if isinstance(x, bool):
            return lib.rk_bool(c, 1 if x else 0)
        if isinstance(x, int):
            if -(2**63) <= x < 2**63:
                return lib.rk_int(c, x)
            return lib.rk_int_s(c, str(x).encode())
        if isinstance(x, float):
            return lib.rk_num(c, x)
        if isinstance(x, str):
            b = x.encode("utf-8")
            return lib.rk_str(c, b, len(b))
        if isinstance(x, (list, tuple)):
            arr = lib.rk_array(c)
            for item in x:
                lib.rk_push(c, arr, self._from_py(item))
            return arr
        if isinstance(x, dict):
            h = lib.rk_hash(c)
            for k, v in x.items():
                kb = str(k).encode("utf-8")
                lib.rk_set(c, h, kb, len(kb), self._from_py(v))
            return h
        raise TypeError(f"cannot pass a {type(x).__name__} to Raku")

    def _str_of(self, v):
        """The value's Str coercion, per the header: the text of a Str, the
        digits of an Int, a gist for anything else."""
        import ctypes
        n = ctypes.c_size_t()
        p = self._lib.rk_str_get(self._ctx, v, ctypes.byref(n))
        return ctypes.string_at(p, n.value).decode("utf-8", "replace") if p else ""

    def _int_of(self, v):
        """An Int, exactly. rk_int_get is an int64 and BigInt::toLL saturates
        there, so INT64_MAX and INT64_MIN may each be a wider Int in disguise;
        the digits settle it, and parse back to the same number when they are
        not. Python integers have no width, so nothing is lost here."""
        n = self._lib.rk_int_get(self._ctx, v)
        if n == _INT64_MAX or n == _INT64_MIN:
            try:
                return int(self._str_of(v))
            except ValueError:      # an allomorph whose text is not decimal
                pass
        return n

    def _to_py(self, v):
        import ctypes
        c, lib = self._ctx, self._lib
        t = lib.rk_type(c, v)
        if t == RK_ANY:
            return None
        if t == RK_BOOL:
            return bool(lib.rk_truthy(c, v))
        if t == RK_INT:
            return self._int_of(v)
        if t in (RK_NUM, RK_RAT):
            return lib.rk_num_get(c, v)
        if t == RK_STR or t == RK_OTHER:  # RK_OTHER stringifies, per the header
            return self._str_of(v)
        if t == RK_ARRAY:
            return [self._to_py(lib.rk_at_pos(c, v, i))
                    for i in range(lib.rk_elems(c, v))]
        if t == RK_HASH:
            out = {}
            for i in range(lib.rk_elems(c, v)):
                n = ctypes.c_size_t()
                kp = lib.rk_key_at(c, v, i, ctypes.byref(n))
                key = ctypes.string_at(kp, n.value).decode("utf-8", "replace")
                out[key] = self._to_py(lib.rk_val_at(c, v, i))
            return out
        raise RakuError(f"unknown RkType {t}")

    def _to_obj(self, v):
        """_to_py for the object shim's transport: every RK_OTHER is a box
        (object_shim.raku, rk-py-out), rooted here as an Object. The walk
        makes no calls, so the unrooted value it reads stays valid."""
        import ctypes
        c, lib = self._ctx, self._lib
        t = lib.rk_type(c, v)
        if t == RK_OTHER:
            return Object(self, self._root(v))
        if t == RK_ARRAY:
            return [self._to_obj(lib.rk_at_pos(c, v, i))
                    for i in range(lib.rk_elems(c, v))]
        if t == RK_HASH:
            out = {}
            for i in range(lib.rk_elems(c, v)):
                n = ctypes.c_size_t()
                kp = lib.rk_key_at(c, v, i, ctypes.byref(n))
                key = ctypes.string_at(kp, n.value).decode("utf-8", "replace")
                out[key] = self._to_obj(lib.rk_val_at(c, v, i))
            return out
        return self._to_py(v)

    # ---- rooting -------------------------------------------------------------

    def _root(self, v):
        return self._lib.rk_root(self._ctx, v)

    def _unroot(self, v):
        if self._rk:
            self._lib.rk_unroot(self._ctx, v)

    def can(self, name):
        """Is there a routine of this name in the mainline scope?"""
        self._alive()
        return self._lib.rk_can(self._ctx, name.encode("utf-8")) != 0

    @property
    def version(self):
        """The engine's version string, e.g. "3.14.0"."""
        return self._lib.rk_version().decode()


def _holds_object(x):
    if isinstance(x, Object):
        return True
    if isinstance(x, (list, tuple)):
        return any(_holds_object(i) for i in x)
    if isinstance(x, dict):
        return any(_holds_object(i) for i in x.values())
    return False


_default = None
_default_lock = threading.Lock()


def interpreter(lib_path=None):
    """The process's default interpreter, created on first use."""
    global _default
    with _default_lock:
        if _default is None:
            _default = Interp(lib_path)
        return _default


class Grammar:
    """A compiled Raku grammar. Get one from from_file or from_source; the
    handle is an id into the shim's cache, so identical source compiles once."""

    def __init__(self, interp, gid, label):
        self._interp = interp
        self._gid = gid
        self._label = label

    @classmethod
    def from_source(cls, source, name=None, actions=None, interp=None):
        """Compile grammar source. `name` is the grammar's name in the source;
        omit it only when the grammar declaration is the source's LAST
        statement. `actions` names an actions class in the same source —
        every parse then runs with a fresh instance of it."""
        if actions and not name:
            raise ValueError("actions= needs name= as well")
        it = interp or interpreter()
        gid = it.call("rk-grammar-compile", source, name or "", actions or "")
        return cls(it, gid, name or "<anonymous>")

    @classmethod
    def from_file(cls, path, name=None, actions=None, interp=None):
        """Compile a grammar from a .raku file — the documented default: the
        grammar keeps its own file, its own syntax highlighting, and its
        actions live beside it."""
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()
        g = cls.from_source(src, name=name, actions=actions, interp=interp)
        g._label = os.path.basename(path) + (f"#{name}" if name else "")
        return g

    def parse(self, text, rule=None, strict=False):
        """Parse text. Returns a Match handle, or None when the parse fails.
        With strict=True a failed parse raises ParseError instead, carrying
        the engine's highwater diagnosis (line, column, rule). The whole
        input must match, as Raku's .parse anchors both ends."""
        raw = self._interp._call_raw("rk-grammar-parse", self._gid, text,
                                     rule or "")
        if self._interp._lib.rk_type(self._interp._ctx, raw) == RK_ANY:
            if strict:
                d = self._interp.call("rk-grammar-diagnosis", text)
                if d:
                    raise ParseError(
                        f"{self._label}: no match — failed at line {d['line']} "
                        f"column {d['col']} while trying <{d['rule']}>",
                        pos=d["pos"], line=d["line"], column=d["col"],
                        rule=d["rule"])
                raise ParseError(f"{self._label}: no match")
            return None
        return Match(self._interp, self._interp._root(raw))

    def __repr__(self):
        return f"<rakulang.Grammar {self._label}>"


class _Node:
    """Shared lazy-access surface of Match (a rooted handle) and _Path (a
    pending path under one). Every terminal operation is ONE rk_call."""

    # subclasses provide _match (the owning Match) and _steps (tuple of path)

    def __getitem__(self, step):
        if not isinstance(step, (int, str)):
            raise TypeError("capture index must be a str (named) or int (positional)")
        return _Path(self._match, self._steps + (step,))

    def _walk(self, op):
        m = self._match
        return m._interp.call("rk-match-walk", _Raw(m._handle),
                              list(self._steps), op)

    def str(self):
        """The matched text at this node. Raises if nothing matched here."""
        return self._walk("str")

    def int(self):
        return self._walk("int")

    def num(self):
        return self._walk("num")

    @property
    def made(self):
        """What the actions class .made here, or None."""
        return self._walk("made")

    def tree(self):
        """Eager conversion of everything below this node — costs ~3x the
        parse itself; prefer the lazy path or same-file actions when you only
        need part of it (the measurement is in GRAMMAR-PLAN.md)."""
        return self._walk("tree")

    def match(self):
        """This node as an independent rooted Match (survives the parent)."""
        m = self._match
        raw = m._interp._call_raw("rk-match-walk", _Raw(m._handle),
                                  list(self._steps), "match")
        if m._interp._lib.rk_type(m._interp._ctx, raw) == RK_ANY:
            return None
        return Match(m._interp, m._interp._root(raw))

    def __bool__(self):
        return bool(self._walk("bool"))

    def __len__(self):
        return self._walk("elems")

    def __iter__(self):
        if self._walk("islist"):
            for i in range(self._walk("elems")):
                yield self[i]
        elif self._walk("bool"):
            yield self  # a bare capture iterates as itself, once

    def __str__(self):
        return self.str()


class Match(_Node):
    """A successful parse, held as a rooted value in the interpreter. Free it
    with close() (or a with-block, or let the GC get to it)."""

    def __init__(self, interp, rooted):
        self._interp = interp
        self._handle = rooted
        self._match = self
        self._steps = ()

    def close(self):
        if self._handle:
            self._interp._unroot(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def __repr__(self):
        return "<rakulang.Match>" if self._handle else "<rakulang.Match closed>"


class _Path(_Node):
    def __init__(self, match, steps):
        self._match = match
        self._steps = steps

    def __repr__(self):
        return f"<rakulang.Match path {list(self._steps)!r}>"


class Object:
    """A Raku value with no Python counterpart (an instance of a class, a
    type object, a lazy Seq, a Pair, an enum value...), held in the
    interpreter. Attribute access calls into Raku:

        p = geo.Point.new(x=3, y=4)   # a type object's method, named arguments
        p.x                            # a public attribute: its value
        p.dist(other)                  # any other method: call it
        p.x = 5                        # an `is rw` attribute

    A Python name with underscores also finds the Raku name with hyphens
    (p.to_json finds to-json); getattr(p, "to-json") spells it exactly.
    Results convert as eval() results do, except that values with no Python
    type come back as Objects instead of strings."""

    __slots__ = ("_interp", "_h", "__weakref__")

    def __init__(self, interp, rooted):
        object.__setattr__(self, "_interp", interp)
        object.__setattr__(self, "_h", rooted)

    def _op(self, op, *arg):
        return self._interp._shim("rk-py-op", self, op, *arg)

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        got = self._interp._shim("rk-py-attr", self, name)
        return _attr_result(self, got, name)

    def __setattr__(self, name, value):
        if name in Object.__slots__:
            object.__setattr__(self, name, value)
        else:
            self._interp._shim("rk-py-set-attr", self, name, value)

    def __call__(self, /, *args, **named):
        return self._interp._shim("rk-py-invoke", self, list(args), named)

    def __getitem__(self, key):
        if isinstance(key, bool) or not isinstance(key, (int, str)):
            raise TypeError("an index is an int (a position) or a str (a key)")
        return self._op("pos" if isinstance(key, int) else "key", key)

    def __iter__(self):
        it = self._op("iter")
        while True:
            got = it._op("pull")
            if got[0] == "end":
                return
            yield got[1]

    def __len__(self):
        return self._op("elems")

    def __bool__(self):
        return self._op("bool")

    def __int__(self):
        return self._op("int")

    def __index__(self):
        return self._op("int")

    def __float__(self):
        return self._op("num")

    def __str__(self):
        return self._op("str")

    def __repr__(self):
        if not self._h or not self._interp._rk:
            return "<rakulang.Object (released)>"
        return f"<rakulang.Object {self._op('repr')}>"

    def __eq__(self, other):
        if isinstance(other, Object) or other is None or isinstance(
                other, (bool, int, float, str, list, tuple, dict)):
            return self._op("eqv", other)
        return NotImplemented

    def __hash__(self):
        return hash(self._op("which"))

    def close(self):
        """Release the value now instead of when Python collects this."""
        if self._h:
            self._interp._unroot(self._h)
            object.__setattr__(self, "_h", None)

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


class Module(Object):
    """A module loaded with Interp.use(). Its attributes are what the module
    exports (subs, classes, constants), then what its package holds (`our`
    subs, nested classes), then, for a module that is a class, the class's
    own methods (Module.new). dir() lists the names."""

    __slots__ = ("_name",)

    def __init__(self, interp, rooted, name):
        super().__init__(interp, rooted)
        object.__setattr__(self, "_name", name)

    def __getattr__(self, name):
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        got = self._interp._shim("rk-py-module-get", self, name)
        return _attr_result(self, got, name)

    def __getitem__(self, name):
        return getattr(self, name)

    def __dir__(self):
        return self._interp._shim("rk-py-module-names", self)

    def __repr__(self):
        return f"<rakulang.Module {self._name or 'MAIN'}>"

    def __str__(self):
        return self._name


class Method:
    """A method looked up on an Object and waiting for its call."""

    __slots__ = ("_obj", "_name")

    def __init__(self, obj, name):
        self._obj = obj
        self._name = name

    def __call__(self, /, *args, **named):
        o = self._obj
        return o._interp._shim("rk-py-call-method", o, self._name,
                               list(args), named)

    def __repr__(self):
        return f"<rakulang.Method {self._name}>"


def _attr_result(obj, got, name):
    kind = got[0]
    if kind == "value":
        return got[1]
    if kind == "method":
        return Method(got[2] if len(got) > 2 else obj, got[1])
    what = obj._name if isinstance(obj, Module) else obj._op("name")
    raise AttributeError(f"Raku {what} has no {name!r}")
