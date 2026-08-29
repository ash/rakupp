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
import threading

from . import _abi
from ._abi import (RK_OK, RK_ANY, RK_BOOL, RK_INT, RK_NUM, RK_RAT, RK_STR,
                   RK_ARRAY, RK_HASH, RK_OTHER)

__all__ = ["Grammar", "Match", "RakuError", "ParseError", "Interp", "interpreter"]

_SHIM_ABI = 1  # must equal rk-shim-abi() in grammar_shim.raku

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
        self._lib = _abi.load(lib_path)
        self._rk = self._lib.rk_new(None)
        if not self._rk:
            raise RakuError(
                "rk_new refused: an interpreter is already live in this process"
            )
        self._ctx = self._lib.rk_ctx(self._rk)
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
        self._alive()
        import ctypes
        out = ctypes.c_void_p()
        status = self._lib.rk_eval(self._rk, source.encode("utf-8"),
                                   ctypes.byref(out))
        if status != RK_OK:
            msg = self._lib.rk_last_error(self._rk)
            raise RakuError(msg.decode("utf-8", "replace") if msg else "rk_eval failed")
        return self._to_py(out.value)

    def call(self, name, *args):
        """Call a Raku routine by name with Python arguments, returning a
        Python value. The eager twin of _call_raw."""
        return self._to_py(self._call_raw(name, *args))

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
