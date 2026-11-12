# Classification taxonomy for Rakudo issues (read by every classifier)

One TSV line per issue, 6 tab-separated columns, no header:

    number <TAB> area <TAB> area2 <TAB> kind <TAB> rel <TAB> gist

- number: the issue number without '#'.
- area: the ONE primary language/runtime area (codes below). area2: a secondary area code or '-'.
- kind: one code from the kind list.
- rel: relevance to an independent Raku implementation (codes below).
- gist: <= 14 words naming the concrete behaviour, in plain words, e.g.
  "`.sum` on a Range of Rats returns a Num" or "`is rw` lost on attribute in role". No tabs, no newlines.

## area codes
regex      regexes: metachars, quantifiers, captures, adverbs, `subst`, `tr///`, `.match`, `.comb` with a regex
grammar    grammars, tokens/rules/protoregexes, action classes, `.parse`, LTM
strings    Str methods, interpolation, heredocs, quoting results, `.trans`, Blob/Buf as strings, encodings
unicode    NFG/NFC, graphemes, codepoints, uniprops, `.uniname`, Unicode operators/identifiers
numeric    Int/Rat/FatRat/Num/Complex, precision, rounding, bigints, native int/num maths, `.base`, Inf/NaN
lists      List/Array/Seq/Slip/Range, laziness, iteration, map/grep/sort/unique/rotor/etc, shaped arrays, sequence op `...`
hashes     Hash/Map/object hashes, Set/Bag/Mix/QuantHash and set operators
oo         classes, roles, attributes, MOP/metamodel, `.^methods`, BUILD/TWEAK/new, subsets, enums, coercion types, type objects, parameterized types, Proxy
dispatch   signatures, parameters, multi/proto dispatch, captures, `where`, callsame/nextsame, wrap, return types
concurrency Promise, Supply, react/whenever, Channel, start/await, threads, Lock, atomics, hyper/race, Proc::Async, schedulers
io         files, handles, IO::Path, sockets, `run`/`shell`/Proc, env vars, signals, stdin/stdout, encodings on handles
datetime   Date, DateTime, Instant, Duration, time zones, `now`
exceptions X:: classes, Failure, die/fail/try/CATCH/CONTROL, warnings, backtraces
syntax     parsing, operators and precedence, metaoperators, junctions, smartmatch, Whatever-currying, quoting constructs, compile-time errors, BEGIN/constant, unspace, slangs
control    phasers (ENTER/LEAVE/FIRST/...), loops, given/when, return/next/last, labels, gather/take control flow
scope      variables, containers, binding, assignment, `is rw`, dynamic vars, `my/our/state/temp/let`, packages, export/import, closures
modules    use/need/require, precompilation, CompUnit repositories, META6, zef interaction, module loading
nativecall NativeCall, CStruct/CArray/Pointer, native libraries
pod        Pod/RakuDoc, declarator blocks, `--doc`, `.WHY`
repl       the REPL
cli        command-line options of `raku`, MAIN/USAGE, @*ARGS
eval       EVAL/EVALFILE
formatting sprintf/printf/.fmt/Formatter
rakuast    the RakuAST API itself (classes, DEPARSE) — NOT ordinary bugs merely labelled "Will be addressed in RakuAST"
build      building/installing/configuring Rakudo, packaging, CI, releases, Blin ecosystem reports, toolchain
backend    MoarVM/JVM/JS-backend-specific internals (spesh, JIT, GC, bytecode) not visible as a language rule
test       Test.pm / Test module behaviour
perf       performance or memory where no other area dominates
misc       none of the above

## kind codes
bug        wrong result or wrong behaviour
crash      segfault, VM panic, internal compiler error, "This type cannot unbox", null-pointer style breakage
hang       hang, deadlock, infinite loop
lta        Less-Than-Awesome: misleading/missing/poor error message or warning; the behaviour itself is right or debatable
perf       too slow or too much memory
feature    request for something new (method, option, behaviour)
design     language-design question, consistency debate, spec clarification, "should X do Y?"
regression something that used to work in Rakudo and broke (Blin reports, BLOCKER label with a regression)
build      build/install/packaging/CI/release problem
docs       documentation-only, meta/tracking/umbrella issue, housekeeping

## rel codes (relevance to a NEW, independent Raku implementation that matches Rakudo's observable behaviour)
lang       observable language semantics: any implementation must get this right; the code in the issue is a good test
error      the wording/kind of an error or warning (matters for compatibility, less for correctness)
tool       tooling around the language (REPL, CLI flags, --doc, profiler, debugger, Test.pm output)
design     open design/spec question — interesting for deciding behaviour, no single right answer yet
impl       internal to Rakudo/MoarVM/JVM/JS, its build, release process or ecosystem — little to learn for another engine
