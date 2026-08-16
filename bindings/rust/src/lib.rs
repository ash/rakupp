//! rakulang — Raku grammars for Rust, over librakupp's C ABI.
//!
//! The Rust face of the grammar service (docs/dev/plans/GRAMMAR-PLAN.md):
//! the grammar stays a `.raku` file, parsing happens in an embedded Raku++
//! interpreter, and the Raku shim it drives ships INSIDE the library
//! (`rk_grammar_shim`) — this crate is invocation, results and lifetime.
//!
//! ```no_run
//! use rakulang::Grammar;
//! let g = Grammar::from_file("log.raku", "Log", "LogActions")?;
//! if let Some(m) = g.parse(&text, "")? {
//!     let lines = m.get("line");
//!     for i in 0..lines.len()? {
//!         println!("{}", lines.at(i).get("ip").str()?);   // lazy: one call per leaf
//!     }
//! }   // Match drops here — the rooted engine value unroots itself
//! # Ok::<(), rakulang::Error>(())
//! ```
//!
//! Lifetime is the part Rust gets for free (the reason the plan wanted this
//! host): a `Match` owns a rooted engine value and `Drop` unroots it, and a
//! `Node` borrows its `Match`, so a path cannot outlive the match it walks.
//! One interpreter per process, created on first use; `Grammar`/`Match` are
//! deliberately `!Send` (raw pointers) — one thread talks to the engine.

use std::ffi::{CStr, CString};
use std::fmt;
use std::os::raw::{c_char, c_double, c_int, c_longlong};
use std::sync::OnceLock;

// ---- the C ABI (rakupp.h / rakupp_ext.h), hand-declared ---------------------

#[allow(non_camel_case_types)]
type size_t = usize;
type RkInterp = *mut std::ffi::c_void;
type RkCtx = *mut std::ffi::c_void;
type RkValue = *mut std::ffi::c_void;

const RK_OK: c_int = 0;
const RK_ANY: c_int = 0;
const RK_BOOL: c_int = 1;
const RK_INT: c_int = 2;
const RK_NUM: c_int = 3;
const RK_RAT: c_int = 4;
const RK_ARRAY: c_int = 6;
const RK_HASH: c_int = 7;

extern "C" {
    fn rk_new(cfg: *const std::ffi::c_void) -> RkInterp;
    fn rk_ctx(rk: RkInterp) -> RkCtx;
    fn rk_eval(rk: RkInterp, src: *const c_char, out: *mut RkValue) -> c_int;
    fn rk_last_error(rk: RkInterp) -> *const c_char;
    fn rk_grammar_shim() -> *const c_char;
    fn rk_call(c: RkCtx, name: *const c_char, argv: *const RkValue, argc: size_t) -> RkValue;
    fn rk_error(c: RkCtx) -> *const c_char;
    fn rk_clear_error(c: RkCtx);
    fn rk_root(c: RkCtx, v: RkValue) -> RkValue;
    fn rk_unroot(c: RkCtx, v: RkValue);
    fn rk_type(c: RkCtx, v: RkValue) -> c_int;
    fn rk_truthy(c: RkCtx, v: RkValue) -> c_int;
    fn rk_int_get(c: RkCtx, v: RkValue) -> c_longlong;
    fn rk_num_get(c: RkCtx, v: RkValue) -> c_double;
    fn rk_str_get(c: RkCtx, v: RkValue, len: *mut size_t) -> *const c_char;
    fn rk_elems(c: RkCtx, v: RkValue) -> size_t;
    fn rk_at_pos(c: RkCtx, v: RkValue, i: size_t) -> RkValue;
    fn rk_key_at(c: RkCtx, v: RkValue, i: size_t, keylen: *mut size_t) -> *const c_char;
    fn rk_val_at(c: RkCtx, v: RkValue, i: size_t) -> RkValue;
    fn rk_int(c: RkCtx, v: c_longlong) -> RkValue;
    fn rk_str(c: RkCtx, utf8: *const c_char, len: size_t) -> RkValue;
    fn rk_array(c: RkCtx) -> RkValue;
    fn rk_push(c: RkCtx, array: RkValue, v: RkValue);
    fn rk_any(c: RkCtx) -> RkValue;
    fn rk_bool(c: RkCtx, truthy: c_int) -> RkValue;
    fn rk_num(c: RkCtx, v: c_double) -> RkValue;
    fn rk_hash(c: RkCtx) -> RkValue;
    fn rk_set(c: RkCtx, hash: RkValue, key: *const c_char, keylen: size_t, v: RkValue);
    fn rk_can(c: RkCtx, name: *const c_char) -> c_int;
    fn rk_version() -> *const c_char;
}

// ---- errors -----------------------------------------------------------------

/// A Raku-side failure, or a diagnosed non-match (`Error::Parse`).
#[derive(Debug)]
pub enum Error {
    /// A die from the engine: broken grammar source, a missing capture, …
    Raku(String),
    /// The grammar did not match (from `parse_strict`): the engine's
    /// highwater — 1-based line/col, 0-based char pos, and the rule trying.
    Parse { label: String, pos: i64, line: i64, col: i64, rule: String },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Raku(m) => write!(f, "{}", m),
            Error::Parse { label, line, col, rule, .. } => write!(
                f, "{}: no match — failed at line {} column {} while trying <{}>",
                label, line, col, rule),
        }
    }
}

impl std::error::Error for Error {}

// ---- the session ------------------------------------------------------------

struct Session {
    rk: RkInterp,
    c: RkCtx,
}
// One interpreter per process; the pointers never move and the crate's types
// are !Send, so handing the addresses through OnceLock is sound. `rk` is kept
// beside `c` because rk_eval takes the interpreter, not the context.
struct SendPtr(RkInterp, RkCtx);
unsafe impl Send for SendPtr {}
unsafe impl Sync for SendPtr {}

static SESSION: OnceLock<Result<SendPtr, String>> = OnceLock::new();

fn session() -> Result<Session, Error> {
    let r = SESSION.get_or_init(|| unsafe {
        let rk = rk_new(std::ptr::null());
        if rk.is_null() {
            return Err("rk_new refused: an interpreter is already live in this process".into());
        }
        let c = rk_ctx(rk);
        if rk_eval(rk, rk_grammar_shim(), std::ptr::null_mut()) != RK_OK {
            let e = rk_last_error(rk);
            let msg = if e.is_null() { "?".into() } else { CStr::from_ptr(e).to_string_lossy().into_owned() };
            return Err(format!("grammar shim failed to load: {}", msg));
        }
        Ok(SendPtr(rk, c))
    });
    match r {
        Ok(p) => Ok(Session { rk: p.0, c: p.1 }),
        Err(m) => Err(Error::Raku(m.clone())),
    }
}

impl Session {
    fn str(&self, s: &str) -> RkValue {
        unsafe { rk_str(self.c, s.as_ptr() as *const c_char, s.len()) }
    }

    fn call(&self, name: &str, args: &[RkValue]) -> Result<RkValue, Error> {
        let cname = CString::new(name).expect("sub name");
        unsafe {
            let r = rk_call(self.c, cname.as_ptr(),
                            if args.is_empty() { std::ptr::null() } else { args.as_ptr() },
                            args.len());
            if r.is_null() {
                let e = rk_error(self.c);
                let msg = if e.is_null() { format!("{} failed", name) }
                          else { CStr::from_ptr(e).to_string_lossy().into_owned() };
                rk_clear_error(self.c);
                return Err(Error::Raku(msg));
            }
            Ok(r)
        }
    }

    fn read_str(&self, v: RkValue) -> String {
        unsafe {
            let mut n: size_t = 0;
            let p = rk_str_get(self.c, v, &mut n);
            if p.is_null() || n == 0 { return String::new(); }
            String::from_utf8_lossy(std::slice::from_raw_parts(p as *const u8, n)).into_owned()
        }
    }

    /// Rust -> engine. The result is UNROOTED: valid until the next eval or
    /// call, which is long enough to pass it as an argument and no longer.
    fn value_of(&self, t: &Tree) -> RkValue {
        unsafe {
            match t {
                Tree::Null => rk_any(self.c),
                Tree::Bool(b) => rk_bool(self.c, if *b { 1 } else { 0 }),
                Tree::Int(i) => rk_int(self.c, *i),
                Tree::Num(n) => rk_num(self.c, *n),
                Tree::Str(s) => self.str(s),
                Tree::List(items) => {
                    let a = rk_array(self.c);
                    for item in items {
                        rk_push(self.c, a, self.value_of(item));
                    }
                    a
                }
                Tree::Map(m) => {
                    let h = rk_hash(self.c);
                    for (k, v) in m {
                        rk_set(self.c, h, k.as_ptr() as *const c_char, k.len(),
                               self.value_of(v));
                    }
                    h
                }
            }
        }
    }

    fn tree_of(&self, v: RkValue) -> Tree {
        unsafe {
            match rk_type(self.c, v) {
                RK_ANY => Tree::Null,
                RK_BOOL => Tree::Bool(rk_truthy(self.c, v) != 0),
                RK_INT => Tree::Int(rk_int_get(self.c, v)),
                RK_NUM | RK_RAT => Tree::Num(rk_num_get(self.c, v)),
                RK_ARRAY => {
                    let n = rk_elems(self.c, v);
                    Tree::List((0..n).map(|i| self.tree_of(rk_at_pos(self.c, v, i))).collect())
                }
                RK_HASH => {
                    let n = rk_elems(self.c, v);
                    let mut out = std::collections::BTreeMap::new();
                    for i in 0..n {
                        let mut kl: size_t = 0;
                        let kp = rk_key_at(self.c, v, i, &mut kl);
                        let key = String::from_utf8_lossy(
                            std::slice::from_raw_parts(kp as *const u8, kl)).into_owned();
                        out.insert(key, self.tree_of(rk_val_at(self.c, v, i)));
                    }
                    Tree::Map(out)
                }
                _ => Tree::Str(self.read_str(v)), // RK_STR / RK_OTHER stringify
            }
        }
    }
}

/// The eager conversion result. A node with no captures converts to its
/// text; named captures become map keys; quantified captures are lists.
/// The map is a BTreeMap, so iteration is key-sorted and deterministic.
#[derive(Debug, Clone, PartialEq)]
pub enum Tree {
    Null,
    Bool(bool),
    Int(i64),
    Num(f64),
    Str(String),
    List(Vec<Tree>),
    Map(std::collections::BTreeMap<String, Tree>),
}

// ---- running Raku ----------------------------------------------------------
// `Tree` is both the argument type and the result type, so what you send and
// what you get back read alike. The From impls keep the call sites short:
// `call("area", &[3.into(), 4.into()])`.

impl From<i64> for Tree {
    fn from(v: i64) -> Tree { Tree::Int(v) }
}
impl From<i32> for Tree {
    fn from(v: i32) -> Tree { Tree::Int(v as i64) }
}
impl From<f64> for Tree {
    fn from(v: f64) -> Tree { Tree::Num(v) }
}
impl From<bool> for Tree {
    fn from(v: bool) -> Tree { Tree::Bool(v) }
}
impl From<&str> for Tree {
    fn from(v: &str) -> Tree { Tree::Str(v.to_string()) }
}
impl From<String> for Tree {
    fn from(v: String) -> Tree { Tree::Str(v) }
}
impl From<Vec<Tree>> for Tree {
    fn from(v: Vec<Tree>) -> Tree { Tree::List(v) }
}
impl From<std::collections::BTreeMap<String, Tree>> for Tree {
    fn from(v: std::collections::BTreeMap<String, Tree>) -> Tree { Tree::Map(v) }
}

/// Evaluate Raku source in the interpreter's mainline scope and return the
/// last statement's value. State persists across calls, exactly as in the
/// REPL: eval a `sub` here and [`call`] finds it afterwards.
///
/// ```no_run
/// rakulang::eval("sub area($w, $h) { $w * $h }")?;
/// # Ok::<(), rakulang::Error>(())
/// ```
pub fn eval(source: &str) -> Result<Tree, Error> {
    let s = session()?;
    let src = CString::new(source).map_err(|_| Error::Raku("source contains a NUL".into()))?;
    unsafe {
        let mut out: RkValue = std::ptr::null_mut();
        if rk_eval(s.rk, src.as_ptr(), &mut out) != RK_OK {
            let e = rk_last_error(s.rk);
            let msg = if e.is_null() { "rk_eval failed".to_string() }
                      else { CStr::from_ptr(e).to_string_lossy().into_owned() };
            return Err(Error::Raku(msg));
        }
        Ok(s.tree_of(out))
    }
}

/// Call a Raku routine by name with Rust arguments:
/// `call("area", &[3.into(), 4.into()])`. The routine must be visible in the
/// mainline scope — declared by an earlier [`eval`], or by a file you
/// evaluated. A die inside it comes back as [`Error::Raku`].
pub fn call(name: &str, args: &[Tree]) -> Result<Tree, Error> {
    let s = session()?;
    let argv: Vec<RkValue> = args.iter().map(|a| s.value_of(a)).collect();
    Ok(s.tree_of(s.call(name, &argv)?))
}

/// Does the mainline scope have a routine of this name?
pub fn can(name: &str) -> bool {
    let Ok(s) = session() else { return false };
    let Ok(cname) = CString::new(name) else { return false };
    unsafe { rk_can(s.c, cname.as_ptr()) != 0 }
}

/// The engine's version string, e.g. `"3.14.0"`.
pub fn version() -> String {
    unsafe {
        let v = rk_version();
        if v.is_null() { String::new() } else { CStr::from_ptr(v).to_string_lossy().into_owned() }
    }
}

// ---- grammars, matches, nodes ----------------------------------------------

/// A compiled Raku grammar. Identical source compiles once; named compiles
/// are isolated, so a recompile never rebinds an earlier Grammar's body.
pub struct Grammar {
    id: i64,
    label: String,
    _not_send: std::marker::PhantomData<*const ()>,
}

impl Grammar {
    /// `name` is the grammar's name in the source (empty only when the
    /// grammar declaration is the source's LAST statement); `actions` names
    /// an actions class in the same source (or "").
    pub fn from_source(source: &str, name: &str, actions: &str) -> Result<Grammar, Error> {
        let s = session()?;
        let id = s.call("rk-grammar-compile", &[s.str(source), s.str(name), s.str(actions)])?;
        Ok(Grammar {
            id: unsafe { rk_int_get(s.c, id) },
            label: if name.is_empty() { "<anonymous>".into() } else { name.into() },
            _not_send: std::marker::PhantomData,
        })
    }

    pub fn from_file(path: &str, name: &str, actions: &str) -> Result<Grammar, Error> {
        let src = std::fs::read_to_string(path).map_err(|e| Error::Raku(format!("{}: {}", path, e)))?;
        let mut g = Self::from_source(&src, name, actions)?;
        g.label = path.into();
        Ok(g)
    }

    /// `Ok(None)` when the input does not match (rule "" = the default TOP);
    /// the whole input must match.
    pub fn parse(&self, text: &str, rule: &str) -> Result<Option<Match>, Error> {
        let s = session()?;
        let raw = s.call("rk-grammar-parse",
                         &[unsafe { rk_int(s.c, self.id) }, s.str(text), s.str(rule)])?;
        if unsafe { rk_type(s.c, raw) } == RK_ANY {
            return Ok(None);
        }
        Ok(Some(Match {
            h: unsafe { rk_root(s.c, raw) },
            _not_send: std::marker::PhantomData,
        }))
    }

    /// `parse`, but a non-match is a diagnosed `Error::Parse` (line, column,
    /// rule — the engine's highwater).
    pub fn parse_strict(&self, text: &str, rule: &str) -> Result<Match, Error> {
        if let Some(m) = self.parse(text, rule)? {
            return Ok(m);
        }
        let s = session()?;
        let d = s.call("rk-grammar-diagnosis", &[s.str(text)])?;
        if unsafe { rk_type(s.c, d) } == RK_HASH {
            if let Tree::Map(m) = s.tree_of(d) {
                let get_i = |k: &str| match m.get(k) { Some(Tree::Int(i)) => *i, _ => -1 };
                let rule = match m.get("rule") { Some(Tree::Str(r)) => r.clone(), _ => String::new() };
                return Err(Error::Parse {
                    label: self.label.clone(),
                    pos: get_i("pos"), line: get_i("line"), col: get_i("col"), rule,
                });
            }
        }
        Err(Error::Raku(format!("{}: no match", self.label)))
    }
}

/// A successful parse: owns a rooted engine value, unrooted on `Drop`.
pub struct Match {
    h: RkValue,
    _not_send: std::marker::PhantomData<*const ()>,
}

impl Drop for Match {
    fn drop(&mut self) {
        if let Ok(s) = session() {
            unsafe { rk_unroot(s.c, self.h) };
        }
    }
}

impl Match {
    pub fn get(&self, key: &str) -> Node<'_> {
        Node { m: self, steps: vec![Step::Key(key.into())] }
    }
    pub fn at(&self, i: usize) -> Node<'_> {
        Node { m: self, steps: vec![Step::Idx(i)] }
    }
    pub fn tree(&self) -> Result<Tree, Error> {
        Node { m: self, steps: vec![] }.tree()
    }
    pub fn made(&self) -> Result<Tree, Error> {
        Node { m: self, steps: vec![] }.made()
    }
}

#[derive(Clone)]
enum Step {
    Key(String),
    Idx(usize),
}

/// A lazy path under a Match — indexing accumulates, terminals cross the
/// boundary once. Borrows the Match: a path cannot outlive its parse.
pub struct Node<'m> {
    m: &'m Match,
    steps: Vec<Step>,
}

impl<'m> Node<'m> {
    pub fn get(&self, key: &str) -> Node<'m> {
        let mut steps = self.steps.clone();
        steps.push(Step::Key(key.into()));
        Node { m: self.m, steps }
    }
    pub fn at(&self, i: usize) -> Node<'m> {
        let mut steps = self.steps.clone();
        steps.push(Step::Idx(i));
        Node { m: self.m, steps }
    }

    fn walk(&self, op: &str) -> Result<RkValue, Error> {
        let s = session()?;
        unsafe {
            let path = rk_array(s.c);
            for st in &self.steps {
                match st {
                    Step::Idx(i) => rk_push(s.c, path, rk_int(s.c, *i as c_longlong)),
                    Step::Key(k) => rk_push(s.c, path, s.str(k)),
                }
            }
            s.call("rk-match-walk", &[self.m.h, path, s.str(op)])
        }
    }

    /// The matched text here; `Err` if nothing matched (probe with `truthy`).
    pub fn str(&self) -> Result<String, Error> {
        Ok(session()?.read_str(self.walk("str")?))
    }
    pub fn int(&self) -> Result<i64, Error> {
        let s = session()?;
        Ok(unsafe { rk_int_get(s.c, self.walk("int")?) })
    }
    pub fn num(&self) -> Result<f64, Error> {
        let s = session()?;
        Ok(unsafe { rk_num_get(s.c, self.walk("num")?) })
    }
    pub fn truthy(&self) -> Result<bool, Error> {
        let s = session()?;
        Ok(unsafe { rk_truthy(s.c, self.walk("bool")?) } != 0)
    }
    pub fn is_list(&self) -> Result<bool, Error> {
        let s = session()?;
        Ok(unsafe { rk_truthy(s.c, self.walk("islist")?) } != 0)
    }
    pub fn len(&self) -> Result<usize, Error> {
        let s = session()?;
        Ok(unsafe { rk_int_get(s.c, self.walk("elems")?) } as usize)
    }
    pub fn is_empty(&self) -> Result<bool, Error> {
        Ok(self.len()? == 0)
    }
    /// Eager conversion of everything below this node (~1.4x the parse).
    pub fn tree(&self) -> Result<Tree, Error> {
        Ok(session()?.tree_of(self.walk("tree")?))
    }
    /// What the actions class computed here (`Tree::Null` when none).
    pub fn made(&self) -> Result<Tree, Error> {
        Ok(session()?.tree_of(self.walk("made")?))
    }
}
