#pragma once
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>


// A decided-once cache slot on a node SHARED between threads: under
// RAKUPP_PARALLEL any thread may (re)compute and store the same idempotent
// value concurrently — a plain field makes that a data race (ThreadSanitizer's
// remaining reports after the thread_local batch were exactly these). Relaxed
// atomics make it defined at plain-load/plain-store cost on arm64/x86-64.
// Copyable, so nodes keep their value-copy semantics.
template <typename T>
struct DecidedOnce {
    std::atomic<T> v;
    DecidedOnce(T init = T{}) : v(init) {}
    DecidedOnce(const DecidedOnce& o) : v(o.v.load(std::memory_order_relaxed)) {}
    DecidedOnce& operator=(const DecidedOnce& o) {
        v.store(o.v.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }
    operator T() const { return v.load(std::memory_order_relaxed); }
    DecidedOnce& operator=(T x) { v.store(x, std::memory_order_relaxed); return *this; }
};

namespace rakupp {

enum class NK {
    // expressions
    IntLit, NumLit, StrLit, InterpStr, BoolLit, VarExpr, ListExpr,
    Assign, Binary, Unary, Call, MethodCall, Index, Ternary, Range,
    Pair, BlockExpr, ArrayLit, HashLit, NameTerm, RegexLit, SubstLit, ChainExpr, SymbolicRef, AllomorphLit, NqpOp,
    // statements
    ExprStmt, VarDecl, SubDecl, IfStmt, WhileStmt, ForStmt, LoopStmt,
    Block, ReturnStmt, LastStmt, NextStmt, RedoStmt, UseStmt, EmptyStmt,
    GivenStmt, WhenStmt, RepeatStmt, Whatever, ClassDecl, SelfTerm, EnumDecl, NamedRegexDecl,
    SubsetDecl,
};

struct Node {
    NK kind;
    int line = 0;
    explicit Node(NK k) : kind(k) {}
    virtual ~Node() = default;
};
using NodePtr = std::unique_ptr<Node>;

struct Expr : Node { using Node::Node; };
using ExprPtr = std::unique_ptr<Expr>;

struct Stmt : Node { using Node::Node; std::string label; };
using StmtPtr = std::unique_ptr<Stmt>;

// ---- Expressions ----
struct IntLit : Expr { long long v; std::string big; std::string raw; /* source spelling (0xFF) for sink warnings */ explicit IntLit(long long x): Expr(NK::IntLit), v(x){} };
struct NumLit : Expr {
    double v; bool imaginary = false;
    std::string raw; // source spelling (6.02e23, ∞) for sink warnings
    bool isRat = false; long long ratNum = 0, ratDen = 1; // decimal literal `3.14` is a Rat (num/den)
    std::string bigNum, bigDen; // decimal strings when the exact Rat overflows long long
    // interpreter cache of the built Rat's (immutable) BigInt parts — literals in
    // hot loops would otherwise re-allocate + re-reduce on every evaluation.
    // (Benign same-value race under RAKUPP_PARALLEL, like Binary::simpleOp.)
    mutable std::shared_ptr<void> cacheN, cacheD;
    explicit NumLit(double x): Expr(NK::NumLit), v(x){}
};
struct StrLit : Expr { std::string v; bool nfcDone = false; /* NFC-normalized in place on first eval */ explicit StrLit(std::string s): Expr(NK::StrLit), v(std::move(s)){} };
// A numeric word in a `<…>` list is an allomorph: the numeric value of `num`,
// tagged so it is ALSO the string `str` (`<42>` is IntStr, `<1/3>` RatStr, `<1e5>` NumStr).
struct AllomorphLit : Expr { ExprPtr num; std::string str; AllomorphLit(): Expr(NK::AllomorphLit){} };
struct RegexLit : Expr { std::string pattern; bool isRx = false; /* rx// : a Regex object, never an implicit match */ std::string declKind; /* "regex"/"token"/"rule" for an anonymous `regex {…}` term: a first-class Regex closing over its scope */ explicit RegexLit(std::string p): Expr(NK::RegexLit), pattern(std::move(p)){} };
struct ChainExpr : Expr { std::vector<ExprPtr> operands; std::vector<std::string> ops; ChainExpr(): Expr(NK::ChainExpr){} };
struct SubstLit : Expr { std::string pattern, repl; bool nonMut=false; SubstLit(std::string p, std::string r, bool nm=false): Expr(NK::SubstLit), pattern(std::move(p)), repl(std::move(r)), nonMut(nm){} };
struct BoolLit : Expr { bool v; explicit BoolLit(bool b): Expr(NK::BoolLit), v(b){} };

// interpolated string: concatenation of parts (each evaluated and stringified)
struct InterpStr : Expr {
    std::vector<ExprPtr> parts;
    InterpStr(): Expr(NK::InterpStr) {}
};

struct VarExpr : Expr {
    std::string name; // includes sigil
    bool declare = false;        // `my $x` style declaration
    std::string declScope;       // my / our / state / constant
    std::string declType;        // optional type constraint (ignored at runtime for now)
    std::string declCoerce;      // coercion-type target: `my Int(Str) $x` coerces assigned values to Int
    ExprPtr declDefault;         // `is default(EXPR)` — the container's reset/initial value
    bool declDynamic = false;    // `my $x is dynamic` — visible to callees, and .dynamic says so
    bool declExport = false;     // `our %x is export` — importers see the BARE name
    bool pkgSymbol = false;      // `Foo::<bar>` — a package symbol-table slot; assigning autovivifies it
    std::string containerIs;     // `my %h is Set` — the container type trait (Set/Bag/Mix…)
    std::string containerOf;     // `my %h is Bag[Int]` — the container's key-type parameter
    ExprPtr declShape;           // shaped array `my @a[3]` / `my @a[2;2]`: the dimension list
    bool namedBind = false;      // `my (:@a, :@b) := %h` — binds the RHS hash's value by bare name
    bool processScoped = false;  // written `PROCESS::<$x>` / `$PROCESS::x` — assignment
                                 // installs into the PROCESS scope, not the current one
    explicit VarExpr(std::string n): Expr(NK::VarExpr), name(std::move(n)) {}
};

// bareword used as a term (type name, enum, sub w/o parens handled separately)
struct NameTerm : Expr {
    std::string name;
    std::string ofType; // type parameters for `Array[Int]` / `Hash[Int,Str]` (comma-joined)
    int defConstraint = 0; // type smiley: 1 = `:D` (defined), 2 = `:U` (type object)
    explicit NameTerm(std::string n): Expr(NK::NameTerm), name(std::move(n)) {}
};

struct ListExpr : Expr {
    std::vector<ExprPtr> items;
    bool parenned = false; // came from `( … )` → a distinct nested list, not a comma-chain to merge into
    bool semicolon = false; // `( a; b )` semicolon-list: each item is one segment's value (multidim)
    ListExpr(): Expr(NK::ListExpr) {}
};

// symbolic reference: `::($name)` — look up a symbol at runtime by its (string) name.
// `pkg` holds an optional package qualifier: `Foo::($bar)` → pkg="Foo".
struct SymbolicRef : Expr {
    ExprPtr nameExpr;
    std::vector<ExprPtr> segs; // trailing `::seg` path parts (literal StrLit or dynamic `::(…)`)
    std::string pkg;      // optional package qualifier `Foo::($bar)` → "Foo"
    std::string sigil;    // sigil form `$::(…)`/`@::(…)`/`%::(…)`/`&::(…)`; empty = bare `::(…)`
    SymbolicRef(): Expr(NK::SymbolicRef) {}
};

struct ArrayLit : Expr { // [ ... ]  or a word-list < ... > / qw
    std::vector<ExprPtr> items;
    bool isList = false; // word-lists are flattening Lists; bracket [..] literals are not
    bool fromCommaList = false; // [a, b] / [x,]: members are ITEMS — a List member stays one element
    ArrayLit(): Expr(NK::ArrayLit) {}
};

struct HashLit : Expr { // { key => val, ... } hash constructor
    std::vector<ExprPtr> items;
    HashLit(): Expr(NK::HashLit) {}
};

struct Assign : Expr {
    ExprPtr target;
    std::string op; // "=", "+=", ":=" ...
    ExprPtr value;
    Assign(): Expr(NK::Assign) {}
};

struct Binary : Expr {
    std::string op;
    ExprPtr lhs, rhs;
    // eval-dispatch cache: -1 unknown, 0 needs a special-cased handler, 1 is a
    // plain operator that goes straight to eval-both-operands + applyArith.
    // (Computed once; a benign same-value race under RAKUPP_PARALLEL.)
    mutable DecidedOnce<signed char> simpleOp{-1};
    // Fast-path SHAPE, decided once from the syntax and cached here: -1 not yet
    // looked at, 0 none, 1 `$var OP literal`, 2 `literal OP $var`,
    // 3 `$var OP $var`. litVal holds the literal's Value for shapes 1 and 2.
    //
    // Only the SHAPE is cached — never the variable, never its value. Both are
    // looked up, and type-checked, on every evaluation. A literal cannot change,
    // so building it once is safe.
    //
    // litVal is a raw pointer that is never freed: the node outlives every
    // evaluation, so there is nothing to reclaim before exit, and unlike a
    // re-assigned shared_ptr it cannot be yanked out from under a reader under
    // RAKUPP_PARALLEL (a racing double-build leaks one Value; it cannot dangle).
    // void* keeps Value out of Ast.h, the dodge the Rat literal cache uses.
    mutable DecidedOnce<signed char> fastShape{-1};
    mutable DecidedOnce<const void*> litVal{nullptr};
    Binary(): Expr(NK::Binary) {}
};

struct Unary : Expr {
    std::string op;
    bool postfix = false;
    ExprPtr operand;
    Unary(): Expr(NK::Unary) {}
};

struct Call : Expr { // sub call by name: foo(args)  or  foo args
    std::string name;
    ExprPtr callee;     // when invoking a code expression: $code(...)
    std::vector<ExprPtr> args;
    Call(): Expr(NK::Call) {}
};

struct MethodCall : Expr {
    ExprPtr inv;
    std::string method;
    std::string methodQual; // `$obj.Class::method` — dispatch to Class's method, past any override
    ExprPtr methodExpr; // indirect call $obj."$name"() — method name computed at runtime
    std::vector<ExprPtr> args;
    bool maybe = false; // .?
    bool bang = false;  // $obj!priv — private-method call syntax
    bool mutate = false; // .= mutating call
    bool hyper = false;  // >>.method  (apply to each element)
    bool meta = false;   // .^method  (metamodel call, e.g. .^name)
    MethodCall(): Expr(NK::MethodCall) {}
};

struct Index : Expr { // base[idx] or base{key}
    ExprPtr base;
    ExprPtr index;
    bool isHash = false;
    bool multiDim = false; // @a[X;Y]: index is a ListExpr of dims, sliced level-by-level
    bool semicolonSub = false; // %h{a;b;c}: a `{; }` multidim brace subscript (parsed as nested Index)
    std::string adverb; // :exists / :delete / :k / :v / :kv / :p  (may start with '!')
    // Fast-path shape for `@arr[$i]` / `@arr[0]`, decided once from the syntax:
    // -1 not yet looked at, 0 none, 1 plain lexical base with a plain lexical
    // index, 2 plain lexical base with an integer literal index. Same rule as
    // Binary::fastShape — the SHAPE is cached, never the container or the index
    // value, both of which are read and checked on every evaluation.
    mutable DecidedOnce<signed char> fastShape{-1};
    mutable DecidedOnce<long long> litIdx{0}; // the constant subscript, for shape 2 (same value from every thread)
    Index(): Expr(NK::Index) {}
};

struct Ternary : Expr {
    ExprPtr cond, then, els;
    Ternary(): Expr(NK::Ternary) {}
};

// nqp:: compatibility subset (see docs/dev/MODULE-FINDINGS.md #4b). These nodes
// exist ONLY when the parser saw `use nqp` and met an nqp::op call — programs
// that never use nqp build no such node and pay nothing anywhere.
enum class NqpOpc : uint16_t {
    // lazy forms (args evaluated by the op itself)
    Stmts, While, Until, IfNull,
    // int ops (int64 semantics, no bignum promotion)
    IseqI, IsneI, IsltI, IsleI, IsgeI, IsgtI, AddI, SubI, MulI, BitandI,
    BitorI, BitxorI, BitshiftlI, BitshiftrI,
    // num ops
    IseqN, IsneN, AtposN,
    // buffer read/write (flag = size|endian, MoarVM encoding)
    ReadUInt, ReadInt, ReadNum, WriteUInt, WriteInt, WriteNum,
    Slice, Decode, SetElems, BindposN, AddBigI,
    // identity/box helpers
    Decont, P6BoxS,
    // string ops
    Ordat, Eqat, Substr, Chars, Concat, Join, Index, Chr,
    StrFromCodes, StrToCodes, FindNotCClass, IsCClass,
    // list ops
    List, ListI, ListS, Elems, Atpos, AtposI, Bindpos, BindposI,
    Push, PushI, PushS, PopS, ShiftI, Splice,
    // hash ops
    Hash, Bindkey,
    // object/meta ops
    Create, Istype, Getattr, Bindattr, P6BindAttrInvRes, P6ScalarWithValue,
    Null, IsNanOrInf,
    // appended (AST-cache safe): the AttrX::Mooish surface
    What, IsList, IsCont, IsTrue, IsConcrete, CloneOp, Shift, LockOp, UnlockOp,
};
struct NqpOp : Expr {
    NqpOpc op;
    std::vector<ExprPtr> args;
    explicit NqpOp(NqpOpc o): Expr(NK::NqpOp), op(o) {}
};

struct RangeExpr : Expr {
    ExprPtr from, to;
    bool exFrom = false, exTo = false;
    RangeExpr(): Expr(NK::Range) {}
};

struct PairExpr : Expr {
    std::string key;     // used when keyExpr is null (bareword / literal key)
    bool colonForm = false; // written as :key(value) — kept for diagnostics spelling
    bool quotedKey = false; // 'a' => 1 — quoted-key pairs are POSITIONAL args, never named
    ExprPtr keyExpr;     // dynamic key, e.g. $var => ... or (expr) => ...
    ExprPtr value;
    PairExpr(): Expr(NK::Pair) {}
};

struct Param {
    std::string name;   // includes sigil; empty for anon
    char sigil = '$';
    std::string type;   // type constraint name (for multi-dispatch); "" = unconstrained
    ExprPtr whereExpr;  // `where` constraint (checked in multi-dispatch)
    ExprPtr litVal;     // literal parameter, e.g. MAIN('population') — arg must equal this
    ExprPtr defaultVal; // may be null
    std::string defaultRaku; // pre-rendered default, for a .assuming residual signature
                             // (priming a named param turns it into `:$a = <primed value>`)
    bool hadWhere = false;   // residual-signature copies can't own whereExpr; remember it was there
    bool typeCapture = false; // `::T $x` — `type` names a type variable, not a real type
    std::string namedKey; // external name for `:name($var)` (else = var name)
    bool aliasBoth = false; // `:name(:$var)` — BOTH the alias and the var name bind
    std::vector<std::string> aliasKeys; // nested aliases `:x(:y(:z($a)))` — every layer's key answers
    std::string pod;      // `#= description` trailing declarator pod (drives $*USAGE)
    char slurpyKind = 0;  // 'f'=*@ (flatten), 'n'=**@ (no-flatten), '1'=+@ (single-arg rule)
    bool named = false;
    bool slurpy = false;
    bool optional = false;
    bool required = false; // explicit `!` on a named param
    bool invocant = false; // declared before ':' in signature
    int defConstraint = 0; // type smiley: 0=none, 1=:D (defined), 2=:U (undefined)
    bool coerce = false;   // coercion type `Int(Str)` / `Int()`: the bound value is coerced to `type`
    bool isRw = false;     // `is rw` — writes copy back to the caller's lvalue
    bool isCopy = false;   // `is copy` — a fresh mutable copy (vs a readonly plain param)
    bool isRaw = false;    // `is raw` — bound without a container (introspected by .raw)
    // destructuring sub-signature: `[$a,$b]` / `($a,$b)` / `|c($x)` — the inner
    // params the argument is unpacked into (null when not a destructuring param).
    std::shared_ptr<std::vector<Param>> subSig;
};

struct BlockExpr : Expr {
    std::vector<Param> params; // for pointy blocks / placeholder
    std::vector<StmtPtr> body;
    bool isSub = false;        // anonymous `sub {…}` / `method {…}` term — a Sub, not a Block
    bool isMethodTerm = false; // …and `method {…}` in particular takes an invocant
    bool isPointy = false;     // `-> {…}` / `<-> {…}` — a WRITTEN signature, even an empty one
    std::string retType;       // `--> T` in the signature of a pointy block / anon routine
    BlockExpr(): Expr(NK::BlockExpr) {}
};

// ---- Statements ----
struct ExprStmt : Stmt {
    ExprPtr e;
    ExprStmt(): Stmt(NK::ExprStmt) {}
};

struct NamedRegexDecl : Stmt { // lexical `my regex/token/rule NAME { … }`, callable as <NAME>
    std::string name, pattern, kind;
    NamedRegexDecl(): Stmt(NK::NamedRegexDecl) {}
};

struct VarDecl : Stmt {
    std::string scope; // my, our, has, state
    std::vector<std::string> names; // for `my ($a,$b)`
    std::string op = "=";
    ExprPtr init; // may be null
    VarDecl(): Stmt(NK::VarDecl) {}
};

struct SubTraitSpec { std::string name; ExprPtr arg; }; // `is traced` / `is role('admin')`

struct SubDecl : Stmt {
    std::string name; // empty for anon
    std::vector<Param> params;
    std::vector<std::vector<Param>> altParams; // extra `(sig1) | (sig2)` signatures, share the body
    std::vector<StmtPtr> body;
    std::vector<SubTraitSpec> traits; // non-built-in `is` traits, dispatched to user trait_mod:<is> multis
    ExprPtr retLiteral; // `--> 1` literal return: the body yields this value
    bool retLiteralPresent = false; // stays true after retLiteral is moved into the body
    bool isMulti = false;
    bool isProto = false; // `proto` — defines the dispatch group; not a candidate itself
    bool hadSig = false;  // explicit `(...)` signature (even empty) — placeholders then illegal
    bool isMethod = false;
    bool isSubmethod = false;
    bool isPrivate = false; // `method !name` — private method, called only via self!name
    std::vector<ExprPtr> immediateArgs; // `sub f($n) {…}(1)` — declare, then call at once
    bool immediateCall = false;
    bool isExport = false; // `is export` — visible to importers of the enclosing module
    bool isOur = false;    // `our sub` — also installed in the package/global scope (visible to sibling blocks)
    std::string retType;   // `of Num` / `returns Int` / `--> T` return type (for .returns/.of)
    std::string pod;       // `#|` leading declarator pod (.WHY)
    bool isNative = false;    // `is native` — a C FFI call
    std::string nativeLib;    // `is native('lib')` — "" ⇒ the default namespace (libc etc.)
    std::string nativeLibSub; // `is native(&sub)` — a sub name called at runtime for the lib path
    ExprPtr nativeLibExpr;    // `is native(EXPR)` — any other argument (a constant,
                              // %?RESOURCES<libraries/…>), evaluated when the declaration
                              // executes (module scope = Rakudo's trait-application time)
    std::string nativeSym;    // `is symbol('name')` — "" ⇒ the sub's own name
    SubDecl(): Stmt(NK::SubDecl) {}
};

struct AttrDecl {
    std::string name;   // bare name, no sigil/twigil
    char sigil = '$';
    std::string containerIs; // `has %.a is Set` — container type trait
    bool pub = true;    // has $.x (public accessor) vs has $!x (private)
    bool rw = false;    // `is rw` — public accessor is writable
    bool required = false; // `is required` — .new must be given a value for it
    bool built = false;    // `is built` — a PRIVATE attr .new may still set by name
    std::string requiredWhy; // `is required("reason")` — carried into the exception message
    std::string type;   // declared type name (`has Int $.x`), "" = none (Mu)
    bool coerce = false; // coercion-type attribute: `has IO::Path() $.filename`
    std::vector<std::string> handles; // `handles <m1 m2>` — delegate these methods to the attr
    int defConstraint = 0; // type smiley: 0=none, 1=:D (defined), 2=:U (undefined)
    bool objKeyed = false; // `has %!h{Mu:U}` — an object-keyed hash: TYPE-OBJECT
                           // subscript keys stay distinct ("(Name)") instead of
                           // stringifying to "" like a plain hash's
    // USER traits (`is json-name('licenseId')`, `is unmarshalled-by(-> $d {…})`,
    // bare `is json-skip`): name + argument expression (null for bare). The
    // built-in traits above never appear here; these are what the JSON::Name /
    // JSON::Unmarshal trait_mods would store on the Attribute meta-object.
    std::vector<std::pair<std::string, ExprPtr>> userTraits;
    ExprPtr def;        // optional default
};

struct GrammarRuleDecl { std::string name, pattern, kind; std::vector<std::string> params; };
struct ClassDecl : Stmt {
    std::string name;
    std::string parent; // first `is Parent` / `does Role`
    std::vector<std::string> extraParents; // additional `is Parent` (multiple inheritance)
    std::vector<std::string> roles; // additional `does Role` (methods composed in)
    std::vector<AttrDecl> attrs;
    std::vector<std::unique_ptr<SubDecl>> methods;
    std::vector<GrammarRuleDecl> rules; // grammar token/rule/regex
    bool isRole = false;
    bool parentIsDoes = false; // the first inheritance target came from `does` (composition), not `is`
    bool isGrammar = false;
    bool isAugment = false;        // augment class Foo { … } — merge methods into an existing type
    bool isStubDecl = false;
    std::string pod; // `#|` leading declarator pod (.WHY)       // body was a bare `...` — a forward declaration, redeclarable
    bool parameterized = false;    // role R[T] — parameterizations coexist by name
    std::string ver, auth, api;
    // `module Zef:ver($?DISTRIBUTION.meta<version> // '*')` — the adverb may be an
    // EXPRESSION, evaluated when the declaration runs (zef computes all three).
    ExprPtr verExpr, authExpr, apiExpr;    // name adverbs: class Foo:ver<1.2>:auth<zef:x>:api<2>
    std::string repr;              // `is repr("CStruct")` — native memory layout (NativeCall)
    std::vector<Param> roleParams; // `role R[$x, Bool :$opt]` — value/type parameters
    std::vector<std::pair<std::string, std::vector<ExprPtr>>> roleArgs; // `does R[args]` per composed role
    bool isPackage = false;        // package / module: body runs in a namespace
    std::vector<StmtPtr> body;     // package/module body statements
    ClassDecl(): Stmt(NK::ClassDecl) {}
};

struct SelfTerm : Expr { SelfTerm(): Expr(NK::SelfTerm) {} };

struct Block : Stmt {
    std::vector<StmtPtr> stmts;
    // -1 = not yet decided, 0 = no hoistable decl, 1 = has one. See
    // Interpreter::hoistExprDecls: a static property of this AST, so it is
    // decided once instead of re-walked on every entry to the block.
    DecidedOnce<signed char> hoistNeed{-1};
    bool isCatch = false;   // CATCH { } phaser
    std::string phaser;     // "BEGIN"/"CHECK"/"INIT"/"END"/... (empty = plain block)
    bool stmtForm = false;  // `PHASER statement;` (no braces): runs in the ENCLOSING
                            // scope, so `INIT my $x = …` declares $x there
    Block(): Stmt(NK::Block) {}
};

struct EnumDecl : Stmt {
    std::string name;   // may be empty (anonymous enum)
    ExprPtr values;     // expression evaluating to words / pairs
    bool isExport = false; // `is export` — importers of a braced module see the value names
    EnumDecl(): Stmt(NK::EnumDecl) {}
};

struct IfStmt : Stmt {
    std::string thenVar; // `if EXPR -> $x { }` / `with EXPR -> $x { }` binds the value
    std::vector<std::pair<ExprPtr, std::unique_ptr<Block>>> branches; // if/elsif
    std::vector<std::string> branchVars; // `elsif EXPR -> $x { }` per-branch binders ("" = none)
    std::string elseVar; // `else -> $x { }` binds the last (falsy) condition value
    std::unique_ptr<Block> elseBlock; // may be null
    bool isUnless = false;
    bool modifier = false; // `STMT if COND` postfix form — a `my` in STMT declares in the ENCLOSING scope
    IfStmt(): Stmt(NK::IfStmt) {}
};

struct WhileStmt : Stmt {
    DecidedOnce<signed char> hasStateCache{-1}; // derived: subtree has a `state` decl (-1 unknown); not serialized
    ExprPtr cond;
    std::unique_ptr<Block> body;
    bool isUntil = false;
    std::string var; // `while EXPR -> $x { }` binds each cond value to $x
    std::vector<Param> params;  // full pointy signature — `while $c.receive ->
                                // (:key($k), :value($v)) {…}` destructures each
                                // value, bound through bindParams like `for` does
    bool asExpr = false; // used in value context: collect each iteration's value into a List
    bool modifier = false; // `STMT while COND` postfix form — no implicit block (a `my` in STMT leaks out)
    WhileStmt(): Stmt(NK::WhileStmt) {}
};

struct ForStmt : Stmt {
    DecidedOnce<signed char> hasStateCache{-1}; // derived: subtree has a `state` decl (-1 unknown); not serialized
    ExprPtr list;
    std::vector<std::string> vars; // loop variables ($_ if empty)
    bool rwVars = false;           // `<-> $i` / `-> $i is rw`: writes copy back to the source
    bool destructure = false;      // `-> ($a,$b,$c)`: unpack each element into vars
    std::vector<Param> params;     // full pointy signature when it has sub-signatures
                                   // (named/nested destructure) — bound via bindParams
    std::unique_ptr<Block> body;
    bool asExpr = false; // used in value context: collect each iteration's value into a List
    bool modifier = false; // `EXPR for LIST` — no implicit block (a `my` in EXPR leaks out)
    ForStmt(): Stmt(NK::ForStmt) {}
};

struct ReturnStmt : Stmt {
    ExprPtr value; // may be null
    bool isRw = false; // `return-rw` — return the container itself, not a decontainerized copy
    ReturnStmt(): Stmt(NK::ReturnStmt) {}
};

struct LastStmt : Stmt { std::string target; LastStmt(): Stmt(NK::LastStmt) {} };
struct NextStmt : Stmt { std::string target; NextStmt(): Stmt(NK::NextStmt) {} };
struct RedoStmt : Stmt { std::string target; RedoStmt(): Stmt(NK::RedoStmt) {} };

struct UseStmt : Stmt {
    std::string module;
    std::string verReq; // `use Foo:ver<0.0.14+>` — version constraint ('' = any)
    std::string arg; // first string argument, e.g. `use lib 'lib'`
    std::vector<std::string> importArgs; // `use Mod <tag !flag>` — passed to sub EXPORT
    ExprPtr argExpr; // computed argument, e.g. `use lib $?FILE.IO.parent`
    bool isNo = false; // `no strict` / `no worries` — the negated pragma form
    bool isNeed = false; // `need Mod` — compiles/loads but imports NOTHING
    UseStmt(): Stmt(NK::UseStmt) {}
};

struct EmptyStmt : Stmt { EmptyStmt(): Stmt(NK::EmptyStmt) {} };

// subset NAME of BASE where EXPR — a refinement type participating in dispatch
struct SubsetDecl : Stmt {
    std::string name;
    std::string baseType;  // "" = Any
    ExprPtr where;         // may be null (pure alias)
    SubsetDecl(): Stmt(NK::SubsetDecl) {}
};

struct WhateverExpr : Expr { bool hyper = false; WhateverExpr(): Expr(NK::Whatever) {} }; // hyper: `**` (HyperWhatever)

struct GivenStmt : Stmt {
    ExprPtr topic;
    std::string var; // `given X -> $y { }`: also bind $y to the topic
    bool modifier = false; // `EXPR with X` — no implicit block (a `my` in EXPR leaks out)
    std::unique_ptr<Block> body;
    int defGuard = 0; // 0=given (always), 1=with (run if defined), 2=without (run if undefined)
    bool hasElse = false;
    std::unique_ptr<Block> elseBody; // for `with X {} else {}`
    std::string elseVar; // `else -> $pos { }` binds the (undefined) topic
    GivenStmt(): Stmt(NK::GivenStmt) {}
};

struct WhenStmt : Stmt {
    ExprPtr cond;          // null for `default`
    bool isDefault = false;
    std::unique_ptr<Block> body;
    WhenStmt(): Stmt(NK::WhenStmt) {}
};

struct LoopStmt : Stmt {   // C-style: loop (init; cond; incr) { }
    DecidedOnce<signed char> hasStateCache{-1}; // derived: subtree has a `state` decl (-1 unknown); not serialized
    ExprPtr init, cond, incr;
    std::unique_ptr<Block> body;
    bool asExpr = false; // used in value context: collect each iteration's value into a List
    LoopStmt(): Stmt(NK::LoopStmt) {}
};

struct RepeatStmt : Stmt { // repeat { } while/until cond
    DecidedOnce<signed char> hasStateCache{-1}; // derived: subtree has a `state` decl (-1 unknown); not serialized
    ExprPtr cond;
    bool isUntil = false;
    std::unique_ptr<Block> body;
    RepeatStmt(): Stmt(NK::RepeatStmt) {}
};

struct Program {
    std::vector<StmtPtr> stmts;
};

// Print a program's AST as an indented plain-text tree (for --dump-ast).
void dumpAst(const Program& prog, std::ostream& out);

// Real AOT: emit a self-contained C++ program that rebuilds this exact AST at
// startup and interprets it (no lexing/parsing at run time). Throws AstEmitError
// on any construct it can't reconstruct (the caller then falls back to bundling).
struct AstEmitError { std::string msg; };
struct BundledModule;
void emitAstProgram(const Program& prog, std::ostream& out,
                    const std::string& fileName, const std::string& finish,
                    const std::vector<BundledModule>& mods);

// Emit C++ that registers a compiled binary's embedded modules at startup (see
// collectModuleGraph). Writes nothing when there are none. Shared by --aot and
// --exe so a binary from either carries its dependencies the same way.
struct BundledModule;
void emitModuleTable(const std::vector<BundledModule>& mods, std::ostream& decls,
                     std::ostream& calls, bool withSources = false);

} // namespace rakupp
