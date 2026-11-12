# The next release's CHANGELOG entry, collected as work lands

*Emptied at the v5.7.0 tag: everything collected here is in
[CHANGELOG.md](../../CHANGELOG.md). This is not a CHANGELOG entry yet:
[RELEASING.md](RELEASING.md) step 2 writes that at the tag, with the tag's
own gate figures. Add each item as it lands, in the CHANGELOG's own shape —
what changed, what it measured and how, with the commit — so the entry starts
from this page. Move each item into CHANGELOG.md at the tag and delete it
here.*

*Covered so far: 59e9d3aa, e5b2b6c8, dec6d962, 15997e10, 71451a61, c68ec738,
19078698, 9338d1b1, 58e2fdd4, abfea750, 8a11fb4d, 1d8f1495,
fd3fda0f, 33351b91, 5b6a2753, d1e39cb5, 64e4c44f, 7800a133,
9c45b83d.*

---

### New

- **`t/exe/parity.raku`: a compiled program behaves as the interpreted one, or
  the gate fails** (5f0da948, a7fab7cb). Every program in `examples/` and
  `t/regression/` — 1,573 — is built with `--exe` and run against its
  interpreted run, stdout and exit status; one that does not build fails too.
  The old `t/exe/run.raku` compared `-O` with no `-O` and only counted the
  binaries that disagreed with the interpreter, so on 2026-11-11 143 programs
  answered differently compiled and 14 did not build. The binary is built
  beside its source and run with `RAKUPP_EXE_INTERPRETER`, which a compiled
  binary's `$*EXECUTABLE` answers, so the 284 programs that spawn "the
  interpreter" compare too. `t/exe/parity-known.txt` lists what is left: empty.

### Faster

### Fixed: silent wrong answers

- **`--exe` gives the program the interpreter gives** (the `--exe` parity
  round: 75425e0a … 00938a8e, four merged branches). From 160 divergences to
  none on `t/exe/parity.raku`. The silent ones included: a block a module
  routine hands another module's routine read the wrong `my constant` and died
  on its own private subs (#240, #242); `say %h<missing>` printed Nil, not
  (Any); `%h<k> = @x` stored three items, not one; `multi f(Mu)` beside
  `multi f(Any)` took Mu for everything; a typed `has Int:U $!u` or a `buf8`
  parameter took what it should refuse; `is rw` writes were lost in several
  call shapes; Cro's router pushed a whole Junction (routes answered 404);
  `temp` in a routine was never undone. Where the native code now calls the
  interpreter's own function it computes the same answer; where the meaning is
  the interpreter's alone (`EVAL`, pseudo-packages, most of `.^`, `callframe`,
  KEEP/UNDO, `.wrap`, a capturing nested sub that escapes) the program bundles
  with the interpreter, slower but the same — except an `EVAL` under
  `--slim=-eval`, which compiles to the X::Feature::NotBuilt it throws, since a
  bundle needs the parser the cut removed. `.map(|*.list)` curries (#243).
  A program whose generated C++ does not compile bundles instead of leaving no
  binary (88264aeb). `$s = $s ~ X` in a loop is linear compiled too (160k
  appends: 1.4 s → 5 ms). Reported by Zer0-Tolerance (#240, #242, #243).
- **A junction matcher smartmatches a stored Whatever as a value**
  (e99c5e25): `my $w = *; $w ~~ any(Pair, Int)` was True; Rakudo's False.

- **`s///` decodes `\x41`, `\x[…]`, `\o101`, `\c65` and `\c[NAME]` in its
  replacement** (59e9d3aa) (#236): it dropped the backslash and put in the
  rest as text, so `s/X/\x41/` gave `ax41b` where Rakudo gives `aAb`; `S///`
  and `s{…} = "…"` alike. A single-quoted `s[…] = '…'` is literal again: it
  lost its backslash the same way.
  `t/regression/subst-replacement-codepoint-escapes.raku`. Reported by
  Offizin.
- **A Str holding a quote, interpolated into an `s///` pattern, matches as
  text** (e5b2b6c8) (#237): `s/$q$q/Q/` with `$q = '"'` found an empty match
  at 0 and gave `Qa""b`, where `m//` and `.subst` gave `aQb`. The pattern's
  variables are now quoted as `~~` quotes them, every ASCII punctuation
  character escaped. `t/regression/subst-interpolated-quote-char.raku`.
  Reported by Offizin.
- **`Blob.new` and `Buf.new` of a Seq nothing has read yet are filled**
  (c68ec738) (#241): `sub f { (^3).map({ $_ + 1 }) }; Blob.new(f())` was
  `Blob:0x<>`, and a gather's the same; a 5.6.0 regression, since a
  routine's `.map` became lazy. `t/regression/blob-new-of-lazy-seq.raku`.
  Reported by Zer0-Tolerance.
- **`RakuAST.EVAL` keeps the tree's grouping** (58e2fdd4) (#247): it compiled
  the deparsed text, which writes no parentheses, so the tree for
  `(not 0) && (not 1)` ran as `not (0 && not 1)` — True where Rakudo says
  False. `.DEPARSE` still writes Rakudo's text byte for byte.
  `t/regression/rakuast-eval-keeps-tree-grouping.raku`. Reported by
  Zer0-Tolerance.
- **`.delayed` delays done and quit as well, as Rakudo's does**: a quit cut
  off the value still waiting (`$s.emit(1); $s.quit(…)` on a `.delayed(0.3)`
  delivered nothing, Rakudo's 1 and then the quit), and an empty source was
  done at once, not after the delay.
  `t/regression/supply-delayed-delays-done-and-quit.raku`.
- **`$0`, `$1`, … read the `$/` in scope** (33351b91) (#256): a `my $/`, a `$/`
  parameter, a pointy block's and an action method's `$/` saw the captures of
  the last `~~` anywhere visible, since each match also wrote `$0..$N` as
  variables of their own. JSON::Tiny guards on `$0.Str ne '"'`, so after any
  capturing `~~` every key it parsed gained a prefix (`{"a":1}` → `Za`). They
  are `$/[N]` now, as `$<name>` was; a regex code block's `$/` is the cursor
  even under a `$/` parameter, and `$/ = Nil` leaves Nil.
  `t/regression/capture-var-reads-scoped-match.raku`. Reported by Offizin.
- **`.write` takes a Blob and refuses a Str** (7800a133): every handle's
  `.write` took text and sent the engine's UTF-8 whatever its `:enc` said —
  `$fh.write("é")` on a Latin-1 handle wrote (195 169), where Rakudo refuses
  the call and `.print` writes (233) — and took an Int, a List, no argument
  or two. Rakudo's binding errors now, on files, `$*OUT`/`$*ERR`, a Proc's
  stdin, sockets, Proc::Async and IO::Handle subclasses.
  `t/regression/handle-write-binds-a-blob.raku`. Reported by the open pull
  request #257, whose change is not used.
- **An unclosed character class is refused at compile time** (9c45b83d) (#254):
  `m/<-[W]*/`, the `>` left out, matched "Hello " where Rakudo dies
  "couldn't find final '>'"; so did `m/<[a]/`, `rx//`, `m{}`, a `regex`
  declaration and `s///`. A bare `/<[a]/` said it could not find the closing
  `/`. `t/regression/regex-class-assertion-needs-its-close.raku`. Reported by
  WeirdAnalyst.

### Fixed: crashes

- **A long run of a quantified group no longer overflows the stack**
  (abfea750) (#250): `[.*? 'Hello' \s* '=' \s* $<result>=(\d+)] *` over
  100,000 records died with SIGSEGV on a Raspberry Pi 5 and SIGBUS on macOS,
  each repetition costing stack even on the main thread's 1 GiB. At the
  256th repetition the engine makes the recursion's own first attempt in a
  loop, and falls back to the recursion only if what follows fails. The
  step budget against catastrophic backtracking, a fixed 8M that made
  400,000 such records answer no match, grows by 16 steps a byte: 1,000,000
  records match in 0.9 s, and a failing pathological search on a long
  subject runs that much longer before it gives up (14 s against 10 s on
  200,000 bytes of `[a|aa]* <?{ False }>`).
  `t/regression/regex-long-group-repetition.raku`. Reported by WeirdAnalyst.
- **`.match(:c)` and `:p` start where asked under `:m` / `:ignoremark`**
  (dec6d962) (#246): the search began at 0 every time, so the usual
  `while $s.match($rx, :$c) { $c = $/.pos }` walk found its last match
  forever. `t/regression/match-continue-ignoremark.raku`. Reported by
  Zer0-Tolerance.
- **`run(:in("x"), "cat", :out)` no longer deadlocks** (5b6a2753) (#258): every
  truthy `:in` opened a stdin pipe nothing closed. As in Rakudo, only True is
  that pipe, the Str '-' inherits, and anything else — False, a Str, a number,
  Any, Nil — closes the child's stdin. `shell(…, :in)` pipes too, where it
  gave the child our stdin. `t/regression/run-in-non-handle-closes-stdin.raku`.
  Reported by Offizin.

### Behaves like Rakudo

- **A `<<` word boundary followed by a space opens no word list** (15997e10)
  (#239): `/ (<< [unit\s+]? [class|role] \s+) (Test) >> /` died "couldn't
  find final ')'" at compile time, the second `<` read as a `< … >` list
  that ran to the `>>`. `t/regression/regex-word-boundary-in-group.raku`.
  Reported by kliklamala.
- **A `my` is declared before it runs** (71451a61) (#248, #249): in the
  right side of `||`, `&&`, `//`, `or`, `and`, `^^`, `xor`, `andthen`,
  `notandthen` and `orelse` that never ran, it was X::Undeclared later — rak
  picks its context option with `my $c := (my $a := …) || (my $b := …)` —
  and in a sub body `my $c := do { $c := 2 }` died the same way, which took
  out App::Rak's --sourcery. A `my` in an `andthen`/`orelse` right side
  lands in the scope around it, not the topic's; and in the REPL (8a11fb4d) such
  a declaration is still there on the next line, as a skipped modifier's and
  an untaken ternary side's now are too.
  `t/regression/my-in-short-circuit-and-do-init.raku`,
  `t/regression/repl-hoists-branch-declarations.raku`. Reported by
  Zer0-Tolerance.
- **A stash term before a block or pointy block is a term** (19078698)
  (#238): `for A::, B:: -> $pkg { … }` and
  `for Foo::EXPORT::DEFAULT::, Bar::EXPORT::DEFAULT:: -> $pkg` died "Function
  'B::' needs parens to avoid gobbling block"; a 5.6.0 regression. One
  nothing declares is still refused, and a lone `for A:: -> $p` walks the
  stash's entries as a hash's. `t/regression/stash-term-before-pointy-block.raku`.
  Reported by Offizin.
- **`UNIT::{'&name'}:exists` and `:p` inside `sub EXPORT` see the module's
  symbols under an import list** (9338d1b1) (#245): they answered False and
  `()` while a plain `UNIT::{'&name'}` found the sub; `UNIT::{'&EXPORT'}:exists`
  was False either way. `t/regression/unit-stash-exists-under-import-list.raku`.
  Reported by Zer0-Tolerance.

- **`Uni.encode` is a `Buf`** (1d8f1495) (#251): `$s.NFD.encode("utf8")`,
  the other normal forms' and a bare `Uni.new(…)`'s answered `utf8` (or
  `Blob[uint8]` for latin-1), as `Str.encode` does, so `~~ Buf` was False and
  a `--> Buf` return check died. The codepoints encode as they are, so an NFD
  stays decomposed. `t/regression/uni-encode-is-a-buf.raku`. Reported by
  Offizin.
- **`Unicode.version` and `Unicode.NFG`** (fd3fda0f) (#252), on the class and
  on `Unicode.new`, were X::Method::NotFound: a program could not ask which
  Unicode the engine knows. They answer `v18.0` (Rakudo 2026.09: `v17.0`)
  and `True`. `t/regression/unicode-version-and-nfg.raku`. Reported by
  Offizin.
- **`my %h = Nil` is the odd-number error** (d1e39cb5) (#259), as the Nil docs
  say: Nil takes one slot in a list, and a hash store needs an even count. It
  and `%h = $was-set-to-Nil` were an empty Hash. `%(Nil)` and `%(Any)` are
  `.hash`, the empty Hash (`%(Any)` had thrown).
  `t/regression/hash-store-of-nil-is-odd.raku`. Reported by Offizin.
- **A malformed `{…}` in a string is refused at compile time** (64e4c44f) (#260):
  `say "{1 2}"` printed 2, `"{\\hsize=100pt}"` died at run time as "Target
  is not assignable", and `-c` passed both. An interpolated block has an
  EVAL snippet's rule, two terms in a row, and its parse errors point into
  the string. `t/regression/interpolated-block-two-terms.raku`. Reported by
  Offizin.

### Platforms and packaging

### Tools

### Left open

- The `--exe`-only issues #240, #242, #243 and #244 wait for a round of
  their own on `--exe` coverage.
