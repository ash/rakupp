# Miscellaneous

*Started 2026-10-15.*

Small items that belong to no plan: gaps found in passing, often while working
on something outside this repo. Each line says how to reproduce the gap and who
needs it fixed. When an item is done, remove its line; when one grows into a
plan, move it to [TODO.md](TODO.md).

---

- [ ] **A regex does not interpolate a variable with a non-ASCII name**
  (found 2026-10-19 by FatRoast's `names-v1` in the `mixed` palette):

  ```raku
  my $é = rx/a/;          say "a" ~~ /$é/;   # Nil, Rakudo ｢a｣
  my @𝐯 = rx/a/, rx/b/;   say "a" ~~ /@𝐯/;   # Nil, Rakudo ｢a｣
  ```

  ASCII names interpolate, and since edf09560 such a name binds and types
  correctly outside a regex. The regex compiler's variable test is likely
  an ASCII-only first-letter check of the kind that commit replaced. Who
  needs it: S05-interpolation under any palette.

- [ ] **`.trans` with a regex key miscounts after a combining mark** (found
  2026-10-19 by FatRoast's `strings-v1` in the `mixed` palette):

  ```raku
  say "a̲b c".trans(/ \s+ / => "_");   # a̲_ c, Rakudo a̲b_c
  say "e̲ x".trans(/ \s+ / => "_");    # e̲ x, Rakudo e̲_x
  ```

  The match lands one position early per multi-codepoint grapheme before it,
  a codepoint offset used as a grapheme index. Without marks it is right.
  Who needs it: any `.trans` with regex keys over text with combining marks.

- [ ] **An undeclared operator called by name does not fail** (found
  2026-10-19 by FatRoast's `strings-v1`):

  ```raku
  say infix:<nosuch>(42);     # 42; Rakudo: Undeclared routine infix:<nosuch>
  say &infix:<nosuch>;        # &infix:<nosuch>; Rakudo: the same error
  ```

  The one-argument form returns its argument and the code reference is
  made up; with two arguments rakupp says "Unsupported operator" at run
  time. Rakudo refuses all three when compiling. Who needs it: any typo in an
  operator name, which rakupp turns into a silent pass.

- [ ] **Test's exit status differs from Rakudo's when a file dies or runs
  short** (found 2026-10-19; about 40 exit-status-only FatRoast findings per
  sample):

  ```raku
  use Test; plan 2; ok 1; die "x";   # rakupp exits 1, Rakudo 255
  use Test; plan 3; ok 0; ok 0;      # rakupp exits 255, Rakudo 2
  ```

  Rakudo's Test END exits with the failure count (at most 254) when any test
  failed, else 255 when the count run differs from the plan, and that exit
  overrides a `die`'s 1. Who needs it: harnesses that read the status, and
  differential runs that compare it.

- [ ] **A subscript that calls something runs twice on a read** (found
  2026-10-19). Every read path through a slice whose subscript is a call
  evaluates the call twice:

  ```raku
  my @e = 1, 2, 3;
  sub idx { say "idx"; (0, 1) }
  say @e[idx()];
  .say for @e[idx()];
  ```

  rakupp prints `idx` twice before each answer; Rakudo prints it once. The
  assignment side already parks the evaluated subscript for the slot lookup
  (`parkSubscript`, src/Interpreter.h); evalIndex (src/InterpreterCore.cpp)
  has many `eval(idx->index.get())` sites and no such guard. It is also why
  `for` over a slice aliases only side-effect-free subscripts
  (`pureSubscript`, src/InterpreterCalls.cpp). Who needs it: any subscript
  with a side effect, such as a cursor or a random pick.

- [ ] **A `for` over a slice copies what it cannot alias** (found
  2026-10-19). Since 2026-10-19 `for @a[^2]`, `for %h<a b>` and `@a[^2]».++`
  write through, but three shapes still loop over copies:

  ```raku
  my @d = 1, 2;      $_ = 9 for @d[0, 5];        say @d;
  my %h = a => 1;    $_ = 3 for %h<a c>;         say %h;
  my @e = 1, 2, 3;   $_ = 5 for @e[(0, 1).map(* + 0)]; say @e;
  ```

  rakupp prints `[1 2]`, `{a => 1}`, `[1 2 3]`; Rakudo prints
  `[9 2 (Any) (Any) (Any) 9]`, `{a => 3, c => 3}`, `[5 5 3]`. A position past
  the end and a missing key are vivified by Rakudo's write; a subscript that
  is not plainly side-effect free waits for the item above. Who needs it:
  nothing has run into it yet.

- [ ] **`%h<a b> //= v` stores under the key "a b"** (found 2026-10-19).

  ```raku
  my %u; %u<a b> //= 5; say %u.raku;
  ```

  rakupp prints `{"a b" => 5}`; Rakudo prints `{}` (the slice is a defined
  List, so nothing is assigned). The short-circuit assignment path in
  evalAssignInner (src/InterpreterCore.cpp) takes a slice target as one key.
  Who needs it: nothing has run into it yet.

- [ ] **A binding to a hash element does not survive the key's deletion**
  (found 2026-10-19; not new).

  ```raku
  my %h = k => 5; my $x := %h<k>; %h<k>:delete; say $x;
  my %g = a => 1, b => 2;
  for %g.pairs -> $p { %g<b>:delete; say $p.value if $p.key eq "b" }
  ```

  rakupp prints `(Any)` and nothing for the second line; Rakudo prints `5`
  and `2`. The same before and after the 2026-10-19 change that lets a
  deleted hash value go (`ValueHash::erase`, src/ValueHash.h, which keeps the
  value while a Pair aliases an entry). Who needs it: nothing has run into it
  yet.

- [ ] **More one-argument infix forms disagree** (found 2026-10-19). With no
  user multi in the way:

  ```raku
  say [(|)] 1; say &infix:<Z>((1, 2)); say &infix:<,>(1);
  ```

  rakupp prints `1`, `(1 2)`, `1`; Rakudo prints `Set(1)`, `((1 2))`, `(1)`.
  The rule is `Interpreter::infixOneArgForm` (src/InterpreterCalls.cpp) for
  the Callable, applyReduce for the reduction. Who needs it: nothing has run
  into it yet.

- [ ] **A Regex prints as `rx/…/` where Rakudo prints `/…/`** (found
  2026-10-19). `say /y/`, `/y/.gist` and `/y/.raku` are `rx/y/` here and `/y/`
  in Rakudo, so Test's matcher description reads `.message matches rx/y/`
  where Rakudo's reads `.message matches /y/`. Who needs it: TAP output
  compared line by line against Rakudo's.

- [ ] **Test's closing diagnostic is worded differently** (found 2026-10-19).
  With a plan, rakupp ends a failing run with `# Looks like you failed 1 test
  of 2` where Rakudo 2026.09 says `# You failed 1 test of 2` (and the same
  inside subtests); with no plan and no `done-testing`, rakupp prints that
  line and Rakudo prints none. Both go to stderr. Who needs it: tools that
  read the diagnostics; the TAP verdict is the same.
