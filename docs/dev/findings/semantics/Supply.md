# Supply — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Supply.rakumod`,
`Supply-factories.rakumod`, `Supply-coercers.rakumod`, `Supplier.rakumod`,
`Rakudo/Supply.rakumod` (the supply-block and react runtime); 2,661 lines
read in full on 2026-09-17. Oracle: Homebrew Rakudo v2026.08. Compared
against Raku++ 4.0.1-6-gd811876c (build-arm64, 2026-09-17). Format and
legend: [README.md](README.md).

Where this sits against the declared spec: docs.raku.org documents 43 Supply
methods; Roast has 58 files in `S17-supply/`, Rakudo passes 56 of them here
and Raku++ passed 27 when this sheet was written (33 after implementing it).
So the *surface* gap is already measured by Roast. What
this sheet adds is the rules inside the methods — ordering, what an empty or
misused input yields, which exception type — 38 of the 69 items below are neither fully
stated by docs nor fully asserted by Roast.

The two Rakudo bugs and the quirks flagged below are recorded so that step two
does not imitate them. Rakudo's mechanism is deliberately absent from this
document.

## A. Protocol and lifecycle

### S-01  A Supply cannot be created bare                          D:no R:no V:spec
`Supply.new` with no source throws `X::Supply::New`. Supplies come from a
`Supplier`, from `Supply.on-demand`/`from-list`/`interval`, or from a
`supply` block.
```
say (try Supply.new) // $!.^name
# rakudo 2026.08: X::Supply::New
```
rakupp 4.0.2: matches.

### S-02  tap: defaults, and the quit default rethrows            D:partial R:no V:spec
`.tap(&emit?, :&done, :&quit, :&tap)`. Emit defaults to discard, done to a
no-op, **quit to rethrowing the exception at the point where the source
quit**, so an unhandled quit surfaces as an exception in the emitter. The
`:tap` callback receives the `Tap` object before any value flows.
```
say do { my $s = Supplier.new; my $x = try { $s.Supply.tap({;}); $s.quit("boom"); "ok" }; $! ?? $!.^name ~ ":" ~ $!.message !! $x }
# rakudo 2026.08: X::AdHoc:boom
say do { my $t = Supply.from-list(1,2).tap(-> $v {;}, tap => -> $tap { say "tap-cb:" ~ $tap.^name }); $t.^name }
# rakudo 2026.08: tap-cb:Tap
# rakudo 2026.08: Tap
```
rakupp 4.0.2: matches.

### S-03  Tap.close  D:partial R:? V:spec
`Tap.new` takes an optional close hook. `.close` returns `True`; closing
twice is harmless; if the hook returns a `Promise`, `.close` waits for it.
```
say Tap.new.close, " ", Tap.new({ 42 }).close
# rakudo 2026.08: True True
```
rakupp 4.0.2: matches.

### S-04  live and serial flags                                    D:partial R:? V:spec
`.live` is True only for supplies backed by a `Supplier`; `.serial` promises
no concurrent emits. Derived supplies carry the liveness of their source.
`from-list`: live False, serial True. `Supplier.Supply`: True, True.
`interval`: live False. A `supply` block: live False, serial True.
```
say Supply.from-list(1).live, " ", Supplier.new.Supply.live, " ", Supply.interval(1).live, " ", Supply.from-list(1).serial, " ", Supplier.new.Supply.serial, " ", Supply.from-list(1).map({$_}).live, " ", Supplier.new.Supply.map({$_}).live
# rakudo 2026.08: False True False True True False True
say (supply { emit 1; emit 2 }).live, " ", (supply { emit 1 }).serial, " ", (supply { whenever Supplier.new.Supply { } }).live
# rakudo 2026.08: False True False
```
rakupp 4.0.2: matches — every Supply shape here delivers one event at a time to a
given tap, so `.serial` is True throughout.

### S-05  A Supplier fans out to the taps present at emit time    D:yes R:yes V:spec
Values emitted before a tap exists are lost; a closed tap receives nothing
further; a tap that closes itself from inside its handler stops after that value.
```
say do { my $s = Supplier.new; $s.emit(1); my @o; $s.Supply.tap({ @o.push($_) }); $s.emit(2); @o }
# rakudo 2026.08: [2]
say do { my $s = Supplier.new; my @o; my $t; $t = $s.Supply.tap({ @o.push($_); $t.close if $_ == 2 }); $s.emit($_) for 1..4; @o }
# rakudo 2026.08: [1 2]
```
rakupp 4.0.2: matches.

### S-06  The event grammar is enforced per tap: emit* [done|quit]   D:no R:no V:spec
On a `Supplier`'s Supply, an emit after done is dropped, a second done is
dropped, a quit after done is dropped.
```
say do { my $s = Supplier.new; my @o; $s.Supply.tap({ @o.push($_) }, done => { @o.push("done") }); $s.emit(1); $s.done; $s.emit(2); $s.done; @o }
# rakudo 2026.08: [1 done]
```
rakupp 4.0.2: matches. The rule is enforced per TAP, not on the Supplier: Roast's
`basic.t` takes a second Supply from a Supplier that is already done and emits
into it, and that fresh tap receives the values.

### S-07  Re-entrant emits are deferred for the emitting tap only  D:no R:no V:spec
If a tap's handler itself emits into the same Supplier, that value reaches
**other** taps immediately but reaches **the emitting tap** only after its
handler returns. Handlers of one tap never nest.
```
say do { my $s = Supplier.new; my @o; $s.Supply.tap({ @o.push("a$_"); $s.emit(9) if $_ == 1 }); $s.Supply.tap({ @o.push("b$_") }); $s.emit(1); @o }
# rakudo 2026.08: [a1 b9 a9 b1]
```
rakupp 4.0.2: matches — a value arriving while a tap's handler runs is queued on
that tap and drained when the handler returns.

### S-08  Supplier.quit with a string  D:partial R:? V:spec
`quit(Str)` wraps the message in `X::AdHoc` (payload = the string); `quit(Exception)` passes it through.
```
say do { my $s = Supplier.new; my $x = try { $s.Supply.tap({;}); $s.quit(X::AdHoc.new(payload => "q")); "no-throw" }; $x // $!.message }
# rakudo 2026.08: q
```
rakupp 4.0.2: matches. The rethrow stops the fan-out where it happens, so a tap
with no quit handler hides the quit from taps registered after it — as in Rakudo.

### S-09  Supplier::Preserving replays, and resumes preserving  D:partial R:partial V:spec
Events emitted while nobody taps are kept and replayed, in order, to the
first tap that arrives; from then on delivery is live. When the last tap
closes, preserving resumes for the next first tap. Preserved events include
done and quit.
```
say do { my $s = Supplier::Preserving.new; $s.emit(1); $s.emit(2); my @o; $s.Supply.tap({ @o.push($_) }); $s.emit(3); @o }
# rakudo 2026.08: [1 2 3]
say do { my $s = Supplier::Preserving.new; $s.emit(1); my $t = $s.Supply.tap({;}); $t.close; $s.emit(2); my @o; $s.Supply.tap({ @o.push($_) }); @o }
# rakudo 2026.08: [2]
```
Roast `supplier-preserving.t` asserts the replay under a race; the resume-after-last-close rule is asserted nowhere.
rakupp 4.0.2: matches — the kept events are drained into the first tap that
arrives, so the next quiet period starts empty.

### S-10  sanitize and serialize                                   D:no R:no V:spec
`.serialize` returns a supply that never delivers two events concurrently;
`.sanitize` additionally enforces S-06 and closes the source when done or
quit. Both return the invocant itself when it already has the property.
On-demand supplies, `Supplier.Supply` and `supply` blocks are already sane.
```
say Supply.from-list(1,2,3).serialize.list, " ", Supply.from-list(1,2,3).sanitize.list, " ", Supply.from-list(1,2).Tappable.^name
# rakudo 2026.08: (1 2 3) (1 2 3) Supply::Sanitize
```
rakupp 4.0.2: matches. Both are the invocant: a rakupp Supply is already serial
(S-04) and already enforces the grammar per tap (S-06), which is exactly the
condition under which Rakudo returns the invocant too.

### S-11  act = sanitize then tap                                  D:yes R:yes V:spec
```
say do { my @o; (supply { whenever Supply.from-list(1,2) { emit $_ } }).act({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 2 done]
```
rakupp 4.0.2: matches.

### S-12  on-close runs on every close call  D:partial R:partial V:quirk
The hook given to `.on-close` runs each time `.close` is called on the
resulting tap, not once.
```
say do { my $s = Supplier.new; my $closed = 0; my $t = $s.Supply.on-close({ $closed++ }).tap({;}); $t.close; $t.close; $closed }
# rakudo 2026.08: 2
```
rakupp 4.0.2: matches. Decision on the quirk: KEPT. `.on-close` is a teardown
hook, and a caller that closes twice has asked for the teardown twice; making it
fire once would need a flag whose only purpose is to differ from Rakudo.

### S-13  X::Supply::Combinator names the combinator               D:no R:no V:spec
Thrown by `merge`, `zip`, `zip-latest` when an argument is not a defined
Supply; `.combinator` holds the method name.
```
say do { my $e = try { Supply.merge(Supply, Supply) }; $! ?? $!.combinator !! "no-error" }
# rakudo 2026.08: merge
```
rakupp 4.0.2: matches.

### S-14  No event may follow a quit — Rakudo leaks here          D:no R:no V:bug
The intended rule is emit* [done|quit], S-06. Rakudo violates it when a
one-source operator (`.map`) quits while a synchronous source is still
producing: the later values reach `.tap` and even `.act`. A supply-block
operator (`.do`) does not leak.
```
say do { my @o; Supply.from-list(1,2,3).map({ die "bad" if $_ == 2; $_ }).act({ @o.push($_) }, quit => { @o.push("quit:" ~ .message) }); @o }
# rakudo 2026.08: [1 quit:bad 3]        <- the 3 is the bug
say do { my @o; Supply.from-list(1,2,3).do({ die "d" if $_ == 2 }).tap({ @o.push($_) }, quit => { @o.push("quit:" ~ .message) }); @o }
# rakudo 2026.08: [1 quit:d]
```
Step two: deliver `1`, then the quit, and nothing after, in both cases.
rakupp 4.0.2: **deliberately differs from Rakudo** — `[1 quit:bad]` in both cases.
The bug is not imitated: a one-source operator whose user code dies records the
death as the supply's quit and stops there, so the grammar of S-06 holds on
every path.

## B. Factories

### S-15  on-demand: per-tap producer, synchronous by default, closing hook  D:partial R:partial V:spec
Each tap runs the producer afresh with its own Supplier. The producer runs
**synchronously on the tapping thread** unless `:scheduler` says otherwise,
so values arrive before `.tap` returns. `:closing` runs once when the tap
closes, which also happens implicitly on done or quit. An exception in the
producer becomes a quit.
```
say do { my @o; my $t = Supply.from-list(1,2).tap({ @o.push("got $_") }); @o.push("after"); @o }
# rakudo 2026.08: [got 1 got 2 after]
say do { my $closed = 0; my $t = Supply.on-demand(-> $p { $p.emit(1); $p.done }, closing => { $closed++ }).tap({;}); $closed }
# rakudo 2026.08: 1
say do { my $closed = 0; my $t = Supply.on-demand(-> $p { $p.emit(1) }, closing => { $closed++ }).tap({;}); $t.close; $closed }
# rakudo 2026.08: 1
say do { my @o; Supply.on-demand(-> $p { die "prod" }).tap({;}, quit => { @o.push("quit:" ~ .message) }); @o }
# rakudo 2026.08: [quit:prod]
```
`:closing` is undocumented. rakupp 4.0.2: matches — `on-demand` is a supply
block with a generated body, so the liveness, the completion rule and the
die-becomes-quit rule are the ones S-53 to S-68 already give a block.

### S-16  from-list follows the single-argument rule               D:no R:no V:spec
One Iterable argument is flattened into values; several arguments are each
one value, even if they are lists.
```
say Supply.from-list([1,2,3]).list.elems, " ", Supply.from-list((1,2),(3,4)).list.elems, " ", Supply.from-list().list.elems
# rakudo 2026.08: 3 2 0
```
rakupp 4.0.2: matches. A Slip is the one argument that always splices, however
many arguments there are (Roast `min.t` feeds `from-list` two Slips).

### S-17  interval                                                 D:yes R:yes V:spec
`Supply.interval($every, $delay = 0)` emits 0, 1, 2, … every `$every`
seconds, the first after `$delay`; it never completes; closing the tap stops
it. It is a class method only: calling it on an instance dies (Roast `interval.t`).
```
say Supply.interval(0.02).head(3).list
# rakudo 2026.08: (0 1 2)
say do { my $t0 = now; my $v = Supply.interval(1, 0.05).head(1).list; ((now - $t0) < 0.5) ?? "delay-first" !! "waited-interval" }
# rakudo 2026.08: delay-first
```
rakupp 4.0.2: matches. A ticker has no values until something drives it, so
asking a kind-based supply for its list taps it and waits for the stream to end —
which `.head(3)` is what makes happen.

## C. One-source operators (carry liveness, always serial)

### S-18  map: an exception in the mapper becomes a quit           D:no R:? V:spec
```
say do { my @o; Supply.from-list(1,2,3).map({ die "bad" if $_ == 2; $_ }).tap({ @o.push($_) }, quit => { @o.push("quit:" ~ .message) }); @o }
# rakudo 2026.08: [1 quit:bad 3]       <- the trailing 3 is S-14's bug; expect [1 quit:bad]
```
rakupp 4.0.2: matches the intent — `[1 quit:bad]`. See S-14: the value after the
quit is Rakudo's bug and is not imitated.

### S-19  grep uses smartmatch                                     D:yes R:yes V:spec
A type, a value, a regex or a callable all work through `ACCEPTS`.
```
say Supply.from-list(1,"a",2.5,3).grep(Int).list, " ", Supply.from-list(1,2,3).grep(2).list, " ", Supply.from-list(<a b>).grep(/b/).list
# rakudo 2026.08: (1 3) (2) (b)
```
rakupp 4.0.2: matches.

### S-20  first  D:partial R:partial V:spec
`.first` is `.head`; `.first(test)` is `.grep(test).head`; `:end` selects the
last match instead. An empty source yields an empty supply.
```
say Supply.from-list(1,2,3,4).first(* > 1).list, " ", Supply.from-list(1,2,3,4).first(* > 1, :end).list, " ", Supply.from-list(1,2,3).first(:end).list, " ", Supply.from-list().first.list
# rakudo 2026.08: (2) (4) (3) ()
```
rakupp 4.0.2: matches.

### S-21  stable: debounce; a pending value is lost on done  D:partial R:partial V:quirk
`stable($t)` emits a value only if no newer value arrives within `$t`
seconds; a newer value cancels the pending one. Falsy `$t` returns the
invocant (Roast). Done and quit pass through **immediately**, so a value
still pending when the source completes is never emitted.
```
say Supply.from-list(1,2,3).stable(0).list, " ", Supply.from-list(1,2,3).stable(0.01).list
# rakudo 2026.08: (1 2 3) ()
say do { my $s = Supplier.new; my @o; $s.Supply.stable(0.05).tap({ @o.push($_) }); $s.emit(1); $s.emit(2); sleep 0.15; $s.emit(3); sleep 0.15; @o }
# rakudo 2026.08: [2 3]
```
Roast's live test of `stable` is skipped in Rakudo itself. Step two may
choose to flush the pending value on done; the sheet records that Rakudo does not.
rakupp 4.0.2: still differs — no debounce. `stable` on a list-backed supply is
the invocant (every value is already older than any window), and on a live one it
is a scheduling combinator that is not built. NOT DONE.

### S-22  delayed                                                  D:yes R:yes V:spec
Falsy time returns the invocant; otherwise every event, including done and quit, is delivered `$t` seconds later, order preserved.
```
say Supply.from-list(1,2,3).delayed(0).list, " ", Supply.from-list(1,2,3).delayed(0.01).list
# rakudo 2026.08: (1 2 3) (1 2 3)
```
rakupp 4.0.2: matches.

### S-23  schedule-on                                              D:yes R:yes V:spec
Every event is handed to the given scheduler for delivery. Ordering between
events is then the scheduler's. Not probed; Roast `schedule-on.t` passes on both engines.
rakupp 4.0.2: matches — every tap here already delivers one event at a time
(S-04), so `.schedule-on` is the invocant and the ordering it promises holds.

### S-24  start                                                    D:yes R:yes V:spec
`.start(&code)` maps each value to a Supply that runs `code(value)` on the
thread pool and emits its single result, then done; an exception there is
that inner supply's quit. Consume with `.flat` or `.migrate`. Roast
`start.t` passes on Rakudo only.
```
say Supply.from-list(1,2).start({ $_ * 10 }).flat.list, " ", Supply.from-list(1,2).start({ $_ * 10 }).list.map(*.^name).unique
# rakudo 2026.08: (10 20) (Supply)
```
rakupp 4.0.2: matches. `start` is `map` to a supply backed by a real `start`
Promise, so the blocks overlap and a death inside one is that inner supply's
quit. On a LIVE source the inner supply is a PRESERVING supplier that the settled
Promise emits into, so tapping it returns at once — which is what lets one
started block wait on a Promise a later value keeps (Roast `start.t`); on a
list-backed one the result is awaited where it is asked for.

### S-25  do  D:partial R:? V:spec
The side effect runs before the value is passed on; an exception in it is a quit and ends the stream.
```
say do { my @o; Supply.from-list(1,2,3).do({ die "d" if $_ == 2 }).tap({ @o.push($_) }, quit => { @o.push("quit:" ~ .message) }); @o }
# rakudo 2026.08: [1 quit:d]
```
rakupp 4.0.2: matches, exception path included (S-14).

### S-26  flat: a supply of supplies                               D:yes R:yes V:spec
Every inner supply is subscribed as it arrives; all inner values are emitted; done when the outer and every inner are done.
```
say Supply.from-list(Supply.from-list(1,2), Supply.from-list(3)).flat.list
# rakudo 2026.08: (1 2 3)
```
rakupp 4.0.2: matches — on a live source too, where every inner supply is
subscribed as it arrives and the stream is done when the outer and every inner
are. An Iterable value is spread instead of being subscribed, which is what
`$s.emit([1,2])` through a live `.flat` means.

## D. Combinators

### S-27  merge  D:partial R:partial V:spec
Class form `Supply.merge(@s)` and instance form `$a.merge($b, …)`. No
supplies: an empty supply that is immediately done. Any argument that is
not a defined Supply: `X::Supply::Combinator` with `.combinator eq 'merge'`.
One supply: that supply, sanitized. Otherwise all values as they come, done
when all are done.
```
say Supply.merge().list, " ", (try Supply.merge(Supply, Supply.from-list(1)).list) // $!.^name
# rakudo 2026.08: () X::Supply::Combinator
say Supply.from-list(1,2).merge(Supply.from-list(3)).list.sort, " ", Supply.merge(Supply.from-list(1,2)).list
# rakudo 2026.08: (1 2 3) (1 2)
```
rakupp 4.0.2: matches, and the exception now carries `.combinator` (S-13). Its
message text is our own; Roast does not assert it. Sources that are not all
list-backed answer a SPEC that each tap subscribes for itself, so a merge of live
supplies delivers as the values arrive.

### S-28  reduce: one value at the end, Nil for an empty source    D:partial R:no V:spec
```
say Supply.from-list().reduce(&[+]).list.raku, " ", Supply.from-list(1,2,3).reduce(&[+]).list
# rakudo 2026.08: (Nil,) (6)
```
Docs say "same semantics as List.reduce", which does yield Nil for an empty list; the Supply case is not asserted anywhere.
rakupp 4.0.2: matches.

### S-29  produce                                                  D:yes R:yes V:spec
Every running value; the first value is emitted as-is; an empty source emits nothing.
```
say Supply.from-list(1,2,3).produce(&[+]).list, " ", Supply.from-list().produce(&[+]).list
# rakudo 2026.08: (1 3 6) ()
```
rakupp 4.0.2: matches.

### S-30  migrate  D:partial R:partial V:spec
Values must be Supplies, else `X::Supply::Migrate::Needs`. Each new inner
supply replaces the previous one, whose subscription is closed; values of the
current inner are emitted.
```
say (try Supply.from-list(1,2,3).migrate.list) // $!.^name
# rakudo 2026.08: X::Supply::Migrate::Needs
say Supply.from-list(Supply.from-list(1,2), Supply.from-list(3)).migrate.list
# rakudo 2026.08: (1 2 3)
```
rakupp 4.0.2: matches — on a live source too, where each new inner supply closes
the previous subscription, and a value that is not a Supply raises
X::Supply::Migrate::Needs AT THE EMITTER (Roast `migrate.t` asserts that).

### S-31  classify and categorize emit Pairs of preserving supplies  D:no R:partial V:spec
For each key seen for the first time, a `key => Supply` Pair is emitted, in
order of first appearance; the inner supplies replay their values to a late
tapper (they behave as S-09) and are done when the source is done.
`categorize`'s mapper returns a list of keys and the value goes to each.
Keys are compared by identity (`.WHICH`): `1` and `"1"` are different keys.
```
say Supply.from-list(1,2,3,4).classify(* % 2).list.map({ .key => .value.list }).raku
# rakudo 2026.08: (1 => (1, 3), 0 => (2, 4)).Seq
say Supply.from-list(1,2,3).categorize({ $_ %% 2 ?? <even all> !! <odd all> }).list.map({ .key => .value.list }).raku
# rakudo 2026.08: (:odd((1, 3)), :all((1, 2, 3)), :even((2,))).Seq
say Supply.from-list(1, "1", 1).classify({ $_ }).list.elems
# rakudo 2026.08: 2
```
The identity rule and the preserving property are undocumented.
rakupp 4.0.2: matches — the Pairs come out in order of first appearance, keyed by
`.WHICH`, each carrying a supply of that key's values. On a live source the inner
supplies are PRESERVING suppliers, so a tap that arrives after the values still
gets them (Roast `classify.t` taps them once the source is done); the mapper may
be a Callable, an Associative or a Positional, and a container's `is default(…)`
names its own missing key.

### S-32  comb, all forms  D:no R:yes V:spec/quirk
- `.comb` and `.comb(Int $n)` with `$n <= 1`: one character at a time.
- `.comb(Int $n)`: `$n`-character pieces across chunk boundaries; the leftover is emitted at the end.
- `.comb(Str "")`: characters. `.comb(Str $needle)`: the needle once per occurrence; a partial needle at a chunk end is completed by the next chunk.
- `.comb(Regex)`: the regex is applied to the carried text plus the new chunk; matches are emitted as Str; the text **after the last match** is carried forward, so a match never extends into the next chunk (quirk: `"a1b22"` then `"3c"` gives `"22"` and `"3"`, not `"223"`).
- `.comb(Regex, :match)`: all chunks are collected first, then combed; values are Str.
- `.comb(thing, $limit)`: the first `$limit` results.
```
say Supply.from-list("ab","c").comb.list.raku, " ", Supply.from-list("abc","de").comb(2).list.raku, " ", Supply.from-list("abc").comb(0).list.raku
# rakudo 2026.08: ("a", "b", "c") ("ab", "cd", "e") ("a", "b", "c")
say Supply.from-list("xax","ax").comb("a").list.raku, " ", Supply.from-list("abc").comb("").list.raku, " ", Supply.from-list("a1b22","3c").comb(/\d+/).list.raku
# rakudo 2026.08: ("a", "a") ("a", "b", "c") ("1", "22", "3")
say Supply.from-list("a1b2","2c").comb(/\d+/, :match).list.raku, " ", Supply.from-list("a1b2c3").comb(/\d/, 2).list.raku
# rakudo 2026.08: ("1", "2", "2") ("1", "2")
```
Roast `comb.t` asserts the carry rule with `<old dog jumpso>.comb(/../)` → `<ol dd og ju mp so>`.
rakupp 4.0.2: matches, including the carry quirk: comb works PER CHUNK, joining
only what the previous chunk left over, so a match never grows across a boundary.
Decision on the quirk: KEPT — it is what makes a stream comb streamable.

### S-33  split  D:partial R:partial V:spec
The carried remainder is joined with the new chunk and split; the last piece
is carried; at the end the carried piece is emitted **even when empty**.
Adverbs pass through to `Str.split`; `.split(needle, $limit)` keeps the first `$limit`.
```
say Supply.from-list("a,b", ",c,").split(",").list.raku, " ", Supply.from-list("a,,b").split(",", :skip-empty).list.raku, " ", Supply.from-list("a,b,c").split(",", 2).list.raku
# rakudo 2026.08: ("a", "b", "c", "") ("a", "b") ("a", "b")
```
rakupp 4.0.2: matches.

### S-34  encode and decode  D:no R:yes V:spec/quirk
`.encode($enc = "utf8")` turns each Str into a Blob of that encoding.
`.decode($enc = "utf8")` requires Blob values (a Str value dies with
"No such method 'decode'"), decodes each chunk, and **holds back the last
character** of the decoded text until the next chunk or the end, so that a
grapheme split across chunks is never emitted in halves. A one-character
chunk therefore emits nothing until later.
```
say Supply.from-list("hi").encode.list[0].^name, " ", Supply.from-list("hi").encode("latin-1").list[0].elems
# rakudo 2026.08: utf8 2
say Supply.from-list("ab".encode, "c".encode).decode.list.raku, " ", Supply.from-list("a".encode).decode.list.raku
# rakudo 2026.08: ("a", "b", "c") ("a",)
```
The hold-back rule is undocumented. rakupp 4.0.2: matches. Decision on the quirk:
KEPT — holding the last character back is the only way a grapheme split across
two chunks is never emitted in halves.

### S-35  unique, with :as, :with and :expires                     D:yes R:yes V:spec
Identity is `.WHICH` (so `1` and `"1"` are distinct); `:as` maps to the
key; `:with` is a comparator (values are then compared pairwise against all
seen). `:expires($n)`: a key already seen is emitted again once `$n`
seconds have passed since it was last **emitted**; a falsy `:expires` is
plain unique.
```
say Supply.from-list(1,2,1,3,2).unique.list, " ", Supply.from-list(1,"1",1).unique.list.raku, " ", Supply.from-list(<ab cd ef>).unique(:as(*.chars)).list, " ", Supply.from-list(1,2,3,4).unique(:with(-> $a, $b { $a %% 2 == $b %% 2 })).list
# rakudo 2026.08: (1 2 3) (1, "1") (ab) (1 2)
say do { my $s = Supplier.new; my @o; $s.Supply.unique(:expires(0.05)).tap({ @o.push($_) }); $s.emit(1); $s.emit(1); sleep 0.1; $s.emit(1); @o }
# rakudo 2026.08: [1 1]
```
rakupp 4.0.2: matches.

### S-36  squish: :with receives (previous, current)  D:partial R:partial V:spec
Consecutive duplicates are dropped; `:as` maps to the key; `:with` is
called with the previously **kept** value first and the new value second.
```
say Supply.from-list(1,1,2,2,1).squish.list, " ", Supply.from-list(<a A b>).squish(:as(&lc)).list, " ", Supply.from-list(1,2,4,5).squish(:with(-> $last, $v { $v == $last + 1 })).list
# rakudo 2026.08: (1 2 1) (a b) (1 4)
```
rakupp 4.0.2: matches.

### S-37  repeated                                                 D:yes R:yes V:spec
Emits a value on its second and every later occurrence; `:as` and `:with` as in unique.
```
say Supply.from-list(1,1,2,1,3).repeated.list, " ", Supply.from-list(<a A b>).repeated(:as(&lc)).list
# rakudo 2026.08: (1 1) (A)
```
rakupp 4.0.2: matches.

### S-38  rotor emits Arrays; the cycle repeats  D:partial R:partial V:spec
`rotor(Int)` or `rotor(*@cycle, :partial)` where each cycle element is an
Int or a `elems => gap` Pair; the cycle repeats forever. A negative gap
overlaps; a positive gap skips. A final partial batch is emitted only with
`:partial`. Each batch is an **Array**.
```
say Supply.from-list(1..7).rotor(3 => -1).list.raku
# rakudo 2026.08: ([1, 2, 3], [3, 4, 5], [5, 6, 7])
say Supply.from-list(1..7).rotor(2, :partial).list.raku, " ", Supply.from-list(1..7).rotor(2).list.raku, " ", Supply.from-list(1..7).rotor(2 => 1, :partial).list.raku, " ", Supply.from-list(1..7).rotor(1, 2).list.raku
# rakudo 2026.08: ([1, 2], [3, 4], [5, 6], [7]) ([1, 2], [3, 4], [5, 6]) ([1, 2], [4, 5], [7]) ([1], [2, 3], [4], [5, 6], [7])
```
rakupp 4.0.2: matches.

### S-39  batch: the default is one-element batches                D:partial R:partial V:spec
`:elems($n)` with `$n > 0`: batches of `$n`, a shorter last one on done.
No arguments, or `:elems` of 0 or negative, and no `:seconds`: **every value
in its own one-element batch**. `:seconds($s)`: a batch closes when the
wall clock crosses into a new `$s`-second bucket (bucket = floor(now / s)),
also when `:elems` is reached; `:emit-timed` instead runs a timer that
flushes every `$s` seconds if the batch is non-empty. A non-empty batch is
flushed on done. Batches are Lists.
```
say Supply.from-list(1,2,3).batch.list.raku, " ", Supply.from-list(1..5).batch(:2elems).list.raku, " ", Supply.from-list(1..5).batch(:elems(-1)).list.raku
# rakudo 2026.08: ((1,), (2,), (3,)) ((1, 2), (3, 4), (5,)) ((1,), (2,), (3,), (4,), (5,))
say Supply.from-list(1..5).batch(:seconds(10)).list.raku
# rakudo 2026.08: ((1, 2, 3, 4, 5),)      <- wall-clock dependent: splits if a 10 s boundary is crossed
```
The one-element default is undocumented. rakupp 4.0.2: matches for `:elems` and
the default. `:seconds`/`:emit-timed` on a list-backed source, whose values all
arrive at once, is one batch — no wall-clock bucket is ever crossed.

### S-40  lines and words across chunks                            D:yes R:yes V:spec
`lines` splits on the newline class across chunk boundaries; `:!chomp` keeps
the terminator; a `"\r"` at a chunk end is held back in case `"\n"` follows;
the final partial line is emitted (chomped). `words` splits on whitespace
across chunks; a word at a chunk end continues into the next chunk.
```
say Supply.from-list("a\nb", "c\nd").lines.list.raku, " ", Supply.from-list("a\nb", "c\nd").lines(:!chomp).list.raku, " ", Supply.from-list("a\r", "\nb\n").lines.list.raku, " ", Supply.from-list("a\n").lines.list.raku
# rakudo 2026.08: ("a", "bc", "d") ("a\n", "bc\n", "d") ("a", "b") ("a",)
say Supply.from-list(" a b", "c  d ").words.list.raku, " ", Supply.from-list("a b").words.list.raku
# rakudo 2026.08: ("a", "bc", "d") ("a", "b")
```
rakupp 4.0.2: matches.

### S-41  elems, and elems($seconds) never reports the final count  D:partial R:partial V:bug
`.elems` emits the running count after every value. `.elems($seconds)` is
meant to emit the count at most once per `$seconds` bucket and, at done, the
final count if it was not yet reported. Rakudo's done step compares the
count with itself and therefore never emits: a source that completes within
one bucket reports nothing.
```
say Supply.from-list(<a b c>).elems.list, " ", Supply.from-list(<a b c>).elems(10).list
# rakudo 2026.08: (1 2 3) ()          <- the () is the bug; the intent is (3)
```
Roast `elems.t` sleeps between emits so every reported count coincides with a bucket change and the bug is not exercised. Step two: emit the final count at done when it differs from the last one reported.
rakupp 4.0.2: matches `.elems`, and **deliberately differs** on `.elems($seconds)`:
`(3)`, the final count, where Rakudo emits nothing. The bug is not imitated. The
first bucket is the one the SUBSCRIPTION fell in, which is what makes a source
that says everything inside one bucket report only at done (Roast `elems.t`
sleeps into a fresh bucket before each emit and reads 1, 2, 5).

### S-42  head, tail, skip  D:partial R:partial V:spec/quirk
- `head`: first value, then done (the source is closed). `head(0)` or less: empty. `head(*)`, `head(Inf)`: the invocant. `head(*-n)`: all but the last `n`, which needs done to be known. `head(*+n)`: the invocant.
- `tail`: last value at done. **On an empty source `tail` emits `Any`** (quirk). `tail(n)`: last `n`; `tail(0)` or less: empty; `tail(*)`, `tail(Inf)`: the invocant; `tail(*-n)` is `skip(n)`; `tail(n)` with fewer than `n` values: all of them.
- `skip(n = 1)`: drop the first `n`; `n <= 0` drops nothing; `n` is coerced `Int(Cool)` so `"1"` works.
```
say Supply.from-list(1,2,3).head.list, " ", Supply.from-list(1,2,3).head(0).list, " ", Supply.from-list(1,2,3).head(*).list, " ", Supply.from-list(1,2,3).head(*-1).list, " ", Supply.from-list(1,2,3).head(*+1).list, " ", Supply.from-list(1,2,3).head(Inf).list
# rakudo 2026.08: (1) () (1 2 3) (1 2) (1 2 3) (1 2 3)
say Supply.from-list(1,2,3).tail.list, " ", Supply.from-list().tail.list.raku, " ", Supply.from-list(1,2,3).tail(2).list, " ", Supply.from-list(1,2,3).tail(0).list, " ", Supply.from-list(1,2,3).tail(*-1).list, " ", Supply.from-list(1,2,3).tail(5).list
# rakudo 2026.08: (3) (Any,) (2 3) () (2 3) (1 2 3)
say Supply.from-list(1,2,3).skip.list, " ", Supply.from-list(1,2,3).skip(2).list, " ", Supply.from-list(1,2,3).skip(0).list, " ", Supply.from-list(1,2,3).skip(-1).list, " ", Supply.from-list(1,2,3).skip("1").list
# rakudo 2026.08: (2 3) (3) (1 2 3) (1 2 3) (2 3)
```
rakupp 4.0.2: matches, `tail` on empty included. Decision on that quirk: KEPT —
`Any` is "there was no last value", which is what the caller asked about.

### S-43  min, max, minmax  D:partial R:partial V:spec
Emit on every **strict** improvement; undefined values are ignored. A
one-argument `&by` is a key extractor (keys compared with `cmp`); a
two-argument `&by` is a comparator. `minmax` emits a `Range` of the current
extremes on each change; a `Failure` value is thrown.
```
say Supply.from-list(3,1,2).min.list, " ", Supply.from-list(3,1,2).max.list, " ", Supply.from-list(3,Any,1).min.list, " ", Supply.from-list(<bb a ccc>).min(*.chars).list, " ", Supply.from-list(<bb a ccc>).max(-> $a, $b { $a.chars <=> $b.chars }).list
# rakudo 2026.08: (3 1) (3) (3 1) (bb a) (bb ccc)
say Supply.from-list(3,1,2).minmax.list.raku, " ", Supply.from-list(<bb a ccc>).minmax(*.chars).list.raku
# rakudo 2026.08: (3..3, 1..3) ("bb".."bb", "a".."bb", "a".."ccc")
```
rakupp 4.0.2: matches. A `&by` of one parameter is a key extractor and one of two
is the comparator, told apart by the callable's arity; a Str minmax Range keeps
its endpoint objects, so `.raku`, `.min` and `.max` all read them.

### S-44  grab, reverse, sort, collate, rotate  D:yes R:partial V:spec
`grab(&f)` collects everything until done, then emits each element of
`f(@all)`; `reverse`, `sort`, `sort(&by)`, `collate` are grabs.
`rotate(n > 0)` holds the first `n` values, streams the rest, then emits
the held ones; if fewer than `n` values arrive, it emits `@all.rotate(n)`.
`rotate(n < 0)` is a grab; `rotate(0)` is the invocant.
```
say Supply.from-list(1,2,3).grab(*.reverse).list, " ", Supply.from-list(3,1,2).sort.list, " ", Supply.from-list(3,1,2).sort(-*).list, " ", Supply.from-list(<b a>).reverse.list, " ", Supply.from-list(<b A a>).collate.list
# rakudo 2026.08: (3 2 1) (1 2 3) (3 2 1) (a b) (a A b)
say Supply.from-list(1,2,3,4).rotate.list, " ", Supply.from-list(1,2,3,4).rotate(-1).list, " ", Supply.from-list(1,2).rotate(3).list, " ", Supply.from-list(1,2,3).rotate(0).list
# rakudo 2026.08: (2 3 4 1) (4 1 2 3) (2 1) (1 2 3)
```
rakupp 4.0.2: matches.

### S-45  zip  D:partial R:partial V:spec
Class and instance forms. No supplies: empty. A non-Supply argument:
`X::Supply::Combinator` (`zip`). One supply: that supply, unchanged. Otherwise
an **itemized** List of one value per source is emitted each time every
source has an unconsumed value; `:with` folds those values instead. Done as
soon as a source is done and the others have caught up to its count.
```
say Supply.zip(Supply.from-list(1,2,3), Supply.from-list(4,5)).list.raku, " ", Supply.zip(Supply.from-list(1,2), Supply.from-list(3,4), :with(&[+])).list, " ", Supply.zip().list, " ", (try Supply.zip(Supply).list) // $!.^name
# rakudo 2026.08: ($(1, 4), $(2, 5)) (4 6) () X::Supply::Combinator
say Supply.from-list(1,2,3).zip(Supply.from-list(4,5)).list.raku, " ", Supply.from-list(1,2).zip.list.raku
# rakudo 2026.08: ($(1, 4), $(2, 5)) (1, 2)
```
rakupp 4.0.2: matches, itemization included — on live sources as well, where a
row is emitted once every source has an unconsumed value and the zip is done as
soon as one source is done and the others have caught up.

### S-46  zip-latest, with :initial                                D:yes R:yes V:spec
Once every source has emitted at least once, each new value from any source
emits an itemized List of the latest value of every source. `:initial(@v)`
supplies starting values in source order, so fewer sources need to emit
before the first output. `:with` folds. One supply: that supply.
```
say Supply.from-list(1,2,3).zip-latest(Supply.from-list(4,5)).list.raku, " ", Supply.zip-latest(Supply.from-list(1,2), Supply.from-list(3), :initial(0, 0)).list.raku, " ", Supply.zip-latest(Supply.from-list(1), Supply.from-list(2), :with(&[+])).list
# rakudo 2026.08: ($(3, 4), $(3, 5)) ($(1, 0), $(2, 0), $(2, 3)) (3)
```
The first output shows synchronous sources are subscribed in order, so the
first source has emitted everything before the second starts.
rakupp 4.0.2: matches.

### S-47  throttle by time, throttle by concurrency  D:partial R:yes V:spec
`throttle($elems, $seconds, $delay = 0, :$scheduler, :$control, :$status, :$bleed, :$vent-at)`:
at most `$elems` values pass per `$seconds` tick; excess is buffered and
released up to `$elems` per later tick; the supply is done only after the
buffer has drained. `:bleed` is a Supplier that receives overflow once the
buffer holds `:vent-at` values, and the leftover buffer on done (then it is
done). `:control` is a Supply of strings `"limit:N"`, `"bleed:N"`,
`"status:ID"`, `"vent-at:N"`. `:status` is a Supplier receiving hashes with
keys `allowed bled buffered emitted id limit vent-at`, and a final one with
id `done`.

`throttle($elems, &process, $delay = 0, …)`: at most `$elems` calls of
`process(value)` run concurrently; the supply emits **the finished Promise
objects**, not their results.
```
say do { my $s = Supplier.new; my @o; $s.Supply.throttle(2, 0.05).tap({ @o.push($_) }); $s.emit($_) for 1..5; my @first = @o.clone; sleep 0.2; [@first, @o] }
# rakudo 2026.08: [[1 2] [1 2 3 4 5]]
say Supply.from-list(1,2,3).throttle(2, { $_ * 10 }).list.map(*.^name).unique, " ", Supply.from-list(1,2,3).throttle(2, { $_ * 10 }).list.map(*.result).sort
# rakudo 2026.08: (Promise) (10 20 30)
```
rakupp 4.0.2: matches both forms. The time form is a token bucket — `$elems`
per `$seconds` tick, released at once while tokens remain and queued after that,
done only once the queue has drained. The concurrency form keeps at most `$elems`
calls in flight and emits each PROMISE as it starts, in value order. `:control`
is live in both (`"limit:N"` raises or lowers the allowance mid-stream, which is
how a throttle started at 0 is let go), and `:status` reports at the end.
`:bleed` and `:vent-at` are NOT built.

### S-48  share: a hot supply; early events are lost  D:partial R:no V:spec
`.share` subscribes to the source **immediately** and hands the events to
every tap of the result. Events emitted before a tap exists, including done,
are lost to it, so `.share.list` on an already-finished source never completes.
```
say do { my $s = Supply.from-list(1,2,3).share; my @o; $s.tap({ @o.push($_) }); @o }
# rakudo 2026.08: []
say do { my $sup = Supplier.new; my $sh = $sup.Supply.share; my @o; $sh.tap({ @o.push("a$_") }); $sh.tap({ @o.push("b$_") }); $sup.emit(1); @o }
# rakudo 2026.08: [a1 b1]
```
rakupp 4.0.2: matches.

## E. Coercers

### S-49  Channel                                                  D:yes R:yes V:spec
Values are sent, the channel is closed on done and failed on quit.
```
say do { my $c = Supply.from-list(1,2).Channel; $c.list }
# rakudo 2026.08: (1 2)
```
rakupp 4.0.2: matches, and done/quit now reach the Channel: it closes on done and
fails on quit (Roast `Channel.t`).

### S-50  list, Seq, iterator: subscribe now, pull lazily  D:partial R:? V:spec
The subscription is made when `.list`/`.Seq`/`.iterator` is called, so
values emitted between that call and consumption are kept, not lost. The
List is reified on demand: a quit is rethrown **when the list is reified**,
which may be outside the `try` that called `.list`. `.is-lazy` is False.
```
say do { my $sup = Supplier.new; my $l = $sup.Supply.list; $sup.emit(1); $sup.emit(2); $sup.done; $l }
# rakudo 2026.08: (1 2)
say do { my $l = try { Supply.from-list(1,2).map({ die "m" if $_ == 2; $_ }).list }; my $r = try { $l.elems }; $! ?? "lazy-throws-at-reify:" ~ $!.message !! "eager" }
# rakudo 2026.08: lazy-throws-at-reify:m
say Supply.from-list(1,2).list.is-lazy, " ", Supply.from-list(1,2).Seq.^name, " ", Supply.from-list(1,2).list.^name
# rakudo 2026.08: False Seq List
```
rakupp 4.0.2: matches the first and third probes — a live `.list` subscribes where
it is called and fills as the events arrive. The second **differs**: the quit is
raised by `.list` itself rather than at reification. Deliberate: asking for the
values is where the reason they are missing belongs, and it is strictly earlier.

### S-51  Promise, wait, await: the last value                     D:yes R:yes V:spec
`.Promise` is kept with the **last** emitted value on done (Nil if none) and
broken with the exception on quit. `.wait` awaits it; `await $supply` is the same.
```
say Supply.from-list(1,2,3).wait, " ", Supply.from-list().wait.raku, " ", (await Supply.from-list(1,2)), " ", Supply.from-list(1,2).Promise.result
# rakudo 2026.08: 3 Nil 2 2
```
rakupp 4.0.2: matches.

### S-52  await on a supply that quits during subscription          D:no R:no V:bug
Rakudo returns `Any` and sets no `$!` when the source quits synchronously
while `await` is subscribing. The intent, and `.wait`'s behaviour, is to throw.
```
say do { my $r = try { await Supply.from-list(1).map({ die "w" }) }; "r=" ~ $r.raku ~ " err=" ~ ($! // "unset").^name }
# rakudo 2026.08: r=Any err=Str        <- Str is the "unset" marker: no exception was recorded
```
rakupp 4.0.2: still throws "w" — **deliberately differs from Rakudo**, which
hands back Any and records nothing. Kept, as the sheet asks.

## F. Supply blocks and react

### S-53  The block body runs first; whenever events queue behind it   D:no R:no V:spec
Tapping a `supply` block runs its body synchronously. A `whenever` inside
the body subscribes at once, but values its source emits **while the body
is still running** are queued and delivered after the body returns. The same
holds for a `whenever` nested inside another `whenever` body.
```
say do { my @o; (supply { emit 1; whenever Supply.from-list(2) { emit $_ }; emit 3 }).tap({ @o.push($_) }); @o }
# rakudo 2026.08: [1 3 2]
say do { my @o; (supply { my $t = do whenever Supply.from-list(1,2) { emit $_ }; @o.push($t.^name) }).tap({ @o.push($_) }); @o }
# rakudo 2026.08: [Tap 1 2]
say do { my @o; (supply { whenever Supply.from-list(1,2) { emit $_ * 10; whenever Supply.from-list($_) { emit $_ } } }).tap({ @o.push($_) }); @o }
# rakudo 2026.08: [10 20 1 2]
```
`do whenever` evaluates to the Tap.
rakupp 4.0.2: matches. The activation carries a QUEUE: a `whenever` subscribes
where it stands, and whatever its source delivers while a body is running waits
there, tagged with the subscription it came from. That one mechanism is what
S-54, S-55, S-58 and S-67 are built on.

### S-54  done ends the supply at once  D:yes R:partial V:spec
`done` in the body or in a whenever: the rest of that block does not run,
the done callback fires, every whenever is closed, and later source events
are ignored. A `done` in the body before queued events (S-53) drops them.
```
say do { my @o; (supply { whenever Supply.from-list(1,2,3) -> $v { emit $v; done if $v == 1; @o.push("after-done") } }).tap({ @o.push($_) }); @o }
# rakudo 2026.08: [1]
say do { my @o; (supply { whenever Supply.from-list(1,2,3) { emit $_ * 10; done if $_ == 2 } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [10 20 done]
say do { my @o; (supply { whenever Supply.from-list(1,2) { emit $_ }; done; }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [done]
say do { my @o; my $s = Supplier.new; (supply { whenever $s.Supply { emit $_ }; done; }).tap({ @o.push($_) }, done => { @o.push("done") }); $s.emit(1); @o }
# rakudo 2026.08: [done]
```
rakupp 4.0.2: matches — `done` empties the queue and closes every inner tap.

### S-55  last and next inside a whenever                          D:yes R:yes V:spec
`last` closes that whenever, fires its LAST phaser, and when no whenever
remains the supply is done. `next` skips the rest of the body for that value.
```
say do { my @o; (supply { whenever Supply.from-list(1,2,3) { emit $_; last if $_ == 2 } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 2 done]
say do { my @o; (supply { whenever Supply.from-list(1,2) { emit $_; last; LAST { @o.push("L") } } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 L done]
say do { my @o; (supply { whenever Supply.from-list(1,2,3) { next if $_ == 2; emit $_ } }).tap({ @o.push($_) }); @o }
# rakudo 2026.08: [1 3]
```
rakupp 4.0.2: matches — `last` closes that subscription and drops its backlog alone.

### S-56  An exception in the block is the supply's quit           D:yes R:yes V:spec
The tapper's quit callback receives it; the supply ends. `.list` of such a
supply rethrows at reification (S-50).
```
say do { my @o; (supply { emit 1; die "boom" }).tap({ @o.push($_) }, quit => { @o.push("quit:" ~ .message) }); @o }
# rakudo 2026.08: [1 quit:boom]
say (supply { emit 1; die "boom" }).list
# rakudo 2026.08: boom
#   in block <unit> at -e line 1
```
rakupp 4.0.2: matches, except that the exception is raised by `.list` itself
rather than at reification (S-50) — the values are asked for, and the reason the
stream ended is what comes back.

### S-57  QUIT belongs to the whenever and works like CATCH  D:partial R:partial V:spec
A `QUIT` phaser inside a `whenever` block runs when **that whenever's
source** quits, with the exception as its topic. It behaves like `CATCH`: a
`when` or `default` branch that matches consumes the quit, and the whenever
then counts as done. If no branch matches, or the phaser has no `when` or
`default` at all, the quit propagates to the tapper after the phaser body has
run; the phaser's return value plays no part. `.resume` is not possible.
An exception raised by the whenever body itself is not a source quit: a
`QUIT` at the supply-block level does not see it, and it reaches the tapper
as a quit.
```
say do { my @o; (supply { whenever Supply.from-list(1).map({ die "q" }) { emit $_; QUIT { default { @o.push("handled:" ~ .message) } } } }).tap({ @o.push($_) }, quit => { @o.push("quit") }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [handled:q done]
say do { my @o; (supply { whenever Supply.from-list(1).map({ die "q" }) { emit $_; QUIT { when X::AdHoc { @o.push("h"); 42 } } } }).tap({ @o.push($_) }, quit => { @o.push("quit:" ~ .message) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [h done]
say do { my @o; (supply { whenever Supply.from-list(1).map({ die "q" }) { QUIT { default { @o.push("h1") } } }; whenever Supply.from-list(2) { emit $_ } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [h1 2 done]
say do { my @o; (supply { whenever Supply.from-list(1).map({ die "q" }) { QUIT { when X::OutOfRange { @o.push("no") } } } }).tap({;}, quit => { @o.push("quit:" ~ .message) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [quit:q]
say do { my @o; (supply { whenever Supply.from-list(1).map({ die "q" }) { QUIT { @o.push("seen") } } }).tap({;}, quit => { @o.push("quit:" ~ .message) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [seen quit:q]
say do { my @o; (supply { whenever Supply.from-list(1).map({ die "q" }) { QUIT { @o.push("seen"); Nil } } }).tap({;}, quit => { @o.push("quit:" ~ .message) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [seen quit:q]
say do { my @o; (supply { whenever Supply.from-list(1).map({ die "q" }) { QUIT { .resume } } }).tap({;}, quit => { @o.push("quit:" ~ .message) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [quit:Too late to resume this exception]
say do { my @o; (supply { whenever Supply.from-list(1) { die "in-whenever" }; QUIT { @o.push("QUIT:" ~ .message) } }).tap({;}, quit => { @o.push("quit:" ~ .message) }); @o }
# rakudo 2026.08: [quit:in-whenever]
```
Roast `syntax.t` covers QUIT in a whenever; the no-match and plain-body cases are asserted nowhere.
rakupp 4.0.2: matches, with one wording difference: `.resume` inside a QUIT
reaches the tapper as a quit saying the resume failed, in rakupp's own words
rather than Rakudo's. A QUIT phaser's statements run directly, the way a CATCH
reads its own clauses, so a matching `when`/`default` is visible as such.

### S-58  LAST runs on the source's done, not on `done`  D:yes R:partial V:spec
A `LAST` phaser inside a whenever runs when that source completes,
before the whenever is closed; each whenever's LAST runs in turn. An explicit
`done` skips it. `last` runs it (S-55). A `LAST` at the supply-block or react level does not run.
```
say do { my @o; (supply { whenever Supply.from-list(1,2) { emit $_; LAST { @o.push("L") } }; whenever Supply.from-list(3) { emit $_; LAST { @o.push("L2") } } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 2 L 3 L2 done]
say do { my @o; (supply { whenever Supply.from-list(1) { emit $_; done; LAST { @o.push("L") } } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 done]
say do { my @o; (supply { whenever Supply.from-list(1) { }; LAST { @o.push("outer-LAST") } }).tap({;}, done => { @o.push("done") }); @o }
# rakudo 2026.08: [done]
```
rakupp 4.0.2: matches.

### S-59  CLOSE phasers: reverse order, and their place around done  D:partial R:partial V:quirk
`CLOSE` phasers run when the supply is torn down: on natural completion,
**after** the tapper's done callback; on an explicit `done`, **before** it;
also when the tap is closed. Several CLOSE phasers run in reverse order of
declaration.
```
say do { my @o; (supply { whenever Supply.from-list(1) { emit $_ }; CLOSE { @o.push("C1") }; CLOSE { @o.push("C2") } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 done C2 C1]
say do { my @o; (supply { whenever Supply.from-list(1) { emit $_ }; CLOSE { @o.push("C1") }; CLOSE { @o.push("C2") }; done }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [C2 C1 done]
say do { my @o; my $t = (supply { whenever Supplier.new.Supply { }; CLOSE { @o.push("CLOSE") } }).tap({;}); $t.close; @o }
# rakudo 2026.08: [CLOSE]
```
The done/CLOSE order difference is a quirk; step two may pick one order but must run CLOSE on all three paths.
rakupp 4.0.2: reverse order, on all three paths, and **one** order around done:
CLOSE always follows the tapper's done, so the explicit-done case is
`[done C2 C1]` where Rakudo says `[C2 C1 done]`. Decision on the quirk: a single
teardown order is worth more than reproducing a difference that turns on which
way the supply ended.

### S-60  whenever coerces its argument with Supply()  D:yes R:partial V:spec
An Iterable becomes one event per element; a plain Str or Int is one event; a
Promise emits its result then done, **asynchronously** (not before `.tap`
returns), and a broken Promise is a quit with its cause; `Promise.in`
delivers `True`; a Channel is drained, done on close, quit on fail.
```
say do { my @o; react { whenever (1,2,3) { @o.push($_) }; whenever "str" { @o.push($_) }; whenever 42 { @o.push($_) } }; @o }
# rakudo 2026.08: [1 2 3 str 42]
say do { my @o; react { whenever Promise.kept(42) { @o.push($_) } }; @o }
# rakudo 2026.08: [42]
say do { my @o; (supply { whenever Promise.kept(42) { emit $_ } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: []                  <- timing: the Promise's event has not arrived when tap returns
say do { my @o; (try react { whenever Promise.broken("bad") { @o.push($_) } }) // $!.message }
# rakudo 2026.08: bad
say do { my @o; react { whenever Promise.in(0.01) { @o.push("in:" ~ $_.^name) } }; @o }
# rakudo 2026.08: [in:Bool]
say do { my $c = Channel.new; $c.send(1); $c.send(2); $c.close; my @o; react { whenever $c { @o.push($_) } }; @o }
# rakudo 2026.08: [1 2]
say do { my $c = Channel.new; $c.send(1); $c.fail("cf"); my @o; (try react { whenever $c { @o.push($_) } }) // $!.message }
# rakudo 2026.08: cf
```
rakupp 4.0.2: matches, except that a Promise which is ALREADY kept delivers
synchronously, so `(supply { whenever Promise.kept(42) {…} }).tap(…)` has its
value before `.tap` returns where Rakudo's has not. Deliberate: the value is in
hand, and spawning a worker to hand it over later would buy nothing but a race.

### S-61  Closing the tap of a supply block stops everything       D:yes R:yes V:spec
```
say do { my $s = Supplier.new; my @o; my $t = (supply { whenever $s.Supply { emit $_ } }).tap({ @o.push($_) }); $s.emit(1); $t.close; $s.emit(2); @o }
# rakudo 2026.08: [1]
```
rakupp 4.0.2: matches.

### S-62  react                                                    D:yes R:yes V:spec
`react` runs like a tapped supply block and blocks until done or quit; it
evaluates to Nil; a quit is rethrown from `react`; statements after a
`whenever` run before that whenever's events (S-53); `emit` inside react
warns "Useless use of emit in react".
```
say do { my @o; react { whenever Supply.from-list(1,2,3) { @o.push($_); done if $_ == 2 } }; @o }
# rakudo 2026.08: [1 2]
say do { my @o; react { whenever Supply.from-list(1,2) { @o.push($_) }; @o.push("body-end") }; @o }
# rakudo 2026.08: [body-end 1 2]
say (try { react { die "in-react" } }) // $!.message
# rakudo 2026.08: in-react
say do { my $w = ""; { CONTROL { when CX::Warn { $w = .message; .resume } }; react { emit 1; whenever Supply.from-list(1) { } } }; $w }
# rakudo 2026.08: Useless use of emit in react
```
rakupp 4.0.2: matches.

### S-63  whenever and done outside their scope  D:yes R:partial V:spec
`whenever` outside a `supply` or `react` block is a compile-time error
(`X::WheneverOutOfScope`). `done` called from an ordinary tap callback dies
at run time.
```
say do { my @o; (supply { emit 1; emit 2 }).tap({ @o.push($_); done }); @o }
# rakudo 2026.08: done without supply or react
#   in block  at -e line 1
```
rakupp 4.0.2: matches (the compile-time wording is our own). The two cases are
told apart by whether the supply's BODY is the thing running: `done` reached
from a tapper's callback while the body runs is the error; the same call during
a whenever's delivery is S-64.

### S-64  done from a tap callback of a supply block                D:no R:no V:quirk
When the tap callback of a supply-block supply calls `done`, the supply
ends but the tapper's done callback is **not** invoked.
```
say do { my @o; (supply { whenever Supply.from-list(1,2,3) { emit $_ } }).tap({ @o.push($_); done if $_ == 2 }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 2]
```
rakupp 4.0.2: matches. Decision on the quirk: KEPT — the tapper is already
inside its own callback, and calling its done handler from under it would
re-enter the code that has just ended the stream.

### S-65  Every tap re-runs the block independently                D:yes R:yes V:spec
```
say do { my $sup = supply { my $x = 0; whenever Supply.from-list(1,2) { $x += $_; emit $x } }; ($sup.list, $sup.list).raku }
# rakudo 2026.08: ((1, 3), (1, 3))
say do { my @o; my $s = supply { whenever Supply.from-list(1,2) { emit $_ } }; $s.tap({ @o.push("a$_") }); $s.tap({ @o.push("b$_") }); @o }
# rakudo 2026.08: [a1 a2 b1 b2]
```
rakupp 4.0.2: matches.

### S-66  emit after done inside the body is ignored               D:yes R:yes V:spec
```
say (supply { emit 1; emit 2; done; emit 3 }).list
# rakudo 2026.08: (1 2)
```
rakupp 4.0.2: matches.

### S-67  A supply block completes when the body and every whenever are done   D:yes R:yes V:spec
```
say do { my @o; (supply { whenever Supply.from-list(1,2) { emit $_ }; whenever Supply.from-list(3) { emit $_ } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 2 3 done]
say do { my @o; (supply { whenever Supply.from-list(1) { emit $_ }; whenever Supply.from-list(2) { emit $_; done }; whenever Supply.interval(10) { emit "never" } }).tap({ @o.push($_) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 2 done]
```
rakupp 4.0.2: matches.

### S-68  A whenever's quit ends the whole supply unless handled    D:yes R:yes V:spec
```
say do { my @o; (supply { whenever Supply.from-list(1,2) { emit $_ }; whenever Supply.from-list(1,2).map({ die "q" }) { } }).tap({ @o.push($_) }, quit => { @o.push("quit:" ~ .message) }, done => { @o.push("done") }); @o }
# rakudo 2026.08: [1 2 quit:q]
```
rakupp 4.0.2: matches.

## G. Exception catalogue

### S-69  Types thrown by the Supply family                        D:no R:partial V:spec
| type | thrown by | attributes |
|---|---|---|
| `X::Supply::New` | `Supply.new` with no source | — |
| `X::Supply::Combinator` | `merge`, `zip`, `zip-latest` on a non-Supply | `.combinator` |
| `X::Supply::Migrate::Needs` | `migrate` on a non-Supply value | — |
| `X::WheneverOutOfScope` | `whenever` outside supply/react, at compile time | — |
| `X::AdHoc` | `Supplier.quit(Str)`, payload = the string | `.payload` |

Message texts are Rakudo's and not asserted by Roast except where a Roast
file quotes them; step two words its own.
```
say (try Supply.new) // $!.^name, " ", (do { try Supply.merge(Supply, Supply); $! ?? $!.combinator !! "-" }), " ", (do { my $s = Supplier.new; try { $s.Supply.tap({;}); $s.quit("q") }; $!.^name ~ ":" ~ $!.payload })
# rakudo 2026.08: X::Supply::New merge X::AdHoc:q
```
rakupp 4.0.2: matches — all five types are thrown where the table says, with
`.combinator` and `.payload` where they belong. The message texts are our own.

## Counts

| | items |
|---|---|
| total | 69 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 38 |
| Rakudo bugs (do not imitate) | 3 — S-14, S-41, S-52 |
| quirks (recorded, step two decides) | 7 — S-12, S-21, S-32 regex carry, S-34 hold-back, S-42 tail-empty, S-59 order, S-64 |
| rakupp 4.0.1 differs (before implementation) | 45 |
| rakupp 4.0.1 matches (before implementation) | 17 |
| **rakupp 4.0.2 matches** | **62** |
| rakupp 4.0.2 differs | 7 — and 4 of them on purpose |

Implemented 2026-09-17/18 from this sheet alone, with the Homebrew Rakudo as
oracle and `S17-supply` as the gate: **27 of 58 files → 55**, 560 of 670
assertions → 897 of 902. Rakudo passes 56 of the 58 here, so two files were never
reachable; of the three still open, `watch-path.t` is one Rakudo fails too.
No file regressed, and no section of Roast outside S17 lost a file (S02–S06,
S09, S11, S12, S14, S16, S17 and most of S32 measured against
`docs/status/roast-lists/v4.0.1.list`).

The two reachable files still short, both by one assertion, and why:

| file | the assertion | what it needs |
|---|---|---|
| `basic.t` | a `.tap` block with no parameters must be a signature error | the parser records `-> { … }` and `{ … }` as the same empty parameter list, so the two cannot be told apart here |
| `syntax.t` | a CLOSE phaser assigning to a `my` declared in the supply body | the phaser closes over the block's DEFINITION scope; Rakudo's closes over the body's runtime scope |

Where rakupp 4.0.2 differs from Rakudo, and why:

| item | difference | why |
|---|---|---|
| S-14 | a one-source operator that dies delivers the values before the quit and then STOPS | the grammar of S-06 says nothing follows a quit; Rakudo's later values are the flagged bug |
| S-41 | `.elems($seconds)` reports the final count | same: the sheet's own reading of the intent |
| S-52 | `await` on a quitting supply RAISES | same |
| S-59 | CLOSE always runs after the tapper's done, on all three paths | the quirk left the order open; one order beats reproducing a difference that turns on how the supply ended |
| S-50 | a quit surfaces when `.list` is called, not at reification | earlier, and where the caller asked for the values |
| S-21 | `stable` does not debounce | not built |
| S-47 | `throttle`'s `:bleed` and `:vent-at` | not built (`:control` and `:status` are) |

## Method (how this sheet was produced)

1. `git show 2026.08:src/core.c/<file>` for the five files; read in full.
2. Each behaviour turned into a one-line probe; probes run through
   `perl -e 'alarm 10; exec @ARGV' raku -e …` on the Homebrew 2026.08 binary
   and on `build-arm64/rakupp`; outputs recorded verbatim.
3. D flag from `grep` over `doc/Type/Supply.rakudoc` and neighbours in the
   local docs checkout; R flag from reading the named files in
   `S17-supply/` (58 files); `?` where not looked for.
4. The two probes that depend on wall-clock buckets (S-39 seconds, S-41)
   and the ones with `sleep` are marked; all others are deterministic
   because `from-list` delivers synchronously (S-15).
