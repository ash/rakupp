# Miscellaneous

*Started 2026-10-15.*

Small items that belong to no plan: gaps found in passing, often while working
on something outside this repo. Each line says how to reproduce the gap and who
needs it fixed. When an item is done, remove its line; when one grows into a
plan, move it to [TODO.md](TODO.md).

---

- [ ] **Imported symbols are missing from `MY::`** (found 2026-10-10; still
  open in the 2026-10-13 build). Calling an imported sub works; only the stash
  view is missing:

  ```raku
  use Test;
  say MY::.keys.grep(*.starts-with("&")).grep(* eq any <&ok &is &plan>).sort;
  say MY::<&ok>:exists;
  ```

  rakupp prints `()` and `False`; Rakudo prints `(&is &ok &plan)` and `True`.
  Imports should land in the importing scope's lexpad the way Rakudo installs
  them, so that `MY::` keys, `:exists`, `MY::<&name>` lookup and the
  pseudo-packages that walk lexical scopes (`OUTER::`, `UNIT::`) all see them.
  The same goes for imported constants, classes and `is export` variables.
  Who needs it: [rakush](https://github.com/ash/rakush), a shell that embeds
  rakupp through the `rk_*` C API, treats every sub in the session's mainline
  `MY::` as a command and finds them with `rk_eval` of `MY::.keys`. With this
  fixed, `use Some::Module` at its prompt makes the module's exported subs
  commands, with no change to rakush. Test that an import done in one
  `rk_eval` call shows up in `MY::` in a later `rk_eval` call on the same
  interpreter, since that is how rakush uses it.

- [ ] **`fails-like` prints one flat test where Rakudo prints a subtest**
  (found 2026-08-29; still open on 2026-10-15). The verdict already checks the
  exception type and every named matcher, but the output is a single `ok` with
  an empty description. Rakudo's Test prints a subtest "did we fails-like T?"
  holding "code returned a Failure" and a nested subtest "Failure threw when
  sunk" ("code dies", "right exception type (T)", one ".KEY matches VALUE" per
  matcher). `throws-like` already goes through `runSubtestFrame` (ca818aad);
  `fails-like` in src/BuiltinsRegister.cpp does not.

  ```raku
  use Test;
  sub f { fail "x" }
  fails-like { f() }, X::AdHoc, message => 'x';
  ```

  rakupp prints `ok 1 - `; Rakudo prints the two-level subtest, then
  `ok 1 - did we fails-like X::AdHoc?`. Who needs it: the per-test Roast count
  (docs/status/COUNTING.md), which misses about four leaf tests per call: Roast
  makes 140 such calls in 36 files, and S32-num/base.t prints 76 leaf `ok` lines
  under rakupp against 108 under Rakudo, all passing.

- [ ] **A closed write handle stays in the exit-flush registry** (found
  2026-08-30; still open on 2026-10-15). `open :w` pushes a strong reference
  onto `openWriteHandles_` (`registerWriteHandle`, src/Interpreter.h), and only
  program exit clears it, so a program that opens and closes many files grows:

  ```raku
  for ^40000 { my $fh = open "loop.txt", :w; $fh.print("x"); $fh.close }
  ```

  Peak footprint (`/usr/bin/time -l`): rakupp 24 MB at 10,000 iterations and
  91 MB at 40,000, about 2.2 KB per closed handle; the same loop with `:r`
  handles stays at 2 MB, and Rakudo stays flat. Drop the entry on `.close`, or
  hold weak references. docs/guide/faq/garbage-collection.md ("Does dropping a
  filehandle close it?") documents the growth; its other claim is out of date,
  as its `writer()` example now prints 5 on both engines. Who needs it: any
  long-running program that writes many files, such as a server writing one
  file per request or a log rotator.

- [ ] **`substr-eq` and `rindex` cost O(string length) per call** (found
  2026-09-02; still open on 2026-10-15). fc6abf32 made `.index` cheap, but not
  these two. On a non-ASCII string each `substr-eq` call costs time in
  proportion to the whole string, and each `rindex` call in proportion to its
  start position:

  ```raku
  my $u = "aé" x 400_000;                                 # 800,000 chars
  my $t = now; for ^200 { $u.substr-eq("é", 1) }; say now - $t;
  $t = now;    for ^200 { $u.rindex("é") };        say now - $t;
  ```

  rakupp prints about 4.4 and 0.22 (seconds for 200 calls), and 0.59 and 0.029
  at 100,000 chars; Rakudo prints under 0.001 for both. On an ASCII string
  `substr-eq` still grows with length (2.7 ms at 100k chars, 16.9 ms at 800k).
  t/scaling/run.raku has no shape for either; `str-index-walk` covers only
  `index`. Who needs it: scanners that advance by position, as CSV::Native's
  pure-Raku parser did before it was rewritten around `split` and `lines`.

- [ ] **`Str.indices` and `Str.comb(Str)` match inside a grapheme** (found
  2026-09-02; still open on 2026-10-15). Both search the UTF-8 bytes, so a
  needle that is only part of a grapheme (the `\r` or `\n` of a CRLF, a lone
  combining mark) is found inside it. `index`, `rindex`, `contains`, `split`
  and `subst` already refuse such a match, and since fa74f74f `indices` reports
  grapheme positions, so only the boundary check is missing (the `s.find` loop
  under `m == "indices"` in src/MethodCallTail.cpp).

  ```raku
  say "x\r\ny\rz\n".indices("\r"), "x\r\ny\rz\n".indices("\n");
  say "q\x[301]z".indices("\x[301]"), " ", "a\r\nb".comb("\n").elems;
  ```

  rakupp prints `(1 3)(2 5)` and `(1) 1`; Rakudo prints `(3)(5)` and `() 0`.
  Who needs it: CSV::Native's pure-Raku parser (raku-modules), which avoids
  `indices` for this reason.

- [ ] **A user multi for an infix hides the built-in's one-argument form**
  (found 2026-09-20; still open on 2026-10-15). Once `multi sub infix:<OP>` is
  declared, `&infix:<OP>(x)` dies with X::Multi::NoMatch instead of reaching
  the built-in's one-argument candidate. Two-argument calls and `[OP]`
  reductions still fall through. The one-argument rule (46bee51a, in
  `callCallableRaw`, src/InterpreterCore.cpp) is gated on `c.builtin`, which no
  longer holds once user candidates own the name.

  ```raku
  class M { has $.c }
  multi sub infix:<->(M $a, M $b) { M.new(c => $a.c - $b.c) }
  multi sub infix:<(|)>(M $a, M $b) { "u" }
  say (try &infix:<->(10)) // "DIED: " ~ $!.^name;
  say (try &infix:<(|)>(1)) // "DIED: " ~ $!.^name;
  say (try &infix:<->(10, 3)) // "DIED";
  ```

  rakupp prints `DIED: X::Multi::NoMatch` twice, then `7`; Rakudo prints `10`,
  `Set(1)`, `7`. Without the two `multi` lines both engines agree. rtUserInfix,
  rtUserInfixInto and rtUserPrefix deliberately differ (2a6967ca), so a direct
  call may need its own fall-through rule. Who needs it: any program that
  overloads an operator for its own class and also calls or folds
  `&infix:<OP>` with one argument.

- [ ] **`do` in front of a pointy block drops a `for` modifier** (found
  2026-09-20; still open on 2026-10-15). With `do` in front, the statement's
  expression is a `do` Unary wrapping the BlockExpr; the `for` branch of
  `Parser::applyModifiers` (src/Parser.cpp) moves a pointy block's parameters
  onto the ForStmt but only matches a bare BlockExpr, so the block never runs.
  Run each line on its own:

  ```raku
  do -> $x { print "ran " } for 1..2;
  do -> { print "ran " } for 1..2;
  do <-> $x { print "ran " } for 1..2;
  ```

  rakupp prints nothing for any of them. Rakudo prints `ran ran ` for the
  first and dies at run time on the second ("Too many positionals passed") and
  the third (X::Parameter::RW). Without `do`, both engines agree. Who needs it:
  nothing has run into it yet; found next to the `do {…} while` parse fix.

- [ ] **`<->` binds a value with no container instead of refusing it** (found
  2026-09-20; still open on 2026-10-15). A doubly pointy block makes every
  parameter `is rw`, so a bare value, such as a `.kv` key or a Range element,
  must fail to bind with X::Parameter::RW. rakupp binds it and runs the body,
  and over `.kv` the write to `$v` goes through:

  ```raku
  my @a = 1, 2, 3;
  for @a.kv <-> $k, $v { $v = 9 }
  say @a;
  ```

  rakupp prints `[9 9 9]`; Rakudo dies: "Parameter '$k' expects a writable
  container (variable) as an argument, but got '0' (Int) as a value without a
  container." `for 1..3 <-> $x { … }` is the same. `for @a.kv -> $k, $v is rw`
  must keep writing through, as it does in both engines. Who needs it: the one
  case left from the immutable-topic work (87378aa7);
  t/regression/for-immutable-topic.raku pins `$_` and `is raw` but not `<->`.

- [ ] **A subscript key that is a method call is clobbered by a recursive
  right-hand side** (found 2026-09-24; still open on 2026-10-15). In
  `%h{EXPR} = RHS` and `@a[EXPR] = RHS`, when EXPR is a method call and RHS
  re-enters the same routine, the inner call's assignment uses the outer
  call's key:

  ```raku
  sub f($d) { my %h; %h{$d.Str} = $d ?? f($d - 1) !! 'x'; %h }
  say f(1);
  sub g($d) { my @a; @a[$d.Int] = $d ?? g($d - 1) !! 'x'; @a }
  say g(1).raku;
  ```

  rakupp prints `{1 => {1 => x}}` and `[Any, [Any, "x"]]`; Rakudo prints
  `{1 => {0 => x}}` and `[Any, ["x"]]`. A key like `"k$d"` or `$d + 0`, or
  `:=` instead of `=`, gives the right answer, so the evaluated key seems to
  sit in a per-call-site slot that the recursion overwrites. Check the compiled
  paths too. Who needs it: YAML::Native (raku-modules, construct/build-map),
  which works around it with a temporary variable.

- [ ] **A Set, Bag or Mix matches `Map`, and `$*RAKU` and a `Lock::Async`
  match `Associative`** (found 2026-09-24; still open on 2026-10-15). fc6abf32
  fixed the hash-backed built-ins first reported (IO::Handle, Proc, Promise,
  Supplier, Lock, Channel) with a deny-list in `hashKindIsAssociative`
  (src/InterpreterParts.h); these still get through. A Set does Associative on
  both engines, but it is not a Map.

  ```raku
  say set(1) ~~ Map, " ", bag(1) ~~ Map, " ", mix(1) ~~ Map, " ", SetHash.new ~~ Map;
  say $*RAKU ~~ Associative, " ", Lock::Async.new ~~ Associative;
  sub g(Map $m) { "bound" }; say (try g(set(1))) // "refused";
  ```

  rakupp prints `True True True True`, `True True`, `bound`; Rakudo prints
  `False False False False`, `False False`, `refused`. Who needs it:
  YAML::Native's serializer, whose kind-of works around it with
  `~~ Hash || .isa(Map)`, and any serializer that dispatches on `Map`.

- [ ] **A deleted hash value stays alive until the hash is emptied** (found
  2026-09-26; still open on 2026-10-15). `ValueHash::erase`
  (src/ValueHash.h) only marks the entry dead and tombstones its index slot;
  the entry's Value, and whatever string, array or object it holds, lives on
  until `clear()`. `Value&` stability needs the tombstone, not the payload.

  ```raku
  my %h; for ^300 { %h<k> = "x" x 1_000_000; %h<k>:delete }
  ```

  Peak footprint (`/usr/bin/time -l`): rakupp 308 MB, or 5 MB when the key is
  overwritten instead of deleted; Rakudo 89 MB. `.DELETE-KEY` and fresh keys
  retain the same; `@a[0]:delete` does not. Once fixed, drop the "One
  exception today" paragraph from docs/guide/faq/refcounting.md. Who needs it:
  any long-running program that uses a hash as a cache or a work queue.

- [ ] **`for` over an array or hash slice does not alias the elements** (found
  2026-09-26; still open on 2026-10-15). A slice hands out the element
  containers, so the loop's `$_` should be bound to each one; rakupp loops over
  copies. `for @a` itself works, and so does `my $r = @a[^2]; $r[0] = 9`:

  ```raku
  my @a = 1..5;
  $_ *= 10 for @a[^2];    say @a;
  .=not for @a[3,4];      say @a;
  @a[^2]».++;             say @a;
  my %h = a => 1, b => 2; $_++ for %h<a b>; say %h<a b>;
  ```

  rakupp prints `[1 2 3 4 5]` three times and `(1 2)`; Rakudo prints
  `[10 20 3 4 5]`, `[10 20 3 False False]`, `[11 21 3 False False]` and
  `(2 3)`. `for @a[^2] -> $x is rw { $x = 0 }` loses the write the same way.
  Who needs it: Rosetta Code's "100 doors" (docs/dev/findings/ROSETTACODE.md)
  and the FAQ examples.

- [ ] **A nested list on the left of a plain assignment destructures instead
  of flattening** (found 2026-10-11; still open on 2026-10-15). Rakudo
  flattens the left side of a list assignment into one list of containers;
  only a `my (…)` declaration destructures. rakupp destructures in both.

  ```raku
  my ($a, $b, $c, $d);
  ($a, ($b, $c), $d) = 1, 2, 3, 4;     say ($a, $b, $c, $d).raku;
  ($a, ($b, $c), $d) = 1, (2, 3), 4;   say ($a, $b, $c, $d).raku;
  ```

  rakupp prints `(1, 2, Any, 3)` and `(1, 2, 3, 4)`; Rakudo prints
  `(1, 2, 3, 4)` and `(1, $(2, 3), 4, Any)`. `my ($x, ($y, $z)) = 7, [8, 9]`
  gives `(7, 8, 9)` on both and must stay that way. No Roast file asserts
  either reading of the plain form. Who needs it: the JS backend, whose
  `listAssign` (src/codegen/Js.cpp) refuses a nested target until the
  interpreter is settled.

- [ ] **`.minmax` over Rats or Nums is a List, not a Range** (found
  2026-10-11; still open on 2026-10-15). Only an all-Int or all-Str list gets
  a Range:

  ```raku
  say (3e0, 1e0).minmax.raku;   say (1, 2.5).minmax.raku;
  say (3e0, 1e0).minmax.bounds;
  ```

  rakupp prints `(1e0, 3e0)` and `(1, 2.5)`, then dies with "No such method
  'bounds' for invocant of type 'List'"; Rakudo prints `1e0..3e0`, `1..2.5`
  and `(1 3)`. The sub form `minmax(3e0, 1e0)` has the same bug. Who needs it:
  webraku's examples/wavelet/Analysis.rakumod (`.minmax.bounds`).

- [ ] **Some stores into a hash element or a scalar leave the value
  un-itemized** (found 2026-10-11; still open on 2026-10-15). Plain `=` is
  right, but `//=`, `||=`, `.push` of a Hash onto a new key, and a `Hash()`
  append merge store the bare List, Array or Hash, so assigning the stored
  value to an array flattens it:

  ```raku
  my %h; %h<a> //= (1, 2, 3); my @a = %h<a>; say @a.elems;
  my $s; $s //= (1, 2, 3);    my @b = $s;    say @b.elems;
  my %p; %p.push: (b => {z => 1, y => 2}); my @c = %p<b>; say @c.elems;
  my Hash() %o = a => {x => 1}; %o.append: %(a => {y => 2});
  my @d = %o<a>; say @d.elems;
  ```

  rakupp prints 3, 3, 2, 2; Rakudo prints 1, 1, 1, 1 (and `%h<a>.raku` is
  `$(1, 2, 3)` there, `(1, 2, 3)` here). `%h<a> = (1, 2, 3)` and
  `@a[0] //= (1, 2, 3)` are already right. Who needs it: webraku's
  examples/wavelet/Analysis.rakumod (`%h<a> //= …`, then `my @a = %h<a>`), and
  Text::Table::Simple's `%options.append: %o`, which reads back an un-itemized
  merged value though its suite passes.

- [ ] **`--exe` native code dies on every `⚛` operation** (found 2026-10-12;
  still open on 2026-10-15). The parser lowers `$n⚛++` to a call named
  `atomic-fetch-inc`; the interpreter treats it, and the rest of the
  `atomic-*` family, as a special form, but native codegen emits an ordinary
  routine call and the runtime has no routine by that name (src/Codegen.cpp
  has no `atomic-*` case):

  ```raku
  my atomicint $n = 0;
  $n⚛++ for ^10;
  say $n;
  ```

  `rakupp x.raku` and Rakudo print `10`; `rakupp --exe x.raku -o x && ./x`
  prints "Undefined routine 'atomic-fetch-inc'" and exits 1. `atomic-fetch-add`
  and `cas` do the same, in a sub or a method too. In a `DESTROY` the error is
  swallowed, so `$n⚛++` there counts nothing (0 under --exe where the
  interpreter counts 3000). Programs that --exe compiles as "Compiled" rather
  than "Compiled (native)" are fine. Who needs it: the cycle collector's --exe
  checks (#151; CYCLE-GC-PLAN.md counts frees with `$freed⚛++` in DESTROY),
  and any parallel program built with --exe.

- [ ] **`run(…, :in)` sets close-on-exec on its pipes only after the fork**
  (found 2026-10-15). fb7a9b13 made spawn pipes close-on-exec before the fork
  (`cloexecPipe` in `spawnChildStart`, src/Builtins.cpp), but
  `spawnWithInput`, the `run(…, :in)` path, still calls plain `pipe()` and sets
  `FD_CLOEXEC` after `forkForExec()`. Another thread that forks in between
  inherits the write end, and the child reading our input never sees EOF.
  Use `cloexecPipe` there too; t/race/spawn-pipe-cloexec.raku is the shape of
  test to extend. Who needs it: any parallel program that runs children with
  `:in` while other threads spawn, such as the Roast harness.

- [ ] **A patch release for an older minor would become "latest"** (found
  2026-10-10). The way to ship a fix to 5.3 users once 5.4 exists is a
  release branch cut from the tag (`git switch -c release/5.3 v5.3.0`, cherry
  pick, gate, tag v5.3.1). Two steps in `.github/workflows/release.yml` would
  then hand v5.3.1 to everyone: "Attach to Release"
  (`softprops/action-gh-release`) marks each release as latest by default, so
  `install.sh` and every `releases/latest/download/` link would serve 5.3.1;
  and the "Image tags" step always adds `ghcr.io/ash/rakupp:latest`. Compare
  the tag with the highest existing `v*` tag, and set `make_latest` and the
  Docker `latest` tag only when it is the newest; the `x.y` image tag is fine
  as it is. Who needs it: the first patch release cut from a release branch,
  so it has to land before that.

- [ ] **The release policy is not written down** (found 2026-10-10). Users
  install tags, not `main`, but nothing says so, and a patch release has so
  far meant "newer", carrying every commit since the last tag (v5.2.1 had 17,
  features included). A few lines in docs/guide/INSTALL.md and the README:
  install releases, `main` is development; the latest minor gets fixes as
  x.y.z patches cut from a release branch when needed, so a patch holds fixes
  only; breaking changes come only in a major, with a deprecation warning one
  minor ahead where possible. docs/dev/RELEASING.md should say how a patch is
  cut from a release branch. Who needs it: users deciding whether a patch
  release is safe to take, and the next patch release.
