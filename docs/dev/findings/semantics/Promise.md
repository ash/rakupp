# Promise, await, start, sleep, Lock, Lock::Async, Lock::Soft, Semaphore — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Promise.rakumod` (433
lines), `Lock.rakumod` (38), `Lock/Async.rakumod` (208), `Lock/Soft.rakumod`
(302), `Semaphore.rakumod` (16), `Awaiter.rakumod` (110), `Awaitable.rakumod`
(49), `asyncops.rakumod` (56; the 6.c `await`), `src/core.d/await.rakumod`
(118; the `await` in force), `Scheduler.rakumod` (39),
`CurrentThreadScheduler.rakumod` (49), `Cancellation.rakumod` (23) and the
`sleep` family in `Date.rakumod` (lines 441–471), read in full on 2026-09-23;
`ThreadPoolScheduler.rakumod` read for the `cue` candidates and their
argument checks (lines 855–1070 of 1,167), `Channel.rakumod` for its await
handle, `close` and `fail` (lines 200–300), and
`src/Raku/ast/statementprefixes.rakumod` (lines 520–550) for the one thing
the `start` prefix adds over `Promise.start` (PR-19). Not read: the thread
pool's worker management, `Supply`'s await handle (its `await` is recorded as
observed), `Thread`, the atomics. There is no `awaiterator` anywhere in the
tag. Oracle: Homebrew Rakudo v2026.08 on macOS. Compared against Raku++
4.0.1-84-ga4291988 (build-arm64, 2026-09-23). Format and legend:
[README.md](README.md).

Where this sits against the declared spec: Rakudo passes all 13
`S17-promise` files, all 5 `S17-scheduler`, all 10 `S17-lowlevel` and
`S29-context/sleep.t` on this machine; Raku++ passes 6 of `S17-promise`
(anyof, at, in, stress and the two lock-async-stress files), 2 of
`S17-scheduler` (basic, times), 6 of `S17-lowlevel` (not lock.t, atomic.t,
cas.t, cas-loop.t) and `sleep.t`. The rules below are the ones behind those
files and behind every program that races a `start` against `Promise.in`:
what keeps and breaks a promise and who may, what `.result` and `await`
throw and with which role mixed in, what `then` hands on, what the factories
accept, how a sunk `start` ends the program, what the three locks and the
semaphore return and refuse, and what `sleep` and its two siblings answer.
12 of the 36 items are neither fully documented nor fully asserted by Roast.
Four Rakudo behaviours are recorded as bugs and seven as quirks; step two
should not imitate the bugs.

Every probe ran with `alarm 10` and standard input closed, in a fresh
sandbox directory per engine. Anything that could block is run inside a
`start` raced against a `Promise.in` timeout, and the statement says where a
field depends on timing; the margins are generous (0.2 s sleeps against
0.5–3 s timeouts). Two probes (PR-19, PR-35) spawn `$*EXECUTABLE -e` child
processes to observe exit codes; the thread number in their stderr is
replaced by `N`, and a Promise's `.Str` address by `N` in PR-10. No output
contains a path.

## A. Construction, state, keeping and breaking

### PR-01  Promise.new: state, truth, scheduler, roles                  D:partial R:yes V:spec
A new Promise is `Planned`. `.status` is a `PromiseStatus` enum value
(Planned 0, Kept 1, Broken 2) that answers `==`, `~~` and `eqv` against the
enum constants; `.Bool`, `?` and `.so` are False while Planned and True once
Kept or Broken; `.defined` is True. `.scheduler` is the scheduler the
promise was created with, `$*SCHEDULER` (a `ThreadPoolScheduler`) unless
`:scheduler` was passed. Smartmatching the Promise itself against an enum
value is False (Roast: it must not hang). The type object is False and
undefined. The class does the `Awaitable` role only; its MRO is Promise,
Any, Mu.
```
my $p = Promise.new; say $p.^name, " ", $p.status, " ", $p.status.^name, " ", +$p.status, " ", $p.status.raku, " ", $p.Bool, " ", ?$p, " ", $p.so, " ", $p.defined, " ", $p.scheduler.^name, " ", ($p.scheduler === $*SCHEDULER), " ", ($p ~~ Planned), " ", ($p.status ~~ Planned), " ", ($p.status == Planned), " ", ($p.status eqv Planned), " ", ?Promise, " ", Promise.defined, " ", PromiseStatus.enums.raku, " ", Kept.Int, " ", Broken.Str, " ", ($p ~~ Awaitable), " ", ($p ~~ Promise), " ", $p.^mro.map(*.^name).raku, " ", $p.^roles.map(*.^name).raku
# rakudo 2026.08: Promise Planned PromiseStatus 0 PromiseStatus::Planned False False False True ThreadPoolScheduler True False True True True False False Map.new((:Broken(2),:Kept(1),:Planned(0))) 1 Broken True True ("Promise", "Any", "Mu").Seq ("Awaitable",).Seq
```
rakupp 4.0.1-84: differs — `.scheduler` does not exist; the line died there and the rest is unmeasured.

### PR-02  The PromiseStatus enum                                        D:partial R:partial V:spec
`Planned` is False in boolean context (its value is 0), `Kept` and `Broken`
are True; `.Str` and `.key` give the name, `.value` and `.Int` the number;
`PromiseStatus(1)` is Kept while coercing from a name string is
`X::Enum::NoValue`; the values are ordered (`Kept < Broken`); `Kept == 1`
and `Kept eq "Kept"` both hold. A promise's `.gist` shows its scheduler and
its status and no result, and contains no address.
```
say ?Planned, " ", ?Kept, " ", ?Broken, " ", Planned.Str, " ", Kept.key, " ", Kept.value, " ", (try PromiseStatus(1).raku) // $!.^name, " ", (try PromiseStatus("Broken").raku) // $!.^name, " ", (Kept < Broken), " ", Planned.^name, " ", PromiseStatus.^name, " ", Kept.raku, " ", Kept.gist, " ", PromiseStatus.enums.keys.sort.join(","), " ", (Kept == 1), " ", (Kept eq "Kept"), " ", (Kept === PromiseStatus::Kept), " ", Promise.kept.status.Bool, " ", Promise.new.status.Bool, " ", (Promise.kept.status ~~ Kept), " ", (Promise.kept.status ~~ Planned), " ", (Promise.kept.status == Kept), " ", (Promise.kept.status eqv Kept), " ", Promise.kept.gist, " ", Promise.broken.gist
# rakudo 2026.08: False True True Planned Kept 1 PromiseStatus::Kept X::Enum::NoValue True PromiseStatus PromiseStatus PromiseStatus::Kept Kept Broken,Kept,Planned True True True True False True False True True Promise.new(scheduler => ThreadPoolScheduler.new(uncaught_handler => Callable), status => PromiseStatus::Kept) Promise.new(scheduler => ThreadPoolScheduler.new(uncaught_handler => Callable), status => PromiseStatus::Broken)
```
rakupp 4.0.1-84: differs — `PromiseStatus` is not a declared name, so the line died at compile time; `Planned`, `Kept` and `Broken` exist as values (PR-01) but not as an enum type.

### PR-03  keep                                                          D:partial R:yes V:spec
`keep` with no argument keeps with `True`; with one argument it stores
exactly that value — Nil, Any, a type object, an Array, a Slip
(`.result.raku` shows `Nil`, `Any`, `Promise`, `[1, 2, 3]`, a `Slip`); a
promise kept with `False` is still True. `keep` returns Nil. A second `keep`
or a `break` on a promise kept through `keep` throws `X::Promise::Vowed`
(the first `keep` took the vow), whose `.promise` is the promise; the result
is unchanged. Two positional arguments are `X::Multi::NoMatch`.
```
my $p = Promise.new; say $p.keep.raku, " ", $p.status, " ", $p.result.raku, " ", $p.Bool, " ", (try $p.keep(2)) // $!.^name, " ", (try $p.break("x")) // $!.^name, " ", $p.result.raku, " ", do { my $q = Promise.new; $q.keep("kittens"); $q.result }, " ", do { my $q = Promise.new; $q.keep(Nil); $q.result.raku }, " ", do { my $q = Promise.new; $q.keep(Any); $q.result.raku }, " ", do { my $q = Promise.new; $q.keep(Promise); $q.result.^name }, " ", do { my $q = Promise.new; (try $q.keep(1, 2)) // $!.^name }, " ", do { my $q = Promise.new; my @a = 1,2,3; $q.keep(@a); $q.result.raku }, " ", do { my $q = Promise.new; $q.keep((1,2).Slip); $q.result.^name }, " ", do { my $q = Promise.new; $q.keep(False); $q.result.raku ~ ":" ~ $q.Bool }, " ", do { my $q = Promise.new; $q.keep(1); try $q.keep(2); ($!.promise === $q) }
# rakudo 2026.08: Nil Kept Bool::True True X::Promise::Vowed X::Promise::Vowed Bool::True kittens Nil Any Promise X::Multi::NoMatch [1, 2, 3] Slip Bool::False:True True
```
rakupp 4.0.1-84: differs — `X::Promise::Vowed` has no `.promise`; the line died at that last field and the rest is unmeasured.

### PR-04  break                                                         D:no R:yes V:spec
`break` with no argument breaks with an `X::AdHoc` whose payload and
message are "Died" (the docs say the cause defaults to False; Rakudo and
Roast say "Died"). A Str, an Int, Nil, an Array — anything that is not an
Exception — is wrapped in an `X::AdHoc` as its `.payload` (the message is
the payload stringified; a Nil payload stays Nil). An Exception instance is
stored as is (`.cause === $e`); an Exception TYPE object is stored as is
too, so `.cause` is then undefined. `break` returns Nil; a second `break` or
a `keep` afterwards is `X::Promise::Vowed` with `.promise`; two positionals
are `X::Multi::NoMatch`.
```
my $p = Promise.new; say $p.break.raku, " ", $p.status, " ", $p.cause.^name, " ", $p.cause.payload.raku, " ", $p.cause.message, " ", $p.Bool, " ", do { my $q = Promise.new; $q.break("glass"); $q.cause.^name ~ ":" ~ $q.cause.message ~ ":" ~ $q.cause.payload.raku }, " ", do { my $q = Promise.new; $q.break(42); $q.cause.^name ~ ":" ~ $q.cause.payload.raku ~ ":" ~ $q.cause.message }, " ", do { my $q = Promise.new; my $e = X::NYI.new(feature => "f"); $q.break($e); ($q.cause === $e) ~ ":" ~ $q.cause.^name }, " ", do { my $q = Promise.new; $q.break(Nil); $q.cause.^name ~ ":" ~ $q.cause.payload.raku }, " ", do { my $q = Promise.new; $q.break(X::NYI); $q.cause.^name ~ ":" ~ $q.cause.defined }, " ", do { my $q = Promise.new; $q.break("a"); (try $q.break("b")) // $!.^name }, " ", do { my $q = Promise.new; $q.break("a"); (try $q.keep(1)) // $!.^name }, " ", do { my $q = Promise.new; (try $q.break(1, 2)) // $!.^name }, " ", do { my $q = Promise.new; $q.break([1, 2]); $q.cause.payload.raku }, " ", do { my $q = Promise.new; $q.break("a"); try $q.break("b"); ($!.promise === $q) }
# rakudo 2026.08: Nil Broken X::AdHoc "Died" Died True X::AdHoc:glass:"glass" X::AdHoc:42:42 True:X::NYI X::AdHoc:Nil X::NYI:False X::Promise::Vowed X::Promise::Vowed X::Multi::NoMatch [1, 2] True
```
rakupp 4.0.1-84: differs — the line died at `.promise`; measured in an earlier round without that field: `.break` returns a hash-like dump and the cause's `.payload` is Any (only the message "Died" is right).

### PR-05  cause on a promise that is not broken; calls on the type object   D:partial R:yes V:spec
`.cause` on a Planned or Kept promise throws
`X::Promise::CauseOnlyValidOnBroken` with `.status` (the PromiseStatus) and
`.promise`. On the type object: `cause`, `result` and `then` are
`X::Parameter::InvalidConcreteness`, `keep` is `X::Multi::NoMatch`,
`status` and `vow` are `X::AdHoc`, `Bool` is False.
```
my $p = Promise.new; say (try $p.cause) // $!.^name, " ", $!.status, " ", $!.status.^name, " ", ($!.promise === $p), " ", $!.message, " ", do { my $q = Promise.kept(1); (try $q.cause) // $!.^name ~ ":" ~ $!.status }, " ", (try Promise.cause) // $!.^name, " ", (try Promise.result) // $!.^name, " ", (try Promise.keep) // $!.^name, " ", (try Promise.status.raku) // $!.^name, " ", (try Promise.Bool) // $!.^name, " ", (try Promise.vow) // $!.^name, " ", (try Promise.then({ 1 })) // $!.^name, " ", Promise.broken("b").cause.^name
# rakudo 2026.08: X::Promise::CauseOnlyValidOnBroken Planned PromiseStatus True Can only call cause on a broken promise (status: Planned) X::Promise::CauseOnlyValidOnBroken:Kept X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness X::Multi::NoMatch X::AdHoc False X::AdHoc X::Parameter::InvalidConcreteness X::AdHoc
```
rakupp 4.0.1-84: differs — `.cause` on a Planned promise returns Nil instead of throwing, and every type-object call is `X::Method::NotFound`.

### PR-06  result: waits, returns, or rethrows with X::Promise::Broken   D:partial R:partial V:spec
`.result` blocks until the promise is resolved (the probe keeps or breaks
it 0.2 s later from a timer). Kept: the value. Broken: it throws a COPY of
the cause with the role `X::Promise::Broken` mixed in — `.^name` is
`X::AdHoc+{X::Promise::Broken}` (`X::NYI+{X::Promise::Broken}` for a custom
type), it still smartmatches the original type, keeps `.message`,
`.payload` and the type's own attributes, carries `.result-backtrace` (a
Backtrace), and its gist begins "Tried to get the result of a broken
Promise" with an "Original exception:" section; `$!` is not `===` the
cause, which stays a plain exception. Every later `.result` throws a fresh
copy.
```
my $p = Promise.new; $p.break("oh no"); say (try $p.result) // $!.^name, " ", ($! ~~ X::AdHoc), " ", ($! ~~ X::Promise::Broken), " ", $!.message, " ", $!.payload.raku, " ", $!.result-backtrace.^name, " ", ($! === $p.cause), " ", $p.cause.^name, " ", $!.gist.lines[0], " ", $!.gist.lines.grep(/Original/).elems, " ", do { (try $p.result) // $!.^name }, " ", do { my $q = Promise.broken(X::NYI.new(feature => "nyi")); ((try $q.result) // $!.^name) ~ ":" ~ ($! ~~ X::NYI) ~ ":" ~ $!.feature }, " ", do { my $q = Promise.kept(5); $q.result.raku }, " ", do { my $q = Promise.new; Promise.in(0.2).then({ $q.keep("late") }); $q.result }, " ", do { my $q = Promise.new; Promise.in(0.2).then({ $q.break("late-b") }); ((try $q.result) // $!.^name) ~ ":" ~ $!.message }
# rakudo 2026.08: X::AdHoc+{X::Promise::Broken} True True oh no "oh no" Backtrace False X::AdHoc Tried to get the result of a broken Promise 1 X::AdHoc+{X::Promise::Broken} X::NYI+{X::Promise::Broken}:True:nyi 5 late X::AdHoc+{X::Promise::Broken}:late-b
```
rakupp 4.0.1-84: differs — the line died at `.result-backtrace`, the error naming the invocant a plain `X::AdHoc` (no role mixed in); the rest is unmeasured.

### PR-07  keep binds the variable, not its value                        D:no R:no V:quirk
`$p.keep($x)`, `Promise.kept($z)` and a `start { $y }` block all bind the
promise's result to the CONTAINER passed or returned, so an assignment to
that variable after the keep changes what `.result` and `await` return
afterwards (6, 8, 10 in the probe: the variables were 5, 7, 9 when kept). A
value already read from `.result` is unaffected (11); assigning to
`.result` is `X::Assignment::RO`; a `.then` result, `$v.clone`, `+$v` or
`$v + 0` is a plain value. Roast never assigns after keeping and nothing
depends on this; keep values.
```
my $x = 5; my $p = Promise.new; $p.keep($x); $x = 6; my @a = 1,2; my $q = Promise.new; $q.keep(@a); @a.push(3); my $y = 7; my $s = start { $y }; await $s; $y = 8; my $z = 9; my $t = Promise.kept($z); $z = 10; my $w = 11; my $u = Promise.new; $u.keep($w); my $got = $u.result; $w = 12; say $p.result, " ", $q.result.raku, " ", $s.result, " ", $t.result, " ", $got, " ", (await $p), " ", (try { $p.result = 1; "assigned" }) // $!.^name, " ", (try { $s.result = 1; "assigned" }) // $!.^name, " ", do { my $v = 1; my $r = Promise.new; $r.keep($v); my $t2 = $r.then({ .result }); await $t2; $v = 2; $t2.result }, " ", do { my $v = 1; my $r = Promise.new; $r.keep($v.clone); $v = 2; $r.result }, " ", do { my $v = 1; my $r = Promise.new; $r.keep(+$v); $v = 2; $r.result }, " ", do { my $v = 1; my $r = start { $v + 0 }; await $r; $v = 2; $r.result }
# rakudo 2026.08: 6 [1, 2, 3] 8 10 11 6 X::Assignment::RO X::Assignment::RO 1 1 1 1
```
rakupp 4.0.1-84: differs, by decision — copies the value at keep time (5, 7, 9), the saner behaviour; keep it.

### PR-08  vow                                                            D:yes R:yes V:spec
`.vow` returns a `Promise::Vow` — a lexical class, not reachable by name
(`::("Promise::Vow")` is `X::NoSuchSymbol`, `Promise.WHO` lacks it) — with
`.promise`, `.keep(value)` and `.break(value)`, both returning Nil. Once the
vow is taken, `.vow` again and the promise's own `keep`/`break` throw
`X::Promise::Vowed` (`.promise`; "Access denied to keep/break this Promise;
already vowed"). Keeping or breaking through the vow a second time throws
`X::Promise::Resolved` (`.promise`; "Cannot keep/break a Promise more than
once (status: Kept)" or "(status: Broken)", both asserted by Roast) and
leaves result and status unchanged. `$vow.keep` without an argument is
`X::AdHoc`.
```
my $p = Promise.new; my $v = $p.vow; say $v.^name, " ", ($v.promise === $p), " ", (try $p.vow) // $!.^name, " ", ($!.promise === $p), " ", $!.message, " ", (try $p.keep(1)) // $!.^name, " ", (try $p.break(1)) // $!.^name, " ", $p.status, " ", $v.keep(1).raku, " ", $p.status, " ", $p.result, " ", (try $v.keep(2)) // $!.^name, " ", $!.message, " ", ($!.promise === $p), " ", (try $v.break(3)) // $!.^name, " ", $p.result, " ", do { my $q = Promise.new; my $w = $q.vow; $w.break(1); $q.cause.payload ~ " " ~ ((try $w.break(2)) // $!.message) }, " ", (Promise.WHO{$v.^name}:exists), " ", (try ::("Promise::Vow").^name) // $!.^name, " ", (try $v.keep) // $!.^name
# rakudo 2026.08: Promise::Vow True X::Promise::Vowed True Access denied to keep/break this Promise; already vowed X::Promise::Vowed X::Promise::Vowed Planned Nil Kept 1 X::Promise::Resolved Cannot keep/break a Promise more than once (status: Kept) True X::Promise::Resolved 1 1 Cannot keep/break a Promise more than once (status: Broken) False X::NoSuchSymbol X::AdHoc
```
rakupp 4.0.1-84: differs — the vow is a `Vow` without `.promise`; the line died there and the rest is unmeasured.

### PR-09  kept/broken factories; which promises can still be kept      D:partial R:yes V:spec
`Promise.kept` is Kept with True, `Promise.kept($v)` with the value;
`Promise.broken` is Broken with `X::AdHoc` payload "Died",
`Promise.broken("glass")` with that payload, `Promise.broken($exception)`
with the exception itself; their scheduler is `$*SCHEDULER`. Which
exception a later `keep`/`break` throws tells who holds the vow: on a
`Promise.kept`/`Promise.broken` promise it is `X::Promise::Resolved` (no
vow was taken — `.vow` on them even succeeds), on a promise from
`Promise.in`, `start`, `.then`, `anyof`/`allof` (the empty list included)
it is `X::Promise::Vowed`. Two positionals to `kept` are `X::Multi::NoMatch`.
```
my $k = Promise.kept; say $k.status, " ", $k.result.raku, " ", $k.Bool, " ", Promise.kept("v").result, " ", do { my $b = Promise.broken; $b.status ~ " " ~ $b.cause.^name ~ " " ~ $b.cause.payload.raku ~ " " ~ $b.cause.message }, " ", Promise.broken("glass").cause.message, " ", Promise.broken(X::NYI.new(feature => "x")).cause.^name, " ", (try $k.keep(1)) // $!.^name, " ", (try Promise.broken.keep(1)) // $!.^name, " ", (try Promise.in(5).keep(1)) // $!.^name, " ", (try (start { 1 }).keep(1)) // $!.^name, " ", (try Promise.anyof(Promise.new).keep) // $!.^name, " ", (try Promise.allof().keep) // $!.^name, " ", (try Promise.kept.vow.^name) // $!.^name, " ", $k.scheduler.^name, " ", (try Promise.kept(1, 2)) // $!.^name, " ", (try Promise.kept.then({ 1 }).keep) // $!.^name, " ", Promise.broken(42).cause.payload.raku, " ", (Promise.kept.result === True)
# rakudo 2026.08: Kept Bool::True True v Broken X::AdHoc "Died" Died glass X::NYI X::Promise::Resolved X::Promise::Resolved X::Promise::Vowed X::Promise::Vowed X::Promise::Vowed X::Promise::Vowed Promise::Vow ThreadPoolScheduler X::Multi::NoMatch X::Promise::Vowed 42 True
```
rakupp 4.0.1-84: differs — the line died at the fifth field, `Promise.broken.cause.payload`, the cause being a Bool; the rest is unmeasured.

### PR-10  eqv, ===, Str                                                  D:no R:no V:quirk
`eqv` on two Promises is identity OR `eqv` of their results: it therefore
BLOCKS on a Planned promise (the probe's `start { $p eqv $q }` is still
Planned after a second) and rethrows `X::AdHoc+{X::Promise::Broken}` when
either side is broken; two kept promises with `eqv` results are `eqv`
whatever their identity; a Promise is never `eqv` a non-Promise. `===` is
identity; `.Str` is `Promise<address>` so `eq` holds only for the same
object; `.WHICH` is an ObjAt; `==` and `+` are `X::Multi::NoMatch`.
```
say (Promise.kept(1) eqv Promise.kept(1)), " ", (Promise.kept(1) eqv Promise.kept(2)), " ", (Promise.kept(1) eqv Promise.kept("1")), " ", do { my $p = Promise.new; ($p eqv $p) ~ " " ~ ($p === $p) }, " ", (Promise.kept(1) === Promise.kept(1)), " ", (try (Promise.broken("x") eqv Promise.kept(1)).raku) // $!.^name, " ", (try (Promise.kept(1) eqv Promise.broken("x")).raku) // $!.^name, " ", (Promise.kept(1) eqv 1), " ", (Promise eqv Promise), " ", do { my $p = Promise.new; my $q = Promise.new; my $r = start { $p eqv $q }; await Promise.anyof($r, Promise.in(1)); $r.status }, " ", (try (Promise.kept(1) == Promise.kept(1))) // $!.^name, " ", (try +Promise.kept(1)) // $!.^name, " ", Promise.kept(1).Str.subst(/\d+/, "N"), " ", (Promise.kept(1) eq Promise.kept(1)), " ", do { my $p = Promise.kept(1); $p eq $p }, " ", Promise.kept(1).WHICH.^name
# rakudo 2026.08: True False False True True False X::AdHoc+{X::Promise::Broken} X::AdHoc+{X::Promise::Broken} False True Planned X::Multi::NoMatch X::Multi::NoMatch Promise<N> False True ObjAt
```
rakupp 4.0.1-84: differs — `eqv` with a broken side answers False, `eqv` of two Planned promises returns at once (Kept), `==` answers True and `+` answers 2, `.Str` is a hash dump, `.WHICH` a ValueObjAt.

## B. Chaining

### PR-11  then                                                          D:yes R:yes V:spec
`.then(&code)` returns a new Planned Promise (not the original, of the same
class, with the original's scheduler) whose code runs after the original
resolves — not before — receiving the original promise as its one argument;
its return value keeps the new promise (a List value stays a List). It runs
for a Broken original too (the code reads `.status` and `.cause`). If the
code dies, the new promise is Broken with that exception as its plain
`.cause`, and `.result` rethrows it with `X::Promise::Broken`. On an already
resolved promise the code is scheduled at once. Several thens on one
promise all run; `$*PROMISE` inside the code is the new promise. A
non-Callable argument is `X::TypeCheck::Binding::Parameter`; a block that
cannot take one argument breaks the new promise with `X::AdHoc`;
`Promise.then` on the type object is `X::Parameter::InvalidConcreteness`.
```
my $p = Promise.new; my $ran = 0; my $t = $p.then(-> $r { $ran = 1; ($r === $p) ~ ":" ~ $r.status ~ ":" ~ $r.result }); say $t.^name, " ", ($t === $p), " ", $t.status, " ", $ran.Str, " ", $p.keep(42).raku, " ", $t.result, " ", $t.status, " ", $ran.Str, " ", do { my $q = Promise.new; my $u = $q.then({ .status ~ ":" ~ .cause.message }); $q.break("we fail"); $u.result ~ " " ~ $u.status }, " ", do { my $q = Promise.new; my $u = $q.then({ die "then died" }); $q.keep(1); ((try $u.result) // $!.^name) ~ " " ~ $u.status ~ " " ~ $u.cause.message ~ " " ~ $u.cause.^name }, " ", do { my $q = Promise.kept(7); my $u = $q.then({ .result * 2 }); $u.^name ~ " " ~ $u.result }, " ", do { my $q = Promise.broken("b"); $q.then({ .status }).result }, " ", (try Promise.kept(1).then(42)) // $!.^name, " ", do { my $q = Promise.kept(1); my $u = $q.then({ Nil }); $u.result.raku }, " ", do { my $q = Promise.kept(1); ((try $q.then(-> $a, $b { 1 }).result) // $!.^name) }, " ", do { my $q = Promise.new; my $u = $q.then({ 1 }); my $w = $q.then({ 2 }); $q.keep; (await $u, $w).raku }, " ", do { my $q = Promise.new; my $u = $q.then({ .result }); $q.keep((1, 2)); $u.result.raku }, " ", do { my $q = Promise.kept(1); my $u = $q.then({ $*PROMISE }); (await $u) === $u }, " ", (try Promise.then({ 1 })) // $!.^name, " ", do { my $q = Promise.new; my $u = $q.then({ .result }); $q.keep(1); $u.scheduler === $q.scheduler }
# rakudo 2026.08: Promise False Planned 0 Nil True:Kept:42 Kept 1 Broken:we fail Kept X::AdHoc+{X::Promise::Broken} Broken then died X::AdHoc Promise 14 Broken X::TypeCheck::Binding::Parameter Nil X::AdHoc+{X::Promise::Broken} (1, 2) (1, 2) True X::Parameter::InvalidConcreteness True
```
rakupp 4.0.1-84: differs — the line died at `.scheduler`, its last field; measured in an earlier round without it: the common cases match, `keep` returns a hash dump, and the derived promise's rethrow lacks the role.

### PR-12  then with :synchronous                                         D:no R:no V:bug
`:synchronous` runs the code on the thread that keeps or breaks the
promise, inside that `keep`/`break` call, in registration order, and
returns a Planned Promise for it — on a resolving promise. On an ALREADY
resolved promise `.then(&code, :synchronous)` — and `andthen`/`orelse`
likewise — returns not a Promise but the code's return value (1, "q"), and
a death propagates to the caller as a plain `X::AdHoc`. The bug: when the
code of a synchronous then dies while the promise was still Planned —
including the documented pattern of reading `.result` of a promise that
turns out Broken — `keep`/`break` throws an unrelated `X::AdHoc` "Too few
positionals passed; expected 1 argument but got 0", the original promise IS
resolved, the then-promise stays Planned forever, and every synchronous
then registered after it never runs. Do not imitate: break the then-promise
with the exception. `$*PROMISE` is the then-promise here too, and the code
runs on the resolving thread (`$*THREAD.id` equal).
```
my @o; my $q = Promise.new; $q.then({ @o.push(1) }, :synchronous); $q.then({ @o.push(2) }, :synchronous); $q.keep; say @o.raku, " ", Promise.kept(1).then({ .result }, :synchronous).raku, " ", do { my $p = Promise.new; my $u = $p.then({ .result + 1 }, :synchronous); $u.^name ~ " " ~ $u.status ~ " " ~ do { $p.keep(1); $u.status ~ " " ~ $u.result } }, " ", (try Promise.kept(1).then({ die "sd" }, :synchronous)) // $!.^name, " ", do { my $p = Promise.new; my $u = $p.then({ die "pd" }, :synchronous); my $w = $p.then({ "third" }, :synchronous); ((try $p.keep(1)) // $!.^name ~ ":" ~ $!.message.substr(0, 30)) ~ ":" ~ $p.status ~ ":" ~ do { await Promise.anyof($u, Promise.in(0.5)); $u.status ~ ":" ~ $w.status } }, " ", do { my $p = Promise.new; my $t = $p.then({ $*PROMISE === $p }, :synchronous); $p.keep; await $t }, " ", Promise.broken("q").orelse({ .cause.message }, :synchronous).raku, " ", Promise.kept(1).andthen({ .result }, :synchronous).raku, " ", do { my $p = Promise.new; my $u = $p.then({ .result }, :synchronous); ((try { $p.break("sb"); "broke" }) // $!.^name) ~ ":" ~ $p.status ~ ":" ~ $u.status }, " ", do { my $p = Promise.new; my $u = $p.then({ $*THREAD.id }, :synchronous); $p.keep; $u.result == $*THREAD.id }
# rakudo 2026.08: [1, 2] 1 Promise Planned Kept 2 X::AdHoc X::AdHoc:Too few positionals passed; ex:Kept:Planned:Planned False "q" 1 X::AdHoc:Broken:Planned True
```
rakupp 4.0.1-84: differs — the line died at `orelse`, which is missing; measured in an earlier round: `:synchronous` on a resolved promise returns a hash dump.

### PR-13  andthen and orelse                                             D:no R:yes V:spec
`andthen(&code)`: on Kept runs the code (receiving the promise) and the new
promise takes its value; on Broken the code does not run and the new
promise is a fresh Broken promise with the SAME cause object. `orelse(&code)`:
on Broken runs the code; on Kept the new promise is a fresh Kept promise
with the same result. Both work whether the promise is resolved already or
later, a dying code breaks the new promise, and chains compose as Roast's
then.t asserts (`K:30` for keep 1 → +1 → +1 → orelse → ×10). Neither method
is on docs.raku.org.
```
my sub R($p) { try $p.result; $p.status == Kept ?? "K:" ~ $p.result !! "B:" ~ $p.cause.message }; say R(Promise.kept(2).andthen({ .result * 10 })), " ", R(Promise.broken("bad").andthen({ 1 })), " ", R(Promise.kept(2).orelse({ 1 })), " ", R(Promise.broken("bad").orelse({ .cause.message.uc })), " ", R(Promise.kept(1).andthen({ die "ad" })), " ", R(Promise.broken("x").orelse({ die "od" })), " ", do { my $p = Promise.new; my $a = $p.andthen({ .result + 1 }); my $o = $p.orelse({ "no" }); $p.keep(5); R($a) ~ " " ~ R($o) }, " ", do { my $p = Promise.new; my $a = $p.andthen({ .result + 1 }); my $o = $p.orelse({ "no:" ~ .cause.message }); $p.break("m"); R($a) ~ " " ~ R($o) }, " ", Promise.kept(2).andthen({ 1 }).^name, " ", (Promise.broken("z").andthen({ 1 }).cause.^name), " ", do { my $b = Promise.broken("z"); my $a = $b.andthen({ 1 }); ($a === $b) ~ ":" ~ ($a.cause === $b.cause) }, " ", do { my $k = Promise.kept(3); my $o = $k.orelse({ 1 }); ($o === $k) ~ ":" ~ $o.result }, " ", Promise.kept(1).andthen({ .result }, :synchronous).raku, " ", Promise.broken("q").orelse({ .cause.message }, :synchronous).raku, " ", R(Promise.kept(1).andthen({ .result + 1 }).andthen({ .result + 1 }).orelse({ 0 }).then({ .result * 10 }))
# rakudo 2026.08: K:20 B:bad K:2 K:BAD B:ad B:od K:6 K:5 B:m K:no:m Promise X::AdHoc False:True False:3 1 "q" K:30
```
rakupp 4.0.1-84: differs — neither method exists (died at the first field).

### PR-14  Supply of a Promise; await on a Supply                         D:partial R:partial V:spec
`.Supply` is an on-demand (not live) Supply that, per tap, emits the result
and is done once the promise is Kept, or quits with the cause once it is
Broken — after the fact for an already resolved promise, later otherwise;
`.Supply.list` is `(result,)` and a second tap replays. `await` on a Supply
returns the LAST value emitted before `done` (3 for `from-list(1, 2, 3)`, 2
for emit 1, emit 2, done), Nil for a supply that is done without emitting,
and rethrows a quit with the `X::Await::Died` role (PR-20). Supply's own
await handle was not read; this is recorded as observed.
```
say Promise.kept(5).Supply.^name, " ", Promise.kept(5).Supply.list.raku, " ", do { my @e; my $s = Promise.broken("bp").Supply; $s.tap({ @e.push($_) }, done => { @e.push("done") }, quit => { @e.push("quit:" ~ .message) }); await Promise.in(0.3); @e.raku }, " ", do { my $p = Promise.new; my @e; $p.Supply.tap({ @e.push($_) }, done => { @e.push("done") }); $p.keep("v"); await Promise.in(0.3); @e.raku }, " ", (await Supply.from-list(1, 2, 3)), " ", (await supply { emit 1; emit 2; done }), " ", (try (await supply { done }).raku) // $!.^name, " ", (try await supply { emit 1; die "sq" }) // $!.^name, " ", $!.message, " ", Promise.kept(5).Supply.live, " ", do { my $s = Promise.kept(1).Supply; ($s.list.raku, $s.list.raku).join(",") }
# rakudo 2026.08: Supply (5,) ["quit:bp"] ["v", "done"] 3 2 Nil X::AdHoc+{X::Await::Died} sq False (1,),(1,)
```
rakupp 4.0.1-84: differs — the Supply emits a hash-like `(:result(5), :status("Kept"))` instead of the result, calls `done` instead of `quit` for a broken promise and emits at tap time for a Planned one; `await` on a Supply matches except that the quit lacks the role.

### PR-15  Dynamic variables, $*PROMISE, fresh $/ and $!                  D:no R:yes V:spec
Code run by `start` and by `.then` sees the dynamic variables of the place
where the promise (or the then) was created and can modify them (`$*F`
becomes "abc" across a start and its then; `$*A++` in a start changes the
outer `$*A`; a sub called inside sees them; a `$*` declared inside a start
is seen by a nested start and its then). `$*PROMISE` is the promise whose
code is running — the start's own promise; inside a then, the then-promise,
still Planned while its code runs — and is `X::Dynamic::NotFound` outside
any promise. `$/` and `$!` are fresh (Nil) inside a start block.
```
say do { my $*F = "a"; await (start { $*F ~= "b" }).then({ $*F ~= "c" }); $*F }, " ", do { my $t = start { $*PROMISE.^name }; await $t }, " ", do { my $p = Promise.kept(1); my $t = $p.then({ $*PROMISE }); (await $t) === $t }, " ", do { my $p = Promise.new; my $t = $p.then({ $*PROMISE === $p }); $p.keep; await $t }, " ", ((try $*PROMISE.raku) // $!.^name), " ", do { my $/ = 42; my $! = X::NYI.new(feature => "f"); (await start { ($/.raku, $!.raku).join(":") }) }, " ", do { my $x = 0; await start { $x = 1 }; $x }, " ", do { sub t { $*d }; my $*d = 1; await start { t() } }, " ", do { my $*A = 42; await start { $*A++ }; $*A }, " ", do { await start { my $*IN-START = 1; await start { $*IN-START // "none" } } }, " ", do { my $p = Promise.new; my $t = $p.then({ $*PROMISE === $p }, :synchronous); $p.keep; await $t }, " ", do { my $p = Promise.kept; await $p.then({ ($*PROMISE.status, $*PROMISE.^name).join(":") }) }, " ", do { my @o; await start { my $*Q = "in"; await (start { @o.push($*Q // "none") }).then({ @o.push($*Q // "none") }) }; @o.raku }
# rakudo 2026.08: abc Promise True False X::Dynamic::NotFound Nil:Nil 1 1 43 1 False Planned:Promise ["in", "in"]
```
rakupp 4.0.1-84: differs — the line died at `$*PROMISE.status`, the error naming `$*PROMISE` an Any inside a then; the rest is unmeasured.

## C. Factories and start

### PR-16  Promise.in and Promise.at                                      D:partial R:yes V:spec
Both return a Planned promise that is kept with `True` no earlier than the
delay (0.3 s measured at ≥ 0.25 s; timing-dependent). A zero or negative
delay, `-Inf`, and a time in the past keep it right away; `Inf` never keeps
it (still Planned after 0.5 s); `NaN` throws `X::Scheduler::CueInNaNSeconds`
at the call. Accepted delays: Int, Rat, Num, Duration and a numeric Str
("0.1"); a non-numeric Str is `X::TypeCheck::Binding::Parameter`; `Any` is
treated as 0 with a "Use of uninitialized value" warning. `.at` takes an
Instant, a plain number of seconds since the epoch, a DateTime, or a Date.
The promise cannot be kept by hand (`X::Promise::Vowed`); no argument, or
two positionals, is `X::AdHoc`.
```
my $t0 = now; my $p = Promise.in(0.3); say $p.^name, " ", $p.status, " ", $p.result, " ", (now - $t0 >= 0.25), " ", Promise.in(-1).result, " ", Promise.in(0).result, " ", Promise.at(now - 1000).result, " ", Promise.at(now + 0.1).result, " ", Promise.in(1/10).result, " ", Promise.in(0.1e0).result, " ", Promise.in(Duration.new(0.1)).result, " ", (try Promise.in("0.1").result) // $!.^name, " ", (try Promise.in("abc").result) // $!.^name, " ", (try Promise.in(NaN)) // $!.^name, " ", (try Promise.at(NaN)) // $!.^name, " ", Promise.in(Inf).status, " ", Promise.in(-Inf).result, " ", do { my $q = Promise.in(Inf); await Promise.anyof($q, Promise.in(0.5)); $q.status }, " ", (try Promise.at(DateTime.now + Duration.new(0.1)).result) // $!.^name, " ", Promise.at(now.Num - 10).result, " ", do { my @w; CONTROL { when CX::Warn { @w.push(.message.substr(0, 25)); .resume } }; my $q = Promise.in(Any); await Promise.anyof($q, Promise.in(1)); $q.status ~ ":" ~ @w.raku }, " ", Promise.in(0.1).result.^name, " ", (try Promise.in(0.1).keep) // $!.^name, " ", (try Promise.in()) // $!.^name, " ", (try Promise.at(now, 1)) // $!.^name, " ", (try Promise.at(Date.today).result) // $!.^name
# rakudo 2026.08: Promise Planned True True True True True True True True True True X::TypeCheck::Binding::Parameter X::Scheduler::CueInNaNSeconds X::Scheduler::CueInNaNSeconds Planned True Planned True True Kept:["Use of uninitialized valu"] Bool X::Promise::Vowed X::AdHoc X::AdHoc True
```
rakupp 4.0.1-84: differs — a non-numeric Str delay is accepted, and `NaN` yields a Planned promise (printed as a timer dump) instead of throwing; the line died there.

### PR-17  anyof and allof                                                D:partial R:yes V:spec
Both return a new Planned Promise of the same class that is kept with
`True` — `anyof` once ANY input is Kept or Broken, `allof` once ALL inputs
are; a broken input counts like a kept one and `.result` of the combined
promise never throws. The combined promise is kept on the scheduler, so
read it through `.result` or `await`, not `.status` right after an input
resolved. No arguments, or an empty array, gives a promise that is already
Kept. Arguments are flattened (`[…]`, nested lists). Any argument that is
not a defined Promise — 42, the type object, a Channel, a Junction — throws
`X::Promise::Combinator` whose `.combinator` is "anyof" or "allof". The
combined promise's vow is taken (`X::Promise::Vowed` on `keep`). `anyof`
with a `Promise.in` is the timeout idiom: the probe's Planned promise is
still Planned after the timer wins.
```
my $p1 = Promise.new; my $p2 = Promise.new; my $any = Promise.anyof($p1, $p2); my $all = Promise.allof($p1, $p2); say $any.^name, " ", $any.status, " ", $all.status, " ", do { $p1.keep(1); $any.result ~ ":" ~ $any.status }, " ", $all.status, " ", do { $p2.break("bad"); $all.result ~ ":" ~ $all.status }, " ", Promise.allof().status, " ", Promise.anyof().status, " ", Promise.allof(my @e).result, " ", (try Promise.allof(42)) // $!.^name, " ", $!.combinator, " ", (try Promise.anyof(Promise.kept, 42)) // $!.^name ~ ":" ~ $!.combinator, " ", (try Promise.allof(Promise)) // $!.^name, " ", (try Promise.anyof(Channel.new)) // $!.^name, " ", Promise.allof([Promise.kept, Promise.kept]).result, " ", Promise.allof((Promise.kept, Promise.kept), Promise.kept).result, " ", Promise.anyof(Promise.broken("x")).result, " ", (try Promise.anyof(Promise.kept).keep) // $!.^name, " ", (try Promise.anyof().keep) // $!.^name, " ", Promise.allof(Promise.kept).result.raku, " ", do { my $q = Promise.new; my $a = Promise.anyof($q, Promise.in(0.3)); $a.result ~ ":" ~ $q.status }, " ", do { my $q = Promise.kept; my $a = Promise.allof($q); await $a; $a.status }, " ", (try Promise.allof(any(Promise.kept, Promise.kept)).result.raku) // $!.^name
# rakudo 2026.08: Promise Planned Planned True:Kept Planned True:Kept Kept Kept True X::Promise::Combinator allof X::Promise::Combinator:anyof X::Promise::Combinator X::Promise::Combinator True True True X::Promise::Vowed X::Promise::Vowed Bool::True True:Planned Kept X::Promise::Combinator
```
rakupp 4.0.1-84: differs — `X::Promise::Combinator` has no `.combinator`; the line died there and the rest is unmeasured.

### PR-18  start and Promise.start                                        D:partial R:yes V:spec
`start` (a statement prefix taking a block or a bare expression: `start
6 * 7`, `start (1, 2, 3)`) and `Promise.start(&code)` return a Planned
Promise kept with the code's value (a lazy Seq stays a Seq, Nil stays Nil)
or Broken with the exception when the code dies — `.cause` is the plain
exception, `.result` rethrows it with `X::Promise::Broken`, `await` with
`X::Await::Died`. A `fail` breaks it likewise. A `CATCH` inside that handles
the death keeps the promise with Nil; `.resume` lets the block finish
normally. `return` inside a start block breaks it with
`X::ControlFlow::Return`. `Promise.start(&code, args…)` passes the extra
positionals to the code; `:catch(&h)` calls `h($exception)` before
breaking; a non-Callable is `X::TypeCheck::Binding::Parameter`. The code
runs on another thread (`$*THREAD.id` differs), `$*PROMISE` inside is the
promise, and a warning inside stays inside. `await start { 1 } andthen 2`
is `await (start { 1 } andthen 2)`, so 2.
```
my $p = start { 42 }; say $p.^name, " ", (await $p), " ", $p.status, " ", (await start 6 * 7), " ", (await start (1, 2, 3)).raku, " ", (await start { (0..3).map(* + 1) }).raku, " ", do { my $q = start { die "trying" }; ((try $q.result) // $!.^name) ~ " " ~ $q.status ~ " " ~ $q.cause.message ~ " " ~ $q.cause.^name }, " ", do { my $q = start { fail "ff" }; ((try await $q) // $!.^name) ~ ":" ~ $q.status ~ ":" ~ $q.cause.^name }, " ", (await Promise.start(-> $a, $b { $a + $b }, 3, 4)), " ", do { my @c; my $q = Promise.start({ die "cc" }, :catch({ @c.push(.message) })); (try await $q) // $!.^name; @c.raku ~ ":" ~ $q.status }, " ", (await start { die "x"; CATCH { default { "caught" } } }).raku, " ", do { my $q = start { return 5 }; ((try await $q) // $!.^name) }, " ", do { my $q = start { $*PROMISE }; (await $q) === $q }, " ", (await start { $*THREAD.^name }), " ", ((await start { $*THREAD.id }) != $*THREAD.id), " ", (await start { 1 } andthen 2), " ", (await start { Nil }).raku, " ", (start { sleep 0.2; 1 }).status, " ", (try Promise.start(42)) // $!.^name, " ", (await Promise.start({ 7 })), " ", do { my @w; my $q = start { CONTROL { when CX::Warn { @w.push(.message); .resume } }; warn "w"; 3 }; (await $q) ~ ":" ~ @w.raku }, " ", do { my $q = start { die "x"; CATCH { default { .resume } }; "resumed" }; ((try await $q) // $!.^name) }, " ", (await start { my $x = 1; $x + 1 }), " ", (try Promise.start({ 1 }).scheduler.^name) // $!.^name
# rakudo 2026.08: Promise 42 Kept 42 (1, 2, 3) (1, 2, 3, 4).Seq X::AdHoc+{X::Promise::Broken} Broken trying X::AdHoc X::AdHoc+{X::Await::Died}:Broken:X::AdHoc 7 ["cc"]:Broken Nil X::ControlFlow::Return+{X::Await::Died} True Thread True 2 Nil Planned X::TypeCheck::Binding::Parameter 7 3:["w"] resumed 2 ThreadPoolScheduler
```
rakupp 4.0.1-84: differs — `start 6 * 7` yields 7, `fail` keeps the promise (with Nil), the extra positionals are not passed (0), `:catch` is not called, `return` inside a start returns 5, `$*PROMISE` is not the promise, both roles are missing, and `.scheduler` is missing.

### PR-19  A broken start promise in sink context ends the program       D:partial R:yes V:spec
A `start` block used as a statement (sunk) whose code dies prints
"Unhandled exception in code scheduled on thread N" followed by the
exception's message to stderr and terminates the program with exit code 1
at the moment of the death (0.3 s later here; nothing after that runs).
This belongs to the `start` prefix in v6.d and later (`use v6.c` turns it
off), applies when the start is sunk inside a sub, and to
`Promise.start(&code, :report-broken-if-sunk)`; a plain `Promise.start(…)`
sunk, a start assigned to a variable (even `my $ =`), a start with a
`CATCH` that handles, a successful start, and a start whose promise had a
`.then` attached are silent, and a bare `$p;` statement is a compile-time
"Useless use" warning that never sinks the promise. With
`$*SCHEDULER.uncaught_handler` set, the handler receives the exception and
the program continues (exit 0). Each field is `exitcode:stdout:stderr` of
a child `$*EXECUTABLE -e`, the first two stderr lines joined by `/`.
```
my sub Rn($code) { my $r = run($*EXECUTABLE, "-e", $code, :out, :err); $r.exitcode ~ ":" ~ $r.out.slurp(:close).raku ~ ":" ~ $r.err.slurp(:close).lines.head(2).join("/").subst(/"thread " \d+/, "thread N").raku }; say Rn('start { die "boom" }; sleep 0.3; say "after"'), " ", Rn('my $ = start { die "boom" }; sleep 0.3; say "after"'), " ", Rn('Promise.start({ die "boom" }); sleep 0.3; say "after"'), " ", Rn('use v6.c; start { die "boom" }; sleep 0.3; say "after"'), " ", Rn('Promise.start({ die "boom" }, :report-broken-if-sunk); sleep 0.3; say "after"')
# rakudo 2026.08: 1:"":"Unhandled exception in code scheduled on thread N/boom" 0:"after\n":"" 0:"after\n":"" 0:"after\n":"" 1:"":"Unhandled exception in code scheduled on thread N/boom"
my sub Rn($code) { my $r = run($*EXECUTABLE, "-e", $code, :out, :err); $r.exitcode ~ ":" ~ $r.out.slurp(:close).raku ~ ":" ~ $r.err.slurp(:close).lines.head(2).join("/").subst(/"thread " \d+/, "thread N").raku }; say Rn('start { die "boom"; CATCH { default { say "caught" } } }; sleep 0.3; say "after"'), " ", Rn('$*SCHEDULER.uncaught_handler = -> $ex { say "H:" ~ $ex.message }; start { die "boom" }; sleep 0.3; say "after"'), " ", Rn('sub f { start { die "far" } }; f(); sleep 0.3; say "after"'), " ", Rn('my $p = start { die "x" }; $p.then({ 1 }); $p; sleep 0.3; say "after"'), " ", Rn('start { 1 }; sleep 0.1; say "ok"'), " ", Rn('my $p = start { die "y" }; sleep 0.3; $p; say "after"')
# rakudo 2026.08: 0:"caught\nafter\n":"" 0:"H:boom\nafter\n":"" 1:"":"Unhandled exception in code scheduled on thread N/far" 0:"after\n":"WARNINGS for -e:/Useless use of \$p in sink context (line 1)" 0:"ok\n":"" 0:"after\n":"WARNINGS for -e:/Useless use of \$p in sink context (line 1)"
```
rakupp 4.0.1-84: differs — no report and exit 0 in every case, `:report-broken-if-sunk` and the uncaught handler included (the handler is honoured for `cue`, PR-35, but not here).

## D. await

### PR-20  await on one Promise; X::Await::Died                           D:no R:yes V:spec
`await $p` blocks until resolved and returns the result as it is (Nil, a
Slip, an Array; a Slip assigned to an array flattens). On a Broken promise
it rethrows a COPY of the cause with the role `X::Await::Died` mixed in:
`.^name` `X::AdHoc+{X::Await::Died}` (`X::T+{X::Await::Died}` for a custom
type, which still smartmatches `X::T`), `.does(X::Await::Died)`, `.message`
intact, `.await-backtrace` a Backtrace, gist starting "An operation first
awaited:"; `$!` is not `===` the cause. An exception that already carries
the role is rethrown unchanged (a nested `await start { await $p }` shows
it once). `.result` and `await` of one broken promise wear different roles
(`X::Promise::Broken`, `X::Await::Died`).
```
say (await Promise.kept(3)), " ", do { my $p = Promise.broken("oh"); ((try await $p) // $!.^name) ~ " " ~ ($! ~~ X::AdHoc) ~ " " ~ $!.does(X::Await::Died) ~ " " ~ $!.message ~ " " ~ $!.await-backtrace.^name ~ " " ~ $!.gist.lines[0] ~ " " ~ ($! === $p.cause) ~ " " ~ $p.cause.^name }, " ", do { class X::T is Exception { has $.message }; my $p = Promise.broken(X::T.new(message => "crumbs")); ((try await $p) // $!.^name) ~ " " ~ ($! ~~ X::T) ~ " " ~ $!.message }, " ", do { my $p = Promise.broken("in"); ((try await start { await $p }) // $!.^name) ~ " " ~ $!.message }, " ", do { my $p = Promise.broken("x"); ((try $p.result) // $!.^name) ~ " " ~ ((try await $p) // $!.^name) }, " ", (await Promise.kept(Nil)).raku, " ", (await Promise.kept((1, 2).Slip)).raku, " ", do { my @a = await Promise.kept((1, 2).Slip); @a.elems }, " ", (await Promise.kept([1, 2])).raku, " ", (await start { (1, 2).Slip }).^name, " ", do { my $p = Promise.new; Promise.in(0.2).then({ $p.keep("later") }); await $p }
# rakudo 2026.08: 3 X::AdHoc+{X::Await::Died} True True oh Backtrace An operation first awaited: False X::AdHoc X::T+{X::Await::Died} True crumbs X::AdHoc+{X::Await::Died} in X::AdHoc+{X::Promise::Broken} X::AdHoc+{X::Await::Died} Nil slip(1, 2) 2 [1, 2] Slip later
```
rakupp 4.0.1-84: differs — the line died at `$!.message`, the error naming `$!` a Str after the failed `await`, not an Exception; the rest is unmeasured.

### PR-21  await on lists, empty lists and non-awaitables                 D:partial R:yes V:spec
`await $p1, $p2, …` (or an array, or a nested list) waits for ALL of them
and returns a List of the results in argument order (a nested list gives a
nested List; Slips among the results are flattened out — 4 elements); the
wait is honoured in sink context too (two 0.3 s starts take ≥ 0.25 s).
`await ()`, `await []` and an empty array return `()`. `await()` with
nothing is `X::AdHoc` ("Must specify an Awaitable to await (got an empty
list)"); `await Nil` or a type object (`await Promise`) is `X::AdHoc` ("Must
specify a defined Awaitable to await (got an undefined Nil)") without the
role; a defined non-Awaitable (42, alone or in a list) is `X::AdHoc` WITH
`X::Await::Died` ("Can only specify Awaitable objects to await (got a
Int)"). A broken promise anywhere in the list rethrows its cause with the
role.
```
my $p1 = Promise.kept(1); my $p2 = start { sleep 0.1; 2 }; my $p3 = Promise.in(0.1).then({ 3 }); say (await $p1, $p2, $p3).raku, " ", (await $p1, $p2, $p3).^name, " ", do { my @p = $p3, $p2, $p1; (await @p).raku }, " ", (await ($p1, ($p2, $p3))).raku, " ", (await ()).raku, " ", (await []).raku, " ", do { my @e; (await @e).raku }, " ", (try await()) // $!.^name, " ", $!.message, " ", (try await Nil) // $!.^name ~ ":" ~ $!.message, " ", (try await 42) // $!.^name ~ ":" ~ $!.message, " ", (try await Promise) // $!.^name ~ ":" ~ $!.message, " ", (try await 1, 2) // $!.^name, " ", (try await Promise.kept(1), 2) // $!.^name, " ", (try await Promise.kept(1), Promise.broken("bk")) // $!.^name ~ ":" ~ $!.message, " ", (await start { (1, 2).Slip }, start { (3, 4).Slip }).raku, " ", (await start { (1, 2).Slip }, start { (3, 4).Slip }).elems, " ", do { my $t0 = now; await (start { sleep 0.3 }, start { sleep 0.3 }); (now - $t0 >= 0.25) }, " ", (await Promise.kept(1), Promise.kept(2)).elems, " ", (await (Promise.kept(1),)).raku, " ", (await Promise.kept(1)).^name, " ", (await Promise.kept(1), Promise.kept(2)).^name, " ", (try await Promise.kept(1), Nil) // $!.^name
# rakudo 2026.08: (1, 2, 3) List (3, 2, 1) (1, 2, 3) () () () X::AdHoc Must specify an Awaitable to await (got an empty list) X::AdHoc:Must specify a defined Awaitable to await (got an undefined Nil) X::AdHoc+{X::Await::Died}:Can only specify Awaitable objects to await (got a Int) X::AdHoc:Must specify a defined Awaitable to await (got an undefined Promise) X::AdHoc+{X::Await::Died} X::AdHoc+{X::Await::Died} X::AdHoc+{X::Await::Died}:bk (1, 2, 3, 4) 4 True 2 (1,) Int List X::AdHoc+{X::Await::Died}
```
rakupp 4.0.1-84: differs — the line died at `$!.message` (the PR-20 fault: `$!` is a Str); the rest is unmeasured.

### PR-22  await on a Channel; a Nil value hangs it                       D:partial R:partial V:bug
`await $channel` returns the next value sent, waiting for it if needed (a
value sent 0.2 s later arrives); `await $c, $c` takes two values. On a
channel that is closed and empty it throws `X::Channel::ReceiveOnClosed`
with the role (`.channel` is the channel), also when the close happens
while waiting; on a channel that was `.fail`ed it rethrows the failure's
exception (`X::AdHoc+{X::Await::Died}` for a Str, the exception's own type
otherwise). `await Channel` (type object) is `X::AdHoc`. The bug: `await`
takes a `Nil` value in the channel as "nothing there yet", so `await` on a
channel whose next value is Nil never returns (the probe's `start { await
$d }` is still Planned after 0.5 s) and that Nil is consumed, while
`.receive` returns the Nil correctly, `Any` is awaited fine, and a Nil
followed by 1 yields 1. Do not imitate: deliver the Nil.
```
my $c = Channel.new; $c.send(1); $c.send(2); say (await $c), " ", (await $c), " ", do { $c.close; ((try await $c) // $!.^name) ~ " " ~ ($!.channel === $c) ~ " " ~ $!.does(X::Await::Died) }, " ", do { my $d = Channel.new; $d.fail("bad"); ((try await $d) // $!.^name) ~ ":" ~ $!.message }, " ", do { my $d = Channel.new; $d.fail(X::NYI.new(feature => "n")); ((try await $d) // $!.^name) }, " ", do { my $d = Channel.new; Promise.in(0.2).then({ $d.send("late") }); await $d }, " ", do { my $d = Channel.new; Promise.in(0.2).then({ $d.close }); ((try await $d) // $!.^name) }, " ", do { my $d = Channel.new; $d.send(1); $d.send(2); (await $d, $d).raku }, " ", do { my $d = Channel.new; $d.send(1); $d.close; ((try (await $d, $d)) // $!.^name) }, " ", (try await Channel) // $!.^name, " ", do { my $d = Channel.new; $d.send(Nil); $d.receive.raku }, " ", do { my $d = Channel.new; $d.send(Nil); my $w = start { await $d }; await Promise.anyof($w, Promise.in(0.5)); $w.status }, " ", do { my $d = Channel.new; $d.send(Nil); $d.send(1); my $w = start { await $d }; await Promise.anyof($w, Promise.in(0.5)); $w.status ~ ":" ~ ($w.status == Kept ?? $w.result.raku !! "-") }, " ", do { my $d = Channel.new; $d.send(Any); my $w = start { (await $d).raku }; await Promise.anyof($w, Promise.in(0.5)); $w.status ~ ":" ~ ($w.status == Kept ?? $w.result !! "-") }
# rakudo 2026.08: 1 2 X::Channel::ReceiveOnClosed+{X::Await::Died} True True X::AdHoc+{X::Await::Died}:bad X::NYI+{X::Await::Died} late X::Channel::ReceiveOnClosed+{X::Await::Died} (1, 2) X::Channel::ReceiveOnClosed+{X::Await::Died} X::AdHoc Nil Planned Kept:1 Kept:Any
```
rakupp 4.0.1-84: differs — `await $c` returns the Channel object itself instead of a value (the line printed channel dumps).

### PR-23  Promises and Junctions                                          D:no R:no V:quirk
`await` does not autothread: `await any($p1, $p2)`, `await all(…)`,
`await ($p1, $p2).any` and `await ($p1|$p2)` all throw
`X::AdHoc+{X::Await::Died}` "Can only specify Awaitable objects to await
(got a Junction)". `keep` takes a Junction as an ordinary value (`.result`
is `any(1, 2)`), `Promise.kept(any(1, 2))` likewise. `break` and
`Promise.broken` autothread over it instead: `$p.break(any(1, 2))` breaks
with payload 1 and then throws `X::Promise::Vowed`, `Promise.broken(any(1,
2))` throws `X::Promise::Resolved` — the quirk. Method calls on a junction
of promises autothread as usual (`($p1|$p2).result` is `any(1, 2)`, so
does `.status`), and `anyof`/`allof` refuse a Junction
(`X::Promise::Combinator`).
```
say (try (await any(Promise.kept(1), Promise.kept(2))).raku) // $!.^name ~ ":" ~ $!.message, " ", (try (await all(Promise.kept(1), Promise.kept(2))).raku) // $!.^name, " ", (Promise.kept(1).status ~~ Kept|Broken), " ", so all((Promise.kept, Promise.kept)».status) == Kept, " ", (await Promise.kept(any(1, 2))).raku, " ", (try do { my $p = Promise.new; $p.keep(any(1, 2)); $p.result.raku }) // $!.^name, " ", (try Promise.allof(any(Promise.kept, Promise.kept)).result.raku) // $!.^name, " ", (Promise.kept(1) ~~ any(Planned, Kept)), " ", (try (Promise.kept(1)|Promise.kept(2)).result.raku) // $!.^name, " ", (any(Promise.kept, Promise.new).status.raku), " ", so any(Promise.kept, Promise.new), " ", so all(Promise.kept, Promise.new), " ", (try Promise.broken(any(1, 2)).cause.payload.raku) // $!.^name, " ", (try (await (Promise.kept(1), Promise.kept(2)).any).raku) // $!.^name, " ", (await (Promise.kept(1), Promise.kept(2)).List).raku, " ", (any(Promise.kept(1), Promise.kept(2)) ~~ Promise), " ", (try (await (Promise.kept(1)|Promise.kept(2))).raku) // $!.^name, " ", do { my $p = Promise.new; my $e = (try $p.break(any(1, 2))) // $!.^name; $e ~ ":" ~ $p.status ~ ":" ~ $p.cause.payload.raku }, " ", do { my $p = Promise.new; my $e = (try $p.keep(any(1, 2))) // $!.^name; $e ~ ":" ~ $p.status }
# rakudo 2026.08: X::AdHoc+{X::Await::Died}:Can only specify Awaitable objects to await (got a Junction) X::AdHoc+{X::Await::Died} True True any(1, 2) any(1, 2) X::Promise::Combinator False any(1, 2) any(PromiseStatus::Kept, PromiseStatus::Planned) True False X::Promise::Resolved X::AdHoc+{X::Await::Died} (1, 2) True X::AdHoc+{X::Await::Died} X::Promise::Vowed:Broken:1 Any:Kept
```
rakupp 4.0.1-84: differs — `await` autothreads over junctions (`(1, 2)`), `keep` and `Promise.kept` autothread too (`X::Promise::Vowed`, a Kept promise with Any), and `Promise.broken(any(…))` is `X::Method::NotFound`.

### PR-24  Non-blocking await on the pool; ThreadPoolScheduler.new       D:partial R:yes V:spec
Code running on the thread pool that `await`s does not keep its thread:
with the pool limited to 4 threads (`PROCESS::<$SCHEDULER> :=
ThreadPoolScheduler.new(max_threads => 4)`) a recursive tree of `start`s of
depth 10 whose every level awaits two children still completes (fib(10) =
89), and eight starts that each sleep 0.3 s all finish within 2 s.
`.max_threads` reads the limit back (64 by default; `Inf` or `*` give
Inf); `initial_threads` above `max_threads` is `X::AdHoc`. An `await` while
a `Lock` is held is allowed and blocks that thread (the probe's start is
Kept). Timing-dependent, generous margins.
```
PROCESS::<$SCHEDULER> := ThreadPoolScheduler.new(max_threads => 4); sub fib($n) { start { $n <= 1 ?? 1 !! await(fib($n - 2)) + await(fib($n - 1)) } }; my $r = fib(10); await Promise.anyof($r, Promise.in(5)); say $r.status, " ", ($r.status == Kept ?? $r.result !! "-"), " ", $*SCHEDULER.max_threads, " ", (try ThreadPoolScheduler.new(max_threads => 2, initial_threads => 3)) // $!.^name, " ", ThreadPoolScheduler.new(max_threads => Inf).max_threads, " ", ThreadPoolScheduler.new(max_threads => *).max_threads, " ", ThreadPoolScheduler.new.max_threads, " ", do { my @p = (^8).map({ start { sleep 0.3; $_ } }); my $t0 = now; my @r = await @p; (now - $t0 < 2) ~ ":" ~ @r.raku }, " ", do { my $l = Lock.new; my $p = Promise.in(0.3); my $w = start { $l.protect({ await $p; "held-await" }) }; await Promise.anyof($w, Promise.in(3)); $w.status ~ ":" ~ $w.result }
# rakudo 2026.08: Kept 89 4 X::AdHoc Inf Inf 64 True:[0, 1, 2, 3, 4, 5, 6, 7] Kept:held-await
```
rakupp 4.0.1-84: differs — `$*SCHEDULER` has no `max_threads`, so the line died at the third field; the fib tree's outcome is unmeasured.

## E. sleep

### PR-25  sleep                                                          D:partial R:yes V:quirk
`sleep $s` always returns Nil and sleeps about `$s` seconds (0.2 measured
at ≥ 0.15; timing-dependent). Zero, negative, `-Inf` and NaN return at
once; Int, Rat, Num, Duration and a numeric Str are accepted ("0.2"
sleeps); `sleep` with no argument, `sleep *`, `sleep Inf` and a huge value
(10**12) sleep forever (the probe's starts are still Planned after 0.3 s).
`sleep Any` returns Nil after three "Use of uninitialized value" warnings.
The quirk: `sleep "abc"` returns Nil at once with no error and no warning.
```
my $t0 = now; my $r = (sleep 0.2).raku; my $e = now - $t0; say $r, " ", ($e >= 0.15), " ", ($e < 2), " ", (sleep -1).raku, " ", (sleep 0).raku, " ", (sleep 1/10).raku, " ", (sleep 0.05e0).raku, " ", (sleep Duration.new(0.05)).raku, " ", (sleep "0.05").raku, " ", (sleep NaN).raku, " ", do { my $t = now; sleep "0.2"; (now - $t >= 0.15) }, " ", do { my @w; CONTROL { when CX::Warn { @w.push(.message.substr(0, 30)); .resume } }; (sleep Any).raku ~ ":" ~ @w.elems ~ ":" ~ @w[0] }, " ", do { my $t = now; my $w = start { sleep 0.3 }; await Promise.anyof($w, Promise.in(3)); $w.status ~ ":" ~ (now - $t < 2) }, " ", do { my $w = start { sleep }; await Promise.anyof($w, Promise.in(0.3)); $w.status }, " ", do { my $w = start { sleep * }; await Promise.anyof($w, Promise.in(0.3)); $w.status }, " ", do { my $w = start { sleep Inf }; await Promise.anyof($w, Promise.in(0.3)); $w.status }, " ", do { my $w = start { sleep 10**12 }; await Promise.anyof($w, Promise.in(0.3)); $w.status }, " ", (try { sleep("abc"); "returned" }) // $!.^name, " ", do { my $t = now; sleep -Inf; (now - $t < 1) }, " ", (sleep 0.001).raku
# rakudo 2026.08: Nil True True Nil Nil Nil Nil Nil Nil Nil True Nil:3:Use of uninitialized value of  Kept:True Planned Planned Planned Planned returned True Nil
```
rakupp 4.0.1-84: differs — `sleep` with no argument and `sleep *` return at once (Kept), and `sleep Any` warns nothing.

### PR-26  sleep-timer and sleep-until                                    D:partial R:yes V:bug
`sleep-timer $s` sleeps like `sleep` and returns a `Duration` of the
seconds NOT slept: `Duration.new(0.0)` normally and for zero, negative and
`-Inf`; a Str numifies ("0.1"; "abc" is `X::Str::Numeric`); no argument
sleeps forever. `sleep-until $instant` returns True once the instant is
reached (sleeping again if woken early) and False, without sleeping, when
the instant is already past — so `sleep-until now` is False; a DateTime is
accepted; an Int, Num or Str is `X::Cannot::New` (no `Instant()` coercion
for them), Nil is `X::AdHoc`. The bug: `sleep-timer NaN` throws
`X::TypeCheck::Return` — the routine cannot make a Duration from NaN and
fails its own return check, where `sleep NaN` returns at once; return zero.
```
my $t0 = now; my $d = sleep-timer 0.2; say $d.^name, " ", ($d == 0), " ", (now - $t0 >= 0.15), " ", $d.raku.substr(0, 3), " ", (sleep-timer -1).raku, " ", (sleep-timer 0).raku, " ", (sleep-timer "0.1").^name, " ", (sleep-timer 1/10).^name, " ", (try (sleep-timer NaN).raku) // $!.^name, " ", (try sleep-timer("abc")) // $!.^name, " ", (sleep-timer -Inf).raku, " ", do { my $w = start { sleep-timer }; await Promise.anyof($w, Promise.in(0.3)); $w.status }, " ", do { my $t = now; my $b = sleep-until now + 0.2; $b.raku ~ ":" ~ (now - $t >= 0.15) }, " ", (sleep-until now - 1).raku, " ", (sleep-until now).raku, " ", (sleep-until DateTime.now.later(seconds => 0.2)).raku, " ", (sleep-until DateTime.now).raku, " ", (try sleep-until 5) // $!.^name, " ", (try sleep-until "x") // $!.^name, " ", (try sleep-until now.Num + 0.1) // $!.^name, " ", (try sleep-until(Nil)) // $!.^name, " ", (sleep-until now + 0.05).^name
# rakudo 2026.08: Duration True True Dur Duration.new(0.0) Duration.new(0.0) Duration Duration X::TypeCheck::Return X::Str::Numeric Duration.new(0.0) Planned Bool::True:True Bool::False Bool::False Bool::True Bool::False X::Cannot::New X::Cannot::New X::Cannot::New X::AdHoc Bool
```
rakupp 4.0.1-84: differs — `sleep-timer` returns the Num `0e0`, not a Duration, NaN and "abc" give 0, a bare `sleep-timer` returns at once, and `sleep-until` accepts an Int, Num or Str and answers False instead of throwing.

## F. Locks and semaphores

### PR-27  Lock                                                            D:partial R:yes V:spec
`Lock.new` (gist `Lock.new`; every `new` a distinct object). `.lock` and
`.unlock` return the Lock. `.protect(&code)` acquires, runs, releases even
when the code dies (the death propagates and another thread can then take
the lock), and returns the code's value RAW: a container comes back
assignable (`$l.protect({ $x }) = 6` sets `$x`), a List stays a List, Nil
Nil. The lock is re-entrant: `lock` twice then `unlock` once keeps it held
(another thread's `protect` stays Planned), the second `unlock` releases
it, and `protect` nests. Misuse: `unlock` of a lock this thread does not
hold — never locked, or held by another thread — throws `X::AdHoc`
("Attempt to unlock mutex by thread not holding it"); `protect` with a
non-Callable is `X::Multi::NoMatch`, with a block that needs arguments the
block's own `X::AdHoc` (the lock is released); `lock(1)` is `X::AdHoc`;
`Lock.protect` on the type object `X::Multi::NoMatch`. Waits are bounded
by timeouts in the probe.
```
my $l = Lock.new; say $l.^name, " ", $l.lock.raku, " ", $l.unlock.raku, " ", $l.protect({ 42 }), " ", $l.protect({ 1, 2 }).raku, " ", $l.protect({ Nil }).raku, " ", ((try $l.protect({ die "oops" })) // $!.^name), " ", do { my $r = start { $l.protect({ "got" }) }; await Promise.anyof($r, Promise.in(1)); $r.status ~ ":" ~ $r.result }, " ", (try Lock.new.unlock) // $!.^name, " ", $!.message, " ", (try $l.protect(42)) // $!.^name, " ", (try $l.protect(-> $a { $a })) // $!.^name, " ", do { my $r = start { $l.protect({ "free" }) }; await Promise.anyof($r, Promise.in(1)); $r.status }, " ", do { $l.lock; $l.lock; $l.unlock; my $r = start { $l.protect({ "in" }) }; await Promise.anyof($r, Promise.in(0.5)); my $s1 = $r.status; $l.unlock; await Promise.anyof($r, Promise.in(1)); $s1 ~ ":" ~ $r.status }, " ", (try Lock.new.lock(1)) // $!.^name, " ", Lock.new.gist, " ", (Lock.new === Lock.new), " ", do { my $m = Lock.new; $m.lock; my $r = start { (try { $m.unlock; "unlocked" }) // $!.^name }; await Promise.anyof($r, Promise.in(1)); $r.result }, " ", (try Lock.protect({ 1 })) // $!.^name, " ", $l.protect({ $l.protect({ "reentrant" }) }), " ", (try { my $x = 5; $l.protect({ $x }) = 6; $x }) // $!.^name
# rakudo 2026.08: Lock Lock.new Lock.new 42 (1, 2) Nil X::AdHoc Kept:got X::AdHoc Attempt to unlock mutex by thread not holding it X::Multi::NoMatch X::AdHoc Kept Planned:Kept X::AdHoc Lock.new False X::AdHoc X::Multi::NoMatch reentrant 6
```
rakupp 4.0.1-84: differs — `lock`/`unlock` return True, `unlock` never throws (not even from a thread that does not hold the lock), `protect(42)` returns 42, `protect(-> $a { … })` returns Nil, `lock(1)` is accepted, the gist is empty, and the protect value is not assignable (`X::Assignment::RO`).

### PR-28  Lock::ConditionVariable                                        D:partial R:yes V:quirk
`$lock.condition` returns a `Lock::ConditionVariable` — a NEW object on
every call (`===` False: the quirk; waiting and signalling still work
across the copies) — and `X::Parameter::InvalidConcreteness` on the type
object; `Lock::ConditionVariable.new` is `X::Lock::ConditionVariable::New`.
`wait` must be called with the lock held (otherwise `X::AdHoc`); it
releases the lock, blocks until `signal` (wakes one waiter) or
`signal_all` (all), and holds the lock again on return; `wait(&predicate)`
returns at once without waiting when the predicate is already true, and
otherwise keeps waiting until it is; both return Nil; `wait(42)` is
`X::Multi::NoMatch`. `signal`/`signal_all` with nobody waiting are no-ops
and return the condition variable. The handshakes (a waiter and a
signaller 0.3 s later; three waiters on a predicate released by one
`signal_all`) are timing-dependent.
```
my $l = Lock.new; my $c = $l.condition; say $c.^name, " ", ($c === $l.condition), " ", (try Lock::ConditionVariable.new) // $!.^name, " ", $c.signal.raku, " ", $c.signal_all.raku, " ", do { my $w = start { try $c.wait; $!.^name }; await Promise.anyof($w, Promise.in(0.5)); $w.status ~ ":" ~ ($w.status == Kept ?? $w.result !! "-") }, " ", do { $l.protect({ $c.wait({ True }); "immediate" }) }, " ", do { my @log; my $t = start { $l.protect({ @log.push("a"); $c.wait; @log.push("c") }) }; sleep 0.3; $l.protect({ @log.push("b"); $c.signal }); await Promise.anyof($t, Promise.in(2)); @log.join(",") ~ ":" ~ $t.status }, " ", do { my $flag = False; my $t = start { $l.protect({ $c.wait({ $flag }); "woke" }) }; sleep 0.2; $l.protect({ $flag = True; $c.signal_all }); await Promise.anyof($t, Promise.in(2)); $t.status ~ ":" ~ $t.result }, " ", do { my $n = 0; my @t = (^3).map({ start { $l.protect({ $c.wait({ $n > 0 }); $n }) } }); sleep 0.2; $l.protect({ $n = 7; $c.signal_all }); await Promise.anyof(Promise.allof(@t), Promise.in(2)); @t».status.join(",") ~ ":" ~ @t».result.join(",") }, " ", (try $c.wait(42)) // $!.^name, " ", $l.protect({ $c.wait({ True }).raku }), " ", (try Lock.condition) // $!.^name
# rakudo 2026.08: Lock::ConditionVariable False X::Lock::ConditionVariable::New Lock::ConditionVariable.new Lock::ConditionVariable.new Kept:X::AdHoc immediate a,b,c:Kept Kept:woke Kept,Kept,Kept:7,7,7 X::Multi::NoMatch Nil X::Parameter::InvalidConcreteness
```
rakupp 4.0.1-84: differs — `.condition` returns the Lock itself, which has no `signal`; the line died there and the rest is unmeasured.

### PR-29  Lock::Soft                                                      D:no R:yes V:spec
The same shape as `Lock` (`lock`, `unlock`, `protect`, `condition`,
re-entrant, `protect` raw and releasing on death), but `lock`/`unlock`
return Nil and misuse is typed: `unlock` of an unheld lock is
`X::Lock::Unlock::NoMutex`, from a thread other than the holder
`X::Lock::Unlock::WrongThread`. `.condition` returns the same
`Lock::Soft::ConditionVariable` every time (its `.lock` is the lock); its
`wait` without the lock held is `X::Lock::ConditionVariable::NoMutex`;
building a second one for a lock that has one is
`X::Lock::ConditionVariable::Duplicate`; the class is lexical, not
reachable by name. `wait`, `wait(&predicate)` and `signal` behave as in
PR-28. There is no docs page; Roast's `S17-lowlevel/lock.t` runs the same
subtests for `Lock` and `Lock::Soft`.
```
my $l = Lock::Soft.new; say $l.^name, " ", $l.lock.raku, " ", $l.unlock.raku, " ", $l.protect({ 42 }), " ", do { my $x = 5; $l.protect({ $x }) = 6; $x }, " ", ((try $l.protect({ die "oops" })) // $!.^name), " ", do { my $r = start { $l.protect({ "got" }) }; await Promise.anyof($r, Promise.in(1)); $r.status ~ ":" ~ $r.result }, " ", (try Lock::Soft.new.unlock) // $!.^name, " ", do { $l.lock; my $r = start { try $l.unlock; $!.^name }; await Promise.anyof($r, Promise.in(1)); $l.unlock; $r.result }, " ", do { $l.lock; $l.lock; $l.unlock; my $r = start { $l.protect({ "in" }) }; await Promise.anyof($r, Promise.in(0.5)); my $s1 = $r.status; $l.unlock; await Promise.anyof($r, Promise.in(1)); $s1 ~ ":" ~ $r.status }, " ", $l.condition.^name, " ", ($l.condition === $l.condition), " ", (try $l.condition.wait) // $!.^name, " ", (try $l.condition.WHAT.new($l)) // $!.^name, " ", ($l.condition.lock === $l), " ", do { my $m = Lock::Soft.new; my $c = $m.condition; my @log; my $t = start { $m.protect({ @log.push("a"); $c.wait; @log.push("c") }) }; sleep 0.3; $m.protect({ @log.push("b"); $c.signal }); await Promise.anyof($t, Promise.in(2)); @log.join(",") ~ ":" ~ $t.status }, " ", do { my $m = Lock::Soft.new; $m.protect({ $m.condition.wait({ True }); "immediate" }) }, " ", (try Lock::Soft.new.protect(42)) // $!.^name, " ", $l.protect({ $l.protect({ "reentrant" }) }), " ", (try ::("Lock::Soft::ConditionVariable").^name) // $!.^name, " ", (try $l.condition.WHAT.new(Lock::Soft.new).^name) // $!.^name
# rakudo 2026.08: Lock::Soft Nil Nil 42 6 X::AdHoc Kept:got X::Lock::Unlock::NoMutex X::Lock::Unlock::WrongThread Planned:Kept Lock::Soft::ConditionVariable True X::Lock::ConditionVariable::NoMutex X::Lock::ConditionVariable::Duplicate True a,b,c:Kept immediate X::Multi::NoMatch reentrant X::NoSuchSymbol Lock::Soft::ConditionVariable
```
rakupp 4.0.1-84: differs — `Lock::Soft` does not exist (the line died at compile time).

### PR-30  Lock::Async                                                     D:partial R:yes V:quirk
`.lock` returns a Promise: an already-Kept one (result True) when the lock
was free — the SAME singleton Promise every time, shared by every
Lock::Async (the quirk; harmless, it is never Planned) — or a Planned one
that is kept with True when the holder calls `unlock`, first come first
served (three `lock` calls give Kept, Planned, Planned and one `unlock`
keeps the second). `unlock` returns Nil, may be called from another thread
than the one that locked, and throws `X::Lock::Async::NotLocked` when the
lock is not held. The lock is NOT re-entrant: `protect` inside `protect` on
the same lock blocks forever (the probe's start is still Planned after
1 s). `protect(&code)` waits for the lock, runs, unlocks even on death, and
returns the raw value (assignable container). `protect(42)` is
`X::Multi::NoMatch`, `lock(1)` `X::AdHoc`.
```
my $l = Lock::Async.new; my $a = $l.lock; say $l.^name, " ", $a.^name, " ", $a.status, " ", $a.result, " ", do { my $b = $l.lock; $b.status ~ ":" ~ $l.unlock.raku ~ ":" ~ do { await Promise.anyof($b, Promise.in(1)); $b.status ~ ":" ~ $b.result } }, " ", do { $l.unlock; my $c = $l.lock; ($c === $a) ~ ":" ~ ($c === Lock::Async.new.lock) }, " ", (try Lock::Async.new.unlock) // $!.^name, " ", $!.message, " ", do { $l.unlock; (try $l.unlock) // $!.^name }, " ", $l.protect({ 42 }), " ", do { my $x = 5; $l.protect({ $x }) = 6; $x }, " ", ((try $l.protect({ die "oops" })) // $!.^name), " ", $l.protect({ "again" }), " ", do { my $m = Lock::Async.new; await $m.lock; my $r = start { $m.unlock; "other-thread" }; await Promise.anyof($r, Promise.in(1)); $r.result ~ ":" ~ $m.lock.status }, " ", do { my $n = Lock::Async.new; my @p = $n.lock xx 3; @p».status.join(",") ~ ":" ~ do { $n.unlock; await Promise.anyof(@p[1], Promise.in(1)); @p».status.join(",") } }, " ", (try $l.protect(42)) // $!.^name, " ", (try Lock::Async.new.lock(1)) // $!.^name, " ", do { my $r = start { $l.protect({ $l.protect({ "nested" }) }) }; await Promise.anyof($r, Promise.in(1)); $r.status }
# rakudo 2026.08: Lock::Async Promise Kept True Planned:Nil:Kept:True True:True X::Lock::Async::NotLocked Cannot unlock a Lock::Async that is not currently locked X::Lock::Async::NotLocked 42 6 X::AdHoc again other-thread:Kept Kept,Planned,Planned:Kept,Kept,Planned X::Multi::NoMatch X::AdHoc Planned
```
rakupp 4.0.1-84: differs — `.lock` returns a Bool, not a Promise; the line died at `.status` and the rest is unmeasured.

### PR-31  protect-or-queue-on-recursion, with-lock-hidden-from-recursion-check   D:partial R:no V:spec
`protect-or-queue-on-recursion(&code)`: with the lock free, runs the code
now and returns Nil; with the lock held by an outer call of this method on
the current call chain, does NOT run the code but returns a Planned
Promise for it, and the code runs — synchronously, as the outer call
releases — after the outer code has finished (order "outer;inner;"), the
Promise being kept with its value; with the lock held by something not on
the call chain, waits for it, runs the code and returns Nil.
`with-lock-hidden-from-recursion-check(&code)` runs the code at once with
this lock removed from the recursion record, so a
`protect-or-queue-on-recursion` inside it does not see the recursion —
and, when the lock is held by the enclosing call, waits forever (the
probe's start stays Planned). Outside any recursion it simply runs the
code. The count fields are read at each point (`$count.Str`).
```
my $l = Lock::Async.new; my $count = 0; my $info = ""; say $l.protect-or-queue-on-recursion({ $count++ }).raku, " ", $count.Str," ", $l.protect-or-queue-on-recursion({ my $inner = $l.protect-or-queue-on-recursion({ $count++ }); $info = $inner.^name ~ ":" ~ $inner.status }).raku, " ", $info, " ", $count.Str," ", do { my $inner; $l.protect-or-queue-on-recursion({ $inner = $l.protect-or-queue-on-recursion({ $count++; "q" }) }); await Promise.anyof($inner, Promise.in(1)); $inner.status ~ ":" ~ $inner.result.raku ~ ":" ~ $count }, " ", $l.protect-or-queue-on-recursion({ $l.with-lock-hidden-from-recursion-check({ $count++; "hidden" }) }).raku, " ", $count.Str," ", $l.with-lock-hidden-from-recursion-check({ "outside" }), " ", do { my $m = Lock::Async.new; await $m.lock; my $r = start { $m.protect-or-queue-on-recursion({ "held-elsewhere" }) }; await Promise.anyof($r, Promise.in(0.3)); my $s = $r.status; $m.unlock; await Promise.anyof($r, Promise.in(1)); $s ~ ":" ~ $r.status ~ ":" ~ $r.result.raku }, " ", do { my $k = $l.lock; my $st = $k.status; $l.unlock; $st }, " ", do { my $d = ""; $l.protect-or-queue-on-recursion({ $l.protect-or-queue-on-recursion({ $d ~= "inner;" }); $d ~= "outer;" }); $d }, " ", do { my $d = ""; my $w = start { $l.protect-or-queue-on-recursion({ $l.with-lock-hidden-from-recursion-check({ $l.protect-or-queue-on-recursion({ $d ~= "in;" }) }); $d ~= "out;" }) }; await Promise.anyof($w, Promise.in(0.5)); $w.status ~ ":" ~ $d }
# rakudo 2026.08: Nil 1 Nil Promise:Planned 2 Kept:"q":3 Nil 4 outside Planned:Kept:Nil Kept outer;inner; Planned:
```
rakupp 4.0.1-84: differs — `protect-or-queue-on-recursion` returns an Int (the code's value) instead of Nil or a Promise; the line died at `.status` and the rest is unmeasured.

### PR-32  Semaphore                                                       D:partial R:partial V:spec
`Semaphore.new($permits)`: the argument is required (`X::AdHoc` "Too few
positionals"), a Str or Rat is `X::AdHoc` ("cannot unbox to a native
integer"), a negative count `X::AdHoc` ("Failed to initialize Semaphore:
invalid argument"). `try_acquire` takes a permit and returns True, or False
at once when none is left; `acquire` blocks until a permit is available
(the probe's start on a zero-permit semaphore is Planned until the main
thread's `release`); `release` adds a permit with no ceiling (three
releases on a 2-permit semaphore give 5) and may come from another
thread; `acquire` and `release` return Mu. gist and raku are
`Semaphore.new`; each `new` is distinct; `Semaphore.acquire` on the type
object is `X::AdHoc`.
```
my $s = Semaphore.new(2); say $s.^name, " ", $s.try_acquire, " ", $s.try_acquire, " ", $s.try_acquire, " ", $s.release.raku, " ", $s.try_acquire, " ", do { $s.release; $s.release; $s.release; ($s.try_acquire, $s.try_acquire, $s.try_acquire, $s.try_acquire).join(",") }, " ", Semaphore.new(0).try_acquire, " ", do { my $z = Semaphore.new(0); my $r = start { $z.acquire; "acquired" }; await Promise.anyof($r, Promise.in(0.3)); my $st = $r.status; $z.release; await Promise.anyof($r, Promise.in(1)); $st ~ ":" ~ $r.status ~ ":" ~ $r.result }, " ", Semaphore.new(1).acquire.raku, " ", (try Semaphore.new) // $!.^name ~ ":" ~ $!.message.substr(0, 40), " ", (try Semaphore.new("2")) // $!.^name ~ ":" ~ $!.message.substr(0, 40), " ", (try Semaphore.new(-1).try_acquire) // $!.^name ~ ":" ~ $!.message.substr(0, 40), " ", (try Semaphore.new(1.5)) // $!.^name ~ ":" ~ $!.message.substr(0, 40), " ", Semaphore.new(1).gist, " ", Semaphore.new(3).raku, " ", (try Semaphore.acquire) // $!.^name, " ", do { my $q = Semaphore.new(1); $q.acquire; my $r = start { $q.release; "released-elsewhere" }; await Promise.anyof($r, Promise.in(1)); $r.result ~ ":" ~ $q.try_acquire }, " ", (try Semaphore.new(2**40).try_acquire) // $!.^name, " ", (Semaphore.new(1) === Semaphore.new(1))
# rakudo 2026.08: Semaphore True True False Mu True True,True,True,False False Planned:Kept:acquired Mu X::AdHoc:Too few positionals passed; expected 2 a X::AdHoc:This type cannot unbox to a native integ X::AdHoc:Failed to initialize Semaphore: invalid  X::AdHoc:This type cannot unbox to a native integ Semaphore.new Semaphore.new X::AdHoc released-elsewhere:True False False
```
rakupp 4.0.1-84: differs — `release`/`acquire` return True, `Semaphore.new` without an argument or with a Str, Rat or negative count succeeds (a dump), gist/raku are dumps, and the type-object call is `X::Method::NotFound`.

### PR-33  The permit count is 32 bits wide                                D:no R:no V:bug
Permit counts live in 32 bits: 2**31-1 works; 2**31 through 2**32-1 are
refused as "invalid argument" (`X::AdHoc`); 2**32 and 2**40 are silently
accepted and give a semaphore with ZERO permits (`try_acquire` False),
2**32+1 gives one permit; 2**63 is refused, 2**64 is `X::AdHoc` "Cannot
unbox 65 bit wide bigint". Do not imitate the silent wrap: refuse, or
support, big counts.
```
say (try Semaphore.new(2**31 - 1).try_acquire) // $!.^name, " ", (try Semaphore.new(2**31).try_acquire) // $!.^name, " ", (try Semaphore.new(2**32 - 1).try_acquire) // $!.^name, " ", (try Semaphore.new(2**32).try_acquire) // $!.^name ~ ":" ~ $!.message.substr(0, 40), " ", (try Semaphore.new(2**32 + 1).try_acquire) // $!.^name, " ", (try Semaphore.new(2**40).try_acquire) // $!.^name, " ", (try Semaphore.new(2**63)) // $!.^name, " ", (try Semaphore.new(2**64)) // $!.^name ~ ":" ~ $!.message.substr(0, 30), " ", do { my $s = Semaphore.new(1); $s.release; $s.release; ($s.try_acquire, $s.try_acquire, $s.try_acquire, $s.try_acquire).join(",") }, " ", do { my $s = Semaphore.new(0); $s.release; $s.try_acquire }, " ", Semaphore.new(1).try_acquire.^name, " ", (try Semaphore.new(-1)) // $!.^name ~ ":" ~ $!.message.substr(0, 40)
# rakudo 2026.08: True X::AdHoc X::AdHoc False True False X::AdHoc X::AdHoc:Cannot unbox 65 bit wide bigin True,True,True,False True Bool X::AdHoc:Failed to initialize Semaphore: invalid 
```
rakupp 4.0.1-84: differs — every count is accepted and reported back as given (2**63 shows 9223372036854775807), and -1 gives a semaphore with count -1.

## G. Scheduler and method surface

### PR-34  $*SCHEDULER.cue                                                 D:partial R:yes V:quirk
`$*SCHEDULER` is a `ThreadPoolScheduler` doing `Scheduler`;
`.uncaught_handler` is the `Callable` type object until set, `.loads` an
Int, `.max_threads` 64. `cue(&code)` and `cue(&code, :catch)` run the code
on the pool and return Nil (the quirk: the docs promise a Cancellation;
Roast skips that assertion for the pool scheduler); every timed form
(`:in`, `:at`, `:times`, `:every`) returns a `Cancellation` whose
`.cancelled` is False, whose `.cancel` returns True and sets it, and whose
timer then never fires. `:in(NaN)`, `:at(NaN)` and `:every(NaN)` throw
`X::Scheduler::CueInNaNSeconds`; `:in` with `:at` is `X::AdHoc` "Cannot
specify :at and :in at the same time"; `:every` with `:times` above 1 and
`:stop` together "Cannot specify :every, :times and :stop at the same
time". `:in(Inf)` returns a Cancellation and never runs; `:in(-Inf)` runs at
once; `:every(Inf)` runs the code once, at once, on the caller's thread,
with the warning "Inf was passed via :every; running the given block only
once, immediately"; `:times(3)` runs three times, `:every(0.05), :times(3)`
three times, `:every(0.05), :stop(&pred)` until the predicate is true
before a run (2); `:catch` receives the exception of a dying cued block.
Timing-dependent (0.05 s periods observed over 0.5 s).
```
say $*SCHEDULER.^name, " ", ($*SCHEDULER ~~ Scheduler), " ", $*SCHEDULER.uncaught_handler.raku, " ", $*SCHEDULER.loads.^name, " ", $*SCHEDULER.max_threads, " ", $*SCHEDULER.cue({ ; }).raku, " ", $*SCHEDULER.cue({ ; }, :catch({ ; })).raku, " ", $*SCHEDULER.cue({ ; }, :in(0.01)).^name, " ", $*SCHEDULER.cue({ ; }, :at(now)).^name, " ", $*SCHEDULER.cue({ ; }, :times(2)).^name, " ", do { my $c = $*SCHEDULER.cue({ ; }, :every(0.05), :times(2)); $c.^name ~ ":" ~ $c.cancelled ~ ":" ~ $c.cancel.raku ~ ":" ~ $c.cancelled }, " ", (try $*SCHEDULER.cue({ ; }, :in(NaN))) // $!.^name, " ", (try $*SCHEDULER.cue({ ; }, :at(NaN))) // $!.^name, " ", (try $*SCHEDULER.cue({ ; }, :every(NaN))) // $!.^name, " ", (try $*SCHEDULER.cue({ ; }, :in(1), :at(now))) // $!.^name ~ ":" ~ $!.message, " ", (try $*SCHEDULER.cue({ ; }, :every(1), :times(2), :stop({ True }))) // $!.^name ~ ":" ~ $!.message, " ", $*SCHEDULER.cue({ ; }, :in(Inf)).^name, " ", do { my $n = 0; my $c = $*SCHEDULER.cue({ $n++ }, :every(0.05), :times(3)); sleep 0.5; $n }, " ", do { my $n = 0; $*SCHEDULER.cue({ $n++ }, :times(3)); sleep 0.3; $n }, " ", do { my $n = 0; $*SCHEDULER.cue({ $n++ }, :in(-Inf)); sleep 0.2; $n }, " ", do { my $n = 0; $*SCHEDULER.cue({ $n++ }, :in(Inf)); sleep 0.2; $n }, " ", do { my @w; my $n = 0; CONTROL { when CX::Warn { @w.push(.message.substr(0, 20)); .resume } }; $*SCHEDULER.cue({ $n++ }, :every(Inf)); sleep 0.2; $n ~ ":" ~ @w.raku }, " ", do { my $n = 0; my $c = $*SCHEDULER.cue({ $n++ }, :every(0.05), :stop({ $n >= 2 })); sleep 0.5; $n }, " ", do { my @c; $*SCHEDULER.cue({ die "cd" }, :catch({ @c.push(.message) })); sleep 0.3; @c.raku }, " ", do { my $c = $*SCHEDULER.cue({ ; }, :in(5)); $c.cancel; $c.cancelled }, " ", do { my $n = 0; my $c = $*SCHEDULER.cue({ $n++ }, :in(0.3)); $c.cancel; sleep 0.5; $n }
# rakudo 2026.08: ThreadPoolScheduler True Callable Int 64 Nil Nil Cancellation Cancellation Cancellation Cancellation:False:Bool::True:True X::Scheduler::CueInNaNSeconds X::Scheduler::CueInNaNSeconds X::Scheduler::CueInNaNSeconds X::AdHoc:Cannot specify :at and :in at the same time X::AdHoc:Cannot specify :every, :times and :stop at the same time Cancellation 3 3 1 0 1:["Inf was passed via :"] 2 ["cd"] True 0
```
rakupp 4.0.1-84: differs — `$*SCHEDULER` is a bare `Scheduler` without `uncaught_handler`; the line died at that third field and the rest is unmeasured.

### PR-35  Uncaught exceptions, CurrentThreadScheduler, :scheduler        D:partial R:yes V:spec
A cued block that dies with neither `:catch` nor `uncaught_handler` prints
"Unhandled exception in code scheduled on thread N" plus the message to
stderr and ends the process with exit code 1; with `uncaught_handler` set
the handler gets the exception and the program continues.
`CurrentThreadScheduler.new` runs everything synchronously in the caller:
`Promise.start(&code, :scheduler($s))` is Kept when it returns, its `.then`
(which inherits the scheduler) is Kept at once, `Promise.in(0.2,
:scheduler($s))` sleeps 0.2 s in the caller and returns Kept,
`Promise.new(:scheduler($s)).then(…)` runs the then inside `keep`; its
`cue` returns an object with `.cancel`, `:times(3)` runs three times,
`:every` is `X::AdHoc`, a dying block throws `X::AdHoc` to the caller unless
`uncaught_handler` is set (then the handler gets it); `.loads` is 0; it does
`Scheduler`.
```
my sub Rn($code) { my $r = run($*EXECUTABLE, "-e", $code, :out, :err); $r.exitcode ~ ":" ~ $r.out.slurp(:close).raku ~ ":" ~ $r.err.slurp(:close).lines.head(2).join("/").subst(/"thread " \d+/, "thread N").raku }; say Rn('$*SCHEDULER.cue({ die "cued" }); sleep 0.3; say "after"'), " ", Rn('$*SCHEDULER.uncaught_handler = -> $e { say "H:" ~ $e.message }; $*SCHEDULER.cue({ die "cued" }); sleep 0.3; say "after"'), " ", do { my $s = CurrentThreadScheduler.new; my @o; my $p = Promise.start({ @o.push("ran"); 42 }, :scheduler($s)); ($p.status, $p.result, @o.raku, $p.then({ .result + 1 }).status, Promise.in(0.1, :scheduler($s)).status, $s.cue({ @o.push("cued") }).can("cancel").so, @o.elems, $s.loads, (try $s.cue({ ; }, :every(1))) // $!.^name, (try $s.cue({ die "x" })) // $!.^name, ($s ~~ Scheduler), (try ($p.scheduler === $s).raku) // $!.^name, (try ($p.then({ 1 }).scheduler === $s).raku) // $!.^name).join(" ") }, " ", do { my $s = CurrentThreadScheduler.new; my $p = Promise.new(:scheduler($s)); my $t = $p.then({ "t" }); $p.keep(1); $t.status ~ ":" ~ $t.result }, " ", do { my $s = CurrentThreadScheduler.new; my $t0 = now; my $p = Promise.in(0.2, :scheduler($s)); $p.status ~ ":" ~ (now - $t0 >= 0.15) }, " ", do { my $s = CurrentThreadScheduler.new; my $n = 0; $s.cue({ $n++ }, :times(3)); $n }, " ", do { my $s = CurrentThreadScheduler.new; my @h; $s.uncaught_handler = { @h.push(.message) }; $s.cue({ die "u" }); @h.raku }
# rakudo 2026.08: 1:"":"Unhandled exception in code scheduled on thread N/cued" 0:"H:cued\nafter\n":"" Kept 42 ["ran"] Kept Kept True 2 0 X::AdHoc X::AdHoc True Bool::True Bool::True Kept:t Kept:True 3 ["u"]
```
rakupp 4.0.1-84: differs — a dying cued block is not reported (exit 0) though the uncaught handler works; `CurrentThreadScheduler` exists but `Promise.start` on it is asynchronous (Planned), `:every` throws a differently named exception, and `.scheduler` is missing.

### PR-36  The method surface                                               D:no R:no V:spec
Promise's own methods: BUILD, Bool, POPULATE, Supply, allof, andthen, anyof,
at, break, broken, cause, get-await-handle, in, keep, kept, new, orelse,
result, scheduler, sink, start, status, then, vow (no `awaiterator`); its
attributes scheduler, status, result, vow_taken, lock, cond, thens,
thens-sync, dynamic_context, report-broken-if-sunk, of which only
`scheduler` and `status` are public. Lock: condition, lock, new, protect,
unlock. Lock::Async: lock, protect, protect-or-queue-on-recursion, unlock,
with-lock-hidden-from-recursion-check. Lock::Soft: condition, lock, new,
protect, unlock. Semaphore: acquire, new, release, try_acquire.
Lock::ConditionVariable: new, signal, signal_all, wait. `.raku` and `.gist`
of a Promise begin `Promise.new(scheduler => …`; of a Lock, a Lock::Async
and a Semaphore they are `Lock.new`, `Lock::Async.new`, `Semaphore.new`.
MRO: Promise, Any, Mu and Lock::Async, Any, Mu.
```
say Promise.^methods(:local).map(*.name).sort.unique.join(","), " | ", Promise.^attributes.map(*.name).join(","), " | ", Lock.^methods(:local).map(*.name).sort.join(","), " | ", Lock::Async.^methods(:local).map(*.name).sort.join(","), " | ", (try ::("Lock::Soft").^methods(:local).map(*.name).sort.join(",")) // $!.^name, " | ", Semaphore.^methods(:local).map(*.name).sort.join(","), " | ", (try Lock::ConditionVariable.^methods(:local).map(*.name).sort.join(",")) // $!.^name, " | ", Promise.kept.raku.substr(0, 20), " | ", Promise.kept.gist.substr(0, 20), " | ", Lock.new.raku, " | ", Lock::Async.new.raku, " | ", Semaphore.new(1).raku, " | ", Promise.^mro.map(*.^name).join(","), " | ", Lock::Async.^mro.map(*.^name).join(","), " | ", (Promise.can("awaiterator") ?? "yes" !! "no")
# rakudo 2026.08: BUILD,Bool,POPULATE,Supply,allof,andthen,anyof,at,break,broken,cause,get-await-handle,in,keep,kept,new,orelse,result,scheduler,sink,start,status,then,vow | $!scheduler,$!status,$!result,$!vow_taken,$!lock,$!cond,$!thens,$!thens-sync,$!dynamic_context,$!report-broken-if-sunk | POPULATE,condition,lock,new,protect,unlock | POPULATE,lock,protect,protect-or-queue-on-recursion,unlock,with-lock-hidden-from-recursion-check | POPULATE,condition,lock,new,protect,unlock | POPULATE,acquire,new,release,try_acquire | POPULATE,new,signal,signal_all,wait | Promise.new(schedule | Promise.new(schedule | Lock.new | Lock::Async.new | Semaphore.new | Promise,Any,Mu | Lock::Async,Any,Mu | no
```
rakupp 4.0.1-84: differs — `.^methods(:local)` is empty for every class, `Lock::Soft` is an undeclared symbol, `.raku`/`.gist` are hash dumps, and `.^mro` omits the class itself.

## Counts

| | items |
|---|---|
| total | 36 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 12 |
| Rakudo bugs (do not imitate) | 4 — PR-12 a dying `:synchronous` then breaks `keep`/`break` and strands the then-promise, PR-22 a Nil in a Channel hangs `await`, PR-26 `sleep-timer NaN` fails its own return check, PR-33 a permit count of 2**32 wraps to zero |
| quirks (recorded, step two decides) | 7 — PR-07 `keep` binds the container, PR-10 `eqv` blocks on a Planned promise, PR-23 `break` autothreads a Junction, PR-25 `sleep "abc"` is silent, PR-28 a new condition variable per call, PR-30 the shared kept-lock Promise, PR-34 `cue` without a timer returns Nil |
| rakupp 4.0.1-84 differs | 36 (PR-07 by decision) |
| rakupp 4.0.1-84 matches | 0 — every line has at least one differing field; PR-25 (sleep) and PR-07 are the nearest |

Recurring rakupp gaps, for step two: the exception side first — the
`X::Promise::Vowed`, `X::Promise::Combinator` and
`X::Promise::CauseOnlyValidOnBroken` types lack their `.promise`,
`.combinator` and `.status` attributes, the two roles `X::Promise::Broken`
(on `.result`) and `X::Await::Died` (on `await`) are never mixed in, `$!`
after a failed `await` is a Str, `.cause` on a Planned promise is Nil, and
every type-object call is `X::Method::NotFound` where Rakudo refuses with a
typed exception. Then the object: no `.scheduler`, `andthen`, `orelse` or
`:synchronous`; `.raku`, `.gist`, `.Str` and the value returned by `keep`,
`break` and `Promise.in(NaN)` are hash dumps; `PromiseStatus` is not a
type; `.^methods(:local)` is empty and `.^mro` drops the class. `start`:
`start EXPR` takes only the first term, `fail` keeps, extra positionals and
`:catch` are dropped, `return` returns, `$*PROMISE` is wrong, and a sunk
dying start is never reported. `await`: a Channel yields the Channel, and
junctions autothread through `await`, `keep` and `Promise.kept`.
`$*SCHEDULER` is a bare `Scheduler` without `uncaught_handler` or
`max_threads`, and `CurrentThreadScheduler` is not synchronous. Locks: `Lock::Soft` is absent, `Lock.condition` returns the
lock, `Lock::Async.lock` returns a Bool, `Lock` misuse never throws,
`protect` returns a copy; `Semaphore.new` checks nothing and
`acquire`/`release` return True. `sleep` with no argument or `*` returns at
once, `sleep-timer` returns a Num, `sleep-until` takes non-Instants.
PR-07 (copying the kept value) is a deliberate divergence, not a gap.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md), with the timeout discipline of this topic:
every field that could block runs inside a `start` raced with
`Promise.anyof(…, Promise.in(t))` and reports the start's status, so a hang
shows as `Planned` rather than as a killed line; three probes of the first
round still hung and each was a lesson — a `Lock::Async.lock` left held
before a later `protect` (my own), a `protect-or-queue-on-recursion` inside
`with-lock-hidden-from-recursion-check` (PR-31, by design), and a Nil in a
Channel (PR-22, a bug). Traps met: a sub named `X` cannot be called as
`X(…)` (that is a coercion to the `X` package); a variable passed to `say`
is read after ALL arguments are evaluated, so a later argument's side
effect shows in an earlier field (`$ran` read 1 before `keep` ran) — pass
`$v.Str`; `my $r = $p.keep` stores Nil as Any, so print `.raku` inline;
`start { last }` and `sleep-until()` are compile-time refusals that kill the
whole line; a `warn` inside a start prints on stderr out of order — capture
it with `CONTROL` inside the block; zsh's `echo` interprets `\n` in the
outputs, so compare files, never echoed text; `try $p.break(…)` where the
break dies inside a synchronous then needs the `try` around the break, not
around a later read. Four probe rounds. D flags from
`doc/Type/{Promise,PromiseStatus,Lock,Lock/Async,Lock/ConditionVariable,Semaphore,Scheduler,ThreadPoolScheduler,CurrentThreadScheduler,Cancellation}.rakudoc`,
`doc/Type/X/Promise/Vowed.rakudoc`, the `sleep` entries of
`doc/Type/independent-routines.rakudoc` and `doc/Language/concurrency.rakudoc`
(there is no page for Lock::Soft, andthen/orelse, `:synchronous`,
`:report-broken-if-sunk` or `X::Await::Died`); R flags from
`S17-promise/*.t` (basic, then, start, in, at, anyof, allof,
single-assignment, lock-async, nonblocking-await), `S17-lowlevel/lock.t`
and `semaphore.t`, `S17-scheduler/*.t` and `S29-context/sleep.t`.
