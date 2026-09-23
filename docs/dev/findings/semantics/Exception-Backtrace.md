# Exception, Backtrace, Failure and the control-exception protocol — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Exception.rakumod`
(4,010 lines, read in full; only the shared protocol and the attribute
names of the classes below were extracted), `Backtrace.rakumod` (380),
`Failure.rakumod` (196), `control.rakumod` (301), and `traits.rakumod`
(626, read for `is hidden-from-backtrace` only), read on 2026-09-23.
In scope: the Exception object protocol, `die`/`warn`/`fail`, `try` and
`$!`, `CATCH`/`CONTROL`, the `CX::` control exceptions and
`X::ControlFlow`, `Failure`, `Backtrace` and `Backtrace::Frame`, the
phasers that meet exceptions, and the attribute surface of the common
`X::` types. Out of scope: the individual `X::` classes' message prose
(recorded only where Roast asserts it), `$!.pending`, the JSON
exceptions handler, `Exceptions::*`. Oracle: Homebrew Rakudo v2026.08 on
macOS. Compared against Raku++ 4.0.1-84-ga4291988 (build-arm64,
2026-09-23). Format and legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes all 16 Roast
files behind this sheet (`S04-exceptions` 5, `S32-exceptions` 2,
`S04-statements/try.t`, `S32-basics/warn.t`, `S04-exception-handlers`
`catch.t` and `control.t`, and the phaser files `pre-post.t`,
`keep-undo.t`, `enter-leave.t`, `in-loop.t`, `next.t`); Raku++ passes 3
(`control_across_runloop.t`, `fail-6e.t`, `next.t`). The rules below are
what those files and real programs lean on: what a bare `try` and a
`try` with its own `CATCH` do with an exception, what `$!` holds before,
during and after, which exceptions resume, what a caught control
exception does to its loop, which value contexts disarm a Failure and
which detonate it, what a backtrace contains and hides, and which
exception type with which attributes each misuse produces. 15 of the 36
items are neither fully documented nor fully asserted by Roast. Three
Rakudo behaviours are recorded as bugs and five as quirks; step two
should not imitate the bugs.

Every probe ran with `alarm 10` and standard input closed, in a fresh
sandbox directory per engine. No recorded output contains a sandbox
path: the EVAL pseudo-file name that two probes touch is reduced to its
basename with its counter masked as `N` by the probe itself, and the
setting's file names are reported as `SETTING`.

## A. The Exception object

### EX-01  A bare Exception, and a subclass without `message`          D:partial R:partial V:spec
`Exception.new` and `E.new` (E `is Exception` with no `message` method)
construct a defined object. Calling `.message` on either throws an
`X::AdHoc` (its payload is the stub-code text). `.Str` of such an
object, thrown or not, is `Something went wrong in (E)` — the type
object's gist in parentheses; `.gist` is `Unthrown E with no message`
before a throw and, after one, `Died with E` followed by the backtrace
lines (the anonymous block prints as `in block  at`, with an empty
name). `.backtrace` is Nil before a throw and a `Backtrace` after. The
object caught is the very instance thrown (`===`). `.throw` and
`.resume` on a type object throw `X::Parameter::InvalidConcreteness`;
`.resume` on an instance that was never thrown throws `X::AdHoc`;
`.rethrow` on an instance that was never thrown throws it (caught as
E). `.is-compile-time` is False for every runtime exception.
```
say do { class E is Exception {}; my $e = E.new; my @r = $e.^name, $e.defined, $e.Str, $e.gist, $e.backtrace.raku, (try { $e.throw; "no" }) // $!.^name; my $t = $!; @r.push($t === $e, $t.Str, $t.gist.lines[0], $t.gist.lines[1], $t.backtrace.^name, (try $e.message) // $!.^name ~ ":" ~ $!.message); @r.push(Exception.new.Str, Exception.new.gist, (try Exception.new.message) // $!.^name ~ ":" ~ $!.message); try { Exception.new.throw }; @r.push($!.Str, $!.gist.lines[0], $!.gist.lines.elems); @r.push((try Exception.throw) // $!.^name, (try E.throw) // $!.^name, (try E.resume) // $!.^name, (try E.new.resume) // $!.^name ~ ":" ~ $!.message, (try E.new.rethrow) // $!.^name, $e.is-compile-time, Exception.new.is-compile-time, X::AdHoc.new.is-compile-time); @r.join(" | ") }
# rakudo 2026.08: E | True | Something went wrong in (E) | Unthrown E with no message | Nil | E | True | Something went wrong in (E) | Died with E |   in block  at -e line 1 | Backtrace | X::AdHoc:Stub code executed | Something went wrong in (Exception) | Unthrown Exception with no message | X::AdHoc:Stub code executed | Something went wrong in (Exception) | Died with Exception | 3 | X::Parameter::InvalidConcreteness | X::Parameter::InvalidConcreteness | X::Parameter::InvalidConcreteness | X::AdHoc:Can only resume an exception object | E | False | False | False
```
rakupp 4.0.1-84: differs — the line died at `.is-compile-time` (missing), and with that call made safe: `.Str` of a message-less exception is `""`, `.gist` is `E.new(message => Any)`, `.message` returns Nil instead of throwing, `.backtrace` of an unthrown exception is a List of hashes (not Nil), `.throw` on a type object is `X::Method::NotFound`, and `.resume` on an unthrown instance is `X::Parameter::InvalidConcreteness`.

### EX-02  throw, rethrow, resume: where control goes                  D:partial R:yes V:spec
`.resume` inside a CATCH continues right after the statement that
threw and never returns to the handler (an assignment from its result
never happens). `.resume` on the exception left in `$!` after the `try`
has finished throws `X::AdHoc`. A `try` whose block contains a CATCH has
no implicit handler: an exception thrown from inside that CATCH — by
`die`, by `.rethrow` or by `.throw` — leaves the `try` and reaches the
next enclosing handler. `.rethrow` on an exception that was never
thrown throws it with a fresh `Backtrace`.
```
say do { my $r = ""; try { die "a"; $r ~= "after"; CATCH { default { $r ~= "c"; .resume } } }; my $r2 = do { try { die "b" }; (try $!.resume) // $!.^name ~ ":" ~ $!.message }; my $r3 = do { try { try { { die "z" }(); CATCH { default { .rethrow } } }; "inner-survived" }; $!.message }; my $r4 = (try { X::AdHoc.new(:payload<u>).rethrow; "no" }) // $!.^name ~ ":" ~ $!.message ~ ":" ~ $!.backtrace.^name; my $r5 = do { my $v = "unset"; try { try { die "r5"; CATCH { default { $v = .resume; } } } }; $v }; my $r6 = do { try { try { die "r6"; CATCH { default { .throw } } }; "no" }; $!.message }; ($r, $r2, $r3, $r4, $r5, $r6).join(" | ") }
# rakudo 2026.08: cafter | X::AdHoc:Too late to resume this exception | z | X::AdHoc:u:Backtrace | unset | r6
```
rakupp 4.0.1-84: differs — `.resume` in a CATCH exits the try instead of continuing (`c`, not `cafter`), `.resume` after the try is `X::Parameter::InvalidConcreteness`, and `.backtrace` is a List.

### EX-03  The backtrace across rethrow and throw; identity             D:partial R:no V:spec
`.rethrow` keeps the frames of the original throw. `.throw` from inside
a CATCH records a new backtrace: the handler's own frames come first and
the original frames follow them, because the CATCH runs in the dynamic
scope of the throw. The object caught after a rethrow is the same object
(`.WHICH` equal, `===` True); an exception's WHICH is an `ObjAt`.
`Backtrace.gist` is `Backtrace(N frames)`.
```
say do { sub inner { die "deep" }; sub mid { inner(); CATCH { default { .rethrow } } }; try { mid() }; my $bt1 = $!.backtrace.list.map(*.subname).join(","); sub mid2 { inner(); CATCH { default { .throw } } }; try { mid2() }; my $bt2 = $!.backtrace.list.map(*.subname).join(","); my $e = X::AdHoc.new(payload => "id"); try { $e.throw }; my $w1 = $!.WHICH; try { $!.rethrow }; my $w2 = $!.WHICH; try { $e.throw }; ($bt1, $bt2, $w1 eq $w2, $!.WHICH eq $w1, $! === $e, $!.backtrace.gist, $e.backtrace.gist, $!.WHICH.^name).join(" | ") }
# rakudo 2026.08: throw,die,inner,mid,,,<unit> | throw,,,,throw,die,inner,mid2,,,<unit> | True | True | True | Backtrace(4 frames) | Backtrace(4 frames) | ObjAt
```
rakupp 4.0.1-84: differs — no setting frames (`throw`, `die`) are listed, a `.throw` inside a CATCH adds no handler frames, and `Backtrace.gist` prints the frames instead of `Backtrace(N frames)`.

### EX-04  X::AdHoc: payload, message, from-slurpy, die, fail, Failure   D:yes R:partial V:spec
`X::AdHoc.new` has payload `"Unexplained error"`; `.message` and `.Str`
are the payload's Str, `.gist` of an unthrown X::AdHoc is just the
message, `.Numeric` numifies the payload, `.raku` is
`X::AdHoc.new(payload => 42)`. `from-slurpy(3, False, "x")` joins the
pieces with no separator into the message and keeps them as a
`Capture+{X::AdHoc::SlurpySentry}` payload; an Array payload
stringifies with spaces. `.die` throws the exception; `.fail` exits the
calling routine with an unhandled Failure wrapping it; `.Failure`
coerces to an unhandled Failure whose `.exception` is the same object.
```
say do { my $e = X::AdHoc.new(payload => 42); ($e.message, $e.message.^name, $e.payload.^name, $e.Numeric, $e.Str, $e.gist, X::AdHoc.new.message, X::AdHoc.new.payload.raku, do { try { $e.die }; $!.^name ~ ":" ~ ($!.?payload // "-") }, do { sub f { $e.fail; "after" }; my $x = f(); my $h = $x.handled; $x.so; $x.^name ~ ":" ~ $h ~ ":" ~ $x.exception.payload }, do { my $f = $e.Failure; my $h = $f.handled; $f.so; $f.^name ~ ":" ~ $h ~ ":" ~ ($f.exception === $e) }, X::AdHoc.from-slurpy(3, False, "x").message, X::AdHoc.from-slurpy(3, "x").payload.^name, X::AdHoc.new(payload => [1, 2]).message, X::AdHoc.new(payload => (1, 2)).Str, X::AdHoc.new(payload => 42).gist, X::AdHoc.new(payload => 42).raku).join(" | ") }
# rakudo 2026.08: 42 | Str | Int | 42 | 42 | 42 | Unexplained error | "Unexplained error" | X::AdHoc:42 | Failure:False:42 | Failure:False:True | 3Falsex | Capture+{X::AdHoc::SlurpySentry} | 1 2 | 1 2 | 42 | X::AdHoc.new(payload => 42)
```
rakupp 4.0.1-84: differs — `.die` does not throw and `.fail` throws instead of returning a Failure, so the line died in `f`; `.payload` and `.from-slurpy` are unmeasured.

### EX-05  die: Str, object, list, nothing, Nil, type object, Failure   D:partial R:yes V:spec
`die "x"` throws X::AdHoc with the Str as payload; `die 42` keeps the
Int (`.message` "42", `.Numeric` Int, `.payload + 1` works); `die
[1, 2]` keeps the Array (message `1 2`); `die "a", "b", 3` joins the
pieces with no separator (`ab3`, a SlurpySentry Capture payload). `die`
with no argument throws X::AdHoc "Died" when `$!` in the calling scope
is undefined and rethrows `$!` when it is set (a sub has its own,
undefined `$!`). `die Nil` gives an X::AdHoc with payload Nil and an
empty message (and a warning). `die` on a type object (Exception,
X::NYI, Failure) throws X::AdHoc "Died with undefined <name>". `die` of
an Exception instance throws that instance (its attributes survive);
`die` of a Failure throws the Failure's exception and does NOT mark the
Failure handled; `die "a", X::NYI.new(...)` joins the Str and the
exception's message into one X::AdHoc.
```
say do { sub t(&c) { try { c() }; $!.^name ~ ":" ~ $!.message.raku ~ ":" ~ $!.payload.^name }; (t({ die "x" }), t({ die 42 }), t({ die [1, 2] }), t({ die "a", "b", 3 }), t({ die }), (quietly t({ die Nil })), t({ die Exception }), t({ die X::NYI }), do { try { die X::NYI.new(:feature<f>) }; $!.^name ~ ":" ~ $!.feature }, do { my $f = (sub { fail "ff" })(); my $h0 = $f.handled; try { die $f }; my $h = $f.handled; $f.so; $!.^name ~ ":" ~ $!.message ~ ":" ~ $h0 ~ ":" ~ $h }, do { try { die Failure }; $!.message }, t({ die "a", X::NYI.new(:feature<q>) }), do { try { die "p" }; try { die }; $!.message }, do { try { die 42 }; $!.Str ~ ":" ~ $!.Numeric.^name ~ ":" ~ ($!.payload + 1) }, do { try { die "q" }; sub inner { die }; try { inner() }; $!.message }).join(" | ") }
# rakudo 2026.08: X::AdHoc:"x":Str | X::AdHoc:"42":Int | X::AdHoc:"1 2":Array | X::AdHoc:"ab3":Capture+{X::AdHoc::SlurpySentry} | X::AdHoc:"Died":Str | X::AdHoc:"":Nil | X::AdHoc:"Died with undefined Exception":Str | X::AdHoc:"Died with undefined X::NYI":Str | X::NYI:f | X::AdHoc:ff:False:False | Died with undefined Failure | X::AdHoc:"aq not yet implemented. Sorry.":Capture+{X::AdHoc::SlurpySentry} | p | 42:Int:43 | Died
```
rakupp 4.0.1-84: differs — a list keeps only its first element (`"a"`), a type object gives an empty message and keeps the type as payload, the Failure's message shows its internals, `die Failure` has an empty message, `.Numeric` is a Num, and `die()` in a sub rethrows the caller's `$!` (`q`) instead of "Died".

### EX-06  Non-Str payloads, the gist layout, :without-backtrace         D:partial R:yes V:spec
`die $obj` (a non-Exception object with a Str method) gives X::AdHoc
whose `.payload === $obj` and whose `.Str`, `.message` and first gist
line are the object's Str. `die($obj, 42)` keeps the pieces as a
SlurpySentry Capture (`[0] === $obj`, `[1]` 42) and stringifies them
joined (`pstr42`); `die(X::NYI.new, "fee")` is likewise an X::AdHoc
whose `payload[0]` is the X::NYI. The gist of a thrown `die 42` has
three lines: the message, `  in block  at -e line 1` (the anonymous
block) and `  in block <unit> at -e line 1`; a two-line message gives
four. `die :without-backtrace, "wb"` answers `.backtrace` Nil and
`.without-backtrace` True, and its gist is two lines.
```
say do { class P { method Str { "pstr" } }; my $p = P.new; try die $p; my @r = $!.^name, ($!.payload === $p), $!.Str, $!.message, $!.gist.lines[0]; try die($p, 42); @r.push($!.payload.^name, $!.payload[0] === $p, $!.payload[1], $!.Str, $!.message); try die(X::NYI.new(:feature<fee>), "fee"); @r.push($!.^name, $!.payload[0].^name); try die 42; @r.push($!.payload.^name, $!.Str, $!.gist.lines.elems, $!.gist.lines[1]); try die "multi\nline"; @r.push($!.gist.lines.elems, $!.message.lines.elems); try die :without-backtrace, "wb"; @r.push($!.backtrace.raku, $!.gist.lines.elems, $!.without-backtrace); @r.join(" | ") }
# rakudo 2026.08: X::AdHoc | True | pstr | pstr | pstr | Capture+{X::AdHoc::SlurpySentry} | True | 42 | pstr42 | pstr42 | X::AdHoc | X::NYI | Int | 42 | 3 |   in block  at -e line 1 | 4 | 2 | Nil | 2 | True
```
rakupp 4.0.1-84: differs — `die $obj` throws the object itself (no X::AdHoc, no `.payload`), so the line died at the second field; with the attribute calls made safe, `die($obj, 42)` keeps only `$obj`, `die(X::NYI.new, "fee")` throws the X::NYI, and `:without-backtrace` leaves a backtrace.

## B. try and `$!`

### EX-07  What `try` returns                                            D:partial R:partial V:quirk
`try { 42 }` is 42 and `try 42` is 42; a caught exception makes the
try Nil, with `$!` set, in both forms. A Failure produced inside a try
is thrown there (a try is a fatal scope): `try { +"abc" }`, `try
+"abc"`, `try { fail "ff" }` and `try { ... }` are all Nil with `$!`
`X::Str::Numeric`, `X::AdHoc`, `X::StubCode`; a sunk Failure followed by
more statements still ends the try (`try { +"abc"; "after" }` is Nil).
Arrays, hashes and lists pass through; `try { Nil }` is Nil and leaves
`$!` Any. The quirk: a CATCH block that is the last statement of the
try makes the value Nil even when nothing was thrown (`try { 42.self;
CATCH { default { } } }` is Nil), while the same CATCH placed first
leaves 42.
```
say do { my @r; @r.push((try { 42 }).raku, (try { die "x" }).raku, (try { die "x"; CATCH { default { } } }).raku, (try 42).raku, (try die "y").raku, $!.^name); try { 1 }; @r.push($!.raku, (try { +"abc" }).raku, $!.^name, (try +"abc").raku, (try { fail "ff" }).raku, $!.^name ~ ":" ~ $!.message, (try { ... }).raku, $!.^name, (try { CATCH { default { } }; 42 }).raku, (try { 42.self; CATCH { default { } } }).raku, (try { my @a = 1, 2; @a }).raku, (try { my %h = a => 1; %h }).raku, (try { (1, 2) }).raku, (try { Nil }).raku, $!.raku, (try { +"abc"; "after" }).raku, (try { +"abc".self; "after2" }).raku); @r.join(" | ") }
# rakudo 2026.08: 42 | Nil | Nil | 42 | Nil | X::AdHoc | Any | Nil | X::Str::Numeric | Nil | Nil | X::AdHoc:ff | Nil | X::StubCode | 42 | Nil | [1, 2] | {:a(1)} | (1, 2) | Nil | Any | Nil | Nil
```
rakupp 4.0.1-84: differs — `$!` after a successful try is Nil (not Any), `try { fail "ff" }` is `X::ControlFlow::Return` (its `fail` outside a routine is a return), and a trailing CATCH leaves 42.

### EX-08  `$!`: Nil before, Any after a success, one per routine       D:partial R:partial V:spec
Before any try `$!` is Nil. A try that catches sets it to the
exception; a try that succeeds resets it to Any (undefined). Bare
blocks and `do` blocks share the enclosing routine's `$!`: a try inside
them sets it and a successful try inside them resets it. A sub has its
own `$!`: its try never touches the caller's, and a sub that only reads
`$!` sees its own Nil even while the caller's is set.
```
say do { my @r = $!.raku; try { 1 }; @r.push($!.raku); try { die "x" }; @r.push($!.raku); try { 1 }; @r.push($!.raku, $!.defined); my $v = do { try { die "in" }; $!.raku }; @r.push($v, $!.raku); { try { die "blk" } }; @r.push($!.raku); try { die "o1" }; { try { die "o2" } }; @r.push($!.raku); try { die "o3" }; { try { 1 } }; @r.push($!.raku); sub f { try { die "sub" }; $!.message }; try { die "o4" }; @r.push(f(), $!.raku); sub g { $!.raku }; @r.push(g()); @r.join(" | ") }
# rakudo 2026.08: Nil | Any | X::AdHoc.new(payload => "x") | Any | False | X::AdHoc.new(payload => "in") | X::AdHoc.new(payload => "in") | X::AdHoc.new(payload => "blk") | X::AdHoc.new(payload => "o2") | Any | sub | X::AdHoc.new(payload => "o4") | Nil
```
rakupp 4.0.1-84: differs — a success leaves `$!` untouched (never Any), a try inside a bare block does not set the enclosing `$!` (`blk` and `o2` are missing, `o1` and `o3` survive), and a sub reading `$!` sees the caller's (`o4`).

### EX-09  Nested try                                                    D:no R:no V:spec
Inside an outer try, an inner try sets `$!` and the rest of the outer
block sees it (`die "outer" ~ $!.message` gives `outernest`); when the
outer try then succeeds `$!` is Any, whatever the inner did; a `die`
with no argument after an inner try rethrows the inner's exception.
```
say do { my @r; try { try { die "nest" }; die "outer" ~ $!.message }; @r.push($!.message); try { try { die "n2" }; 1 }; @r.push($!.raku); try { die "o6" }; try { try { 1 }; 2 }; @r.push($!.raku); try { die "o7" }; try { try { die "n3" }; @r.push("inner:" ~ $!.message) }; @r.push($!.raku); try { die "o8" }; try { try { die "n4" }; die }; @r.push($!.message); @r.join(" | ") }
# rakudo 2026.08: outernest | Any | Any | inner:n3 | Any | n4
```
rakupp 4.0.1-84: differs — Nil where Rakudo has Any after the outer success.

### EX-10  CATCH: matching, and what escapes                             D:partial R:yes V:spec
A CATCH handles the exception when a `when` or `default` matches: `when`
by type, by regex (against the exception's Str) and by a Str (equal to
its Str) all match. After a match the block that owns the CATCH exits —
the statements after the throw never run — and execution continues
after that block. A `try` whose block has a CATCH has no implicit
handler: an exception the CATCH does not match (a `when` of another
type, an empty CATCH, a CATCH body without `when`/`default`), or one
thrown inside the CATCH (`die`, `.rethrow`), propagates out of the try.
Inside the CATCH `$_` is the exception, `$!` is the enclosing scope's
`$!` (unchanged) and `$/` is Nil.
```
say do { my @r; { die "one"; @r.push("no"); CATCH { when X::AdHoc { @r.push("adhoc:" ~ .message) } } }; @r.push("after1"); try { try { die "two"; CATCH { when X::NYI { @r.push("nyi") } } }; @r.push("no2") }; @r.push("esc:" ~ $!.message); try { try { die "abc"; CATCH { when /b/ { @r.push("rx") } } }; @r.push("in3") }; @r.push($!.raku); try { try { die "three"; CATCH { when "three" { @r.push("str") } } }; @r.push("in4") }; @r.push($!.raku); try { try { die "four"; CATCH { default { @r.push("def:" ~ $_.^name ~ ":" ~ $!.raku ~ ":" ~ $/.raku) } } }; @r.push("in5") }; try { try { die "five"; CATCH { } }; @r.push("no6") }; @r.push("empty:" ~ $!.message); try { try { die "six"; CATCH { @r.push("body:" ~ .message) } }; @r.push("no7") }; @r.push("six:" ~ $!.message); try { try { die "seven"; CATCH { default { die "re:" ~ .message } } }; @r.push("no8") }; @r.push($!.message); try { try { die "eight"; CATCH { default { .rethrow } } }; @r.push("no9") }; @r.push($!.message); @r.join(" | ") }
# rakudo 2026.08: adhoc:one | after1 | esc:two | rx | in3 | Any | str | in4 | Any | def:X::AdHoc:Nil:Nil | in5 | empty:five | body:six | six:six | re:seven | eight
```
rakupp 4.0.1-84: differs — inside the CATCH `$!` is the exception and `$/` is the block's Match; `$!` after the outer success is Nil.

### EX-11  What `.resume` resumes                                         D:partial R:partial V:spec
Everything thrown at the language level resumes: `die`, a typed
`.throw`, a failed typed assignment, and every Failure a fatal scope
turned into a throw (`+"abc"`, `1 div 0`, an index out of range, `"a"
+ 1`). The one refusal is the engine's own "no such method"
(`X::Method::NotFound`): `.resume` on it throws `X::AdHoc`, which,
thrown inside the CATCH, escapes the try. Resumption continues in the
innermost frame: a sub that dies and then returns a value completes
with that value, and an assignment made in the CATCH is overwritten by
it.
```
say do { my @r; sub R(&c) { try { try { c(); @r.push("r"); CATCH { default { .resume } } }; @r.push("k") }; $! ?? $!.^name ~ ":" ~ $!.message.substr(0, 34) !! "resumed" }; my @o = R({ 1.nosuch }), R({ my Int $x = "s" }), R({ die "x" }), R({ X::NYI.new(:feature<f>).throw }), R({ +"abc" }), R({ 1 div 0 }), R({ [1][5].self }), R({ "a" + 1 }); sub bad { die "in-bad"; "not returning" }; my $ret = do { my $v = "init"; { CATCH { default { $v = "from-catch"; .resume } }; $v = bad(); @r.push("got:$v") }; $v }; @o.push(@r.join(","), $ret.raku); @o.join(" | ") }
# rakudo 2026.08: X::AdHoc:This exception is not resumable | resumed | resumed | resumed | resumed | resumed | resumed | resumed | r,k,r,k,r,k,r,k,r,k,r,k,r,k,got:not returning | "not returning"
```
rakupp 4.0.1-84: differs — the "no such method" case resumes, the first six cases exit the try instead of resuming (`k` without `r`), and the CATCH's assignment wins (`from-catch`).

### EX-12  CATCH inside a loop body                                       D:no R:partial V:spec
`next`, `last` and `redo` work from inside a CATCH in a loop body; a
handled exception with none of them ends the iteration and the loop
continues; `next` from a CATCH in a `while` re-evaluates the condition.
```
say do { my @r; for 1..3 { CATCH { default { @r.push("c" ~ .message); next } }; die "d" if $_ == 2; @r.push($_) }; @r.push("-"); for 1..3 { CATCH { default { @r.push("l"); last } }; die "d" if $_ == 2; @r.push($_) }; @r.push("-"); for 1..3 { CATCH { default { @r.push("h") } }; die "d" if $_ == 2; @r.push($_) }; @r.push("-"); my $n = 0; for 1..2 { CATCH { default { @r.push("r"); $n++; redo if $n < 2 } }; die "d" if $n < 2; @r.push("i$_") }; @r.push("-"); my $s = 0; while $s < 3 { CATCH { default { @r.push("w"); $s++; next } }; $s++; die "w" if $s == 2; @r.push("s$s") }; @r.join(" | ") }
# rakudo 2026.08: 1 | cd | 3 | - | 1 | l | - | 1 | h | 3 | - | r | r | i2 | - | s1 | w
```
rakupp 4.0.1-84: matches.

### EX-13  A block's value after its CATCH handled                       D:no R:partial V:quirk
When a CATCH handles the exception the block's value is undefined: Nil
when the CATCH is the block's last statement, Any otherwise; the
handler's own value is never the block's value. After `.resume` the
block runs to its end and its last value is the result (`"after"`).
The same holds for a sub body.
```
say do { ((try { 42.self; CATCH { default { } } }).raku, (try { CATCH { default { } }; 42 }).raku, (do { die "x"; CATCH { default { "cv" } } }).raku, (do { CATCH { default { "hv" } }; die "x"; "unreached" }).raku, (do { CATCH { default { .resume } }; die "x"; "after" }).raku, (do { { die "in" }(); "u".self; CATCH { default { } } }).raku, (sub { die "s"; CATCH { default { "cv" } } })().raku, (sub { CATCH { default { "cv" } }; die "s"; "u" })().raku).join(" | ") }
# rakudo 2026.08: Nil | 42 | Nil | Nil | "after" | Nil | Nil | Nil
```
rakupp 4.0.1-84: differs — a trailing CATCH does not change a completed block's value (42), and after `.resume` the value is Nil instead of `"after"`.

## C. warn and the control exceptions

### EX-14  warn, CX::Warn and CONTROL                                     D:partial R:yes V:spec
`warn` throws a `CX::Warn` (an `X::Control`) whose `.message` is the
arguments joined with no separator (an Array stringifies with spaces:
`42, "x", [1, 2]` gives `42x12`) and which carries a `.backtrace`;
with no arguments, or an empty message, the message is `Warning:
something's wrong`; a trailing newline is kept. A CONTROL with `when
CX::Warn { .resume }` continues after the `warn`, which then returns 0.
A CONTROL that matches but does not resume — or a `default` — exits the
block. `quietly` handles the warning before an outer CONTROL sees it.
```
say do { my @r; { CONTROL { when CX::Warn { @r.push("w:" ~ .message ~ ":" ~ .^name); .resume } }; my $v = warn "hello"; @r.push("v:" ~ $v.raku); warn; warn 42, "x", [1, 2]; warn "a", "b"; @r.push("alive") }; { CONTROL { when CX::Warn { @r.push("unresumed"); } }; warn "u"; @r.push("after-u") }; { CONTROL { default { @r.push("d:" ~ .^name) } }; warn "dd"; @r.push("after-dd") }; { CONTROL { when CX::Warn { @r.push("wr:" ~ .backtrace.^name ~ ":" ~ .message.raku); .resume } }; warn "b\n"; warn ""; }; { CONTROL { when CX::Warn { @r.push("q"); .resume } }; quietly { warn "quiet" }; @r.push("qa") }; @r.join(" | ") }
# rakudo 2026.08: w:hello:CX::Warn | v:0 | w:Warning: something's wrong:CX::Warn | w:42x12:CX::Warn | w:ab:CX::Warn | alive | unresumed | d:CX::Warn | wr:Backtrace:"b\n" | wr:Backtrace:"Warning: something's wrong" | qa
```
rakupp 4.0.1-84: differs — `warn` returns True, an Array argument stringifies as `[1 2]`, `.backtrace` is a List, and `warn ""` keeps the empty message.

### EX-15  note versus warn on standard error                            D:yes R:yes V:spec
`note` prints its arguments and a newline to `$*ERR` and returns True;
with no argument it prints `Noted`. An unhandled `warn` prints the
message and then `  in block <unit> at -e line 1`, and execution
continues; it returns 0. `quietly warn` prints nothing. (The probe runs
the program in a child so that the two streams can be read apart.)
```
my $p = run($*EXECUTABLE, "-e", 'note "n1"; warn "w1"; my $r = note "n2"; my $w = warn "w2"; say $r.raku, ":", $w.raku; my $n = note; say $n.raku; quietly warn "q"; say "end"', :out, :err); say $p.err.slurp.raku, " ", $p.out.slurp.raku
# rakudo 2026.08: "n1\nw1\n  in block <unit> at -e line 1\nn2\nw2\n  in block <unit> at -e line 1\nNoted\n" "Bool::True:0\nBool::True\nend\n"
```
rakupp 4.0.1-84: differs in one field — `warn` returns True; everything printed matches.

### EX-16  next, last, redo seen by CONTROL                               D:partial R:partial V:quirk
`next`, `last` and `redo` reach a CONTROL in the loop body as
`CX::Next`, `CX::Last`, `CX::Redo`. A handler that matches (by type or
`default`) and neither resumes nor rethrows swallows the control: the
body exits and the loop goes on — a caught `last` does not end the
loop. `.rethrow` restores the normal effect; a CONTROL that does not
match lets them through. The quirk: `.resume` on any of the three
throws `X::AdHoc` (not resumable), which then escapes as an ordinary
exception.
```
say do { my @r; for 1..3 { CONTROL { when CX::Next { @r.push("n") } }; next if $_ == 2; @r.push($_) }; @r.push("-"); for 1..3 { CONTROL { when CX::Last { @r.push("l") } }; last if $_ == 2; @r.push($_) }; @r.push("-"); for 1..3 { CONTROL { when CX::Next { @r.push("nr"); .rethrow } }; next if $_ == 2; @r.push($_) }; @r.push("-"); for 1..3 { CONTROL { when CX::Last { @r.push("lr"); .rethrow } }; last if $_ == 2; @r.push($_) }; @r.push("-"); for 1..3 { CONTROL { default { @r.push("d:" ~ .^name) } }; last if $_ == 2; @r.push($_) }; @r.push("-"); for 1..3 { CONTROL { when CX::Warn { .resume } }; last if $_ == 2; @r.push($_) }; @r.push("-"); @r.push((try { for 1..3 { CONTROL { when CX::Next { .resume } }; next }; "ok" }) // $!.^name ~ ":" ~ $!.message); @r.push((try { for 1..3 { CONTROL { when CX::Last { .resume } }; last }; "ok" }) // $!.message); @r.push((try { my $n = 0; for 1..1 { CONTROL { when CX::Redo { .resume } }; $n++; redo if $n == 1 }; "ok" }) // $!.message); @r.push((try { my $n = 0; for 1..1 { CONTROL { when CX::Redo { @r.push("redo"); .rethrow } }; $n++; redo if $n == 1 }; "ok$n" }) // $!.message); @r.join(" | ") }
# rakudo 2026.08: 1 | n | 3 | - | 1 | l | 3 | - | 1 | nr | 3 | - | 1 | lr | - | 1 | d:CX::Last | 3 | - | 1 | - | X::AdHoc:This exception is not resumable | This exception is not resumable | This exception is not resumable | redo | ok2
```
rakupp 4.0.1-84: differs — CONTROL never sees CX::Next, CX::Last or CX::Redo (no handler runs, the loops behave normally), and `.resume` on them is accepted.

### EX-17  return, take, emit, succeed, proceed seen by CONTROL          D:partial R:partial V:bug
CONTROL sees `CX::Return`, `CX::Take`, `CX::Emit`, `CX::Succeed` and
`CX::Proceed` (messages `<return control exception>` etc.). The bug:
`.rethrow` of CX::Return, CX::Take or CX::Emit makes the routine return,
the gather collect, or the supply emit the CX object itself in place of
the original value (5 becomes `CX::Return.new`, `take 1; take 2` gives
two `CX::Take.new`, `emit 7` arrives as the CX::Emit's message). Do not
imitate: the value must survive a rethrow. `.resume` of CX::Take or
CX::Emit skips the take or emit (nothing is gathered or emitted);
`.resume` of CX::Return throws `X::AdHoc` (not resumable); `.rethrow`
of CX::Succeed and CX::Proceed keeps their effect, and `.resume` of
CX::Succeed leaves the `given` block.
```
say do { my @r; sub f { CONTROL { when CX::Return { @r.push("ret:" ~ .message); .rethrow } }; return 5 }; @r.push(f().raku); my @g = gather { CONTROL { when CX::Take { @r.push("take:" ~ .message); .rethrow } }; take 1; take 2 }; @r.push(@g.raku); my @g2 = gather { CONTROL { when CX::Take { @r.push("tres"); .resume } }; take 1; take 2 }; @r.push(@g2.raku); @r.push((try { sub f2 { CONTROL { when CX::Return { .resume } }; return 5; 7 }; f2() }) // $!.message); given 5 { CONTROL { when CX::Succeed { @r.push("succ"); .rethrow } }; when 5 { @r.push("w5") } }; given 5 { CONTROL { when CX::Proceed { @r.push("proc"); .rethrow } }; when 5 { @r.push("p5"); proceed }; default { @r.push("dflt") } }; my @o; given 5 { CONTROL { when CX::Succeed { .resume } }; when 5 { @o.push("in") }; @o.push("after") }; @r.push(@o.raku); my @e; my $s = supply { CONTROL { when CX::Emit { @e.push("e"); .resume } }; emit 7; done }; react { whenever $s { @e.push("got$_") } }; @r.push(@e.raku); my @e2; my $s2 = supply { CONTROL { when CX::Emit { @e2.push("e"); .rethrow } }; emit 7; done }; react { whenever $s2 { @e2.push("got$_") } }; @r.push(@e2.raku); @r.join(" | ") }
# rakudo 2026.08: ret:<return control exception> | CX::Return.new | take:<take control exception> | take:<take control exception> | [CX::Take.new, CX::Take.new] | tres | tres | [] | This exception is not resumable | w5 | p5 | dflt | ["in"] | ["e"] | ["e", "got<emit control exception>"]
```
rakupp 4.0.1-84: differs — CONTROL sees none of these five; the constructs simply work (5, `[1, 2]`, `got7`).

### EX-18  Control without its construct: X::ControlFlow                  D:yes R:yes V:spec
`last`, `next`, `redo` outside a loop throw `X::ControlFlow` with
`.illegal` the keyword and `.enclosing` `loop construct`; `take` gives
`take`/`gather`; `emit` and `done` give `supply or react`; `succeed`
and `proceed` give `when clause`. `return` outside a routine — also
inside EVAL, and from a `gather` block inside a sub — throws
`X::ControlFlow::Return` (`.illegal` `return`, `.enclosing` `Routine`,
`.out-of-dynamic-scope` False). These are ordinary exceptions (`~~
Exception`, not `X::Control`) with a `Backtrace`. A `last` or `next`
inside a sub called from a loop body acts on that loop, also when the
call sits in a parameter default.
```
say do { sub t(&c) { try { c() }; $!.^name ~ ":" ~ $!.illegal ~ ":" ~ $!.enclosing }; my @r = t({ last }), t({ next }), t({ redo }), t({ take 1 }), t({ emit 1 }), t({ done }), t({ succeed }), t({ proceed }), do { try { return 1 }; $!.^name ~ ":" ~ $!.illegal ~ ":" ~ $!.enclosing ~ ":" ~ $!.out-of-dynamic-scope }, do { try { EVAL "return 1" }; $!.^name }, do { sub g($x = last) { $x }; my $i = 0; for 1..3 { $i++; g() }; "runloop:$i" }, do { my @o; sub lst { last }; for 1..3 { @o.push($_); lst() }; @o.raku }, do { try { last }; $!.backtrace.^name ~ ":" ~ ($! ~~ Exception) ~ ":" ~ ($! ~~ X::Control) }, do { my @o; sub nxt { next }; for 1..3 { @o.push($_); nxt(); @o.push("no") }; @o.raku }, do { try { sub gath { gather { return 1 } }; ~gath() }; $!.^name }; @r.join(" | ") }
# rakudo 2026.08: X::ControlFlow:last:loop construct | X::ControlFlow:next:loop construct | X::ControlFlow:redo:loop construct | X::ControlFlow:take:gather | X::ControlFlow:emit:supply or react | X::ControlFlow:done:supply or react | X::ControlFlow:succeed:when clause | X::ControlFlow:proceed:when clause | X::ControlFlow::Return:return:Routine:False | X::ControlFlow::Return | runloop:1 | [1] | Backtrace:True:False | [1, 2, 3] | X::ControlFlow::Return
```
rakupp 4.0.1-84: differs — a `last` outside a loop is fatal ("last without loop construct" ends the program) rather than a catchable X::ControlFlow; the line died at the first field.

### EX-19  Custom X::Control, and CX objects thrown by hand               D:partial R:yes V:spec
A class that `does X::Control`, thrown with `.throw`, reaches CONTROL
(`default` matches) and never CATCH; `.resume` continues after the
throw, and without it the block exits. The CX classes are Exceptions
and X::Controls with fixed messages (`<next control exception>`,
`<take control exception>`, `<done control exception>`);
`CX::Warn.new(:message)` keeps its message. A control exception that no
CONTROL handles becomes an `X::ControlFlow` with `.illegal` `control
exception` and `.enclosing` `handler`, catchable by an outer try or
CATCH. Throwing `CX::Last.new` by hand is NOT a `last`: without a
CONTROL it is that X::ControlFlow; with a CONTROL in the enclosing block
it is caught there and the block exits. `CX::Warn.new(:message).throw`
reaches a `when CX::Warn` handler and resumes like a `warn`.
```
say do { my @r; class CX::My does X::Control { has $.msg; method message { $.msg } }; { CATCH { default { @r.push("catch:" ~ .^name) } }; CONTROL { default { @r.push("control:" ~ .^name ~ ":" ~ .message); .resume } }; CX::My.new(:msg<hi>).throw; @r.push("resumed") }; @r.push((CX::My.new ~~ Exception) ~ ":" ~ (CX::My ~~ X::Control) ~ ":" ~ (CX::Warn.new(:message<m>).message) ~ ":" ~ CX::Next.new.message ~ ":" ~ CX::Take.new.message ~ ":" ~ CX::Done.new.message ~ ":" ~ (CX::Last ~~ X::Control) ~ ":" ~ (CX::Warn ~~ Exception)); try { { CX::My.new(:msg<u>).throw; @r.push("no") }; CATCH { default { @r.push("outer-catch:" ~ .^name ~ ":" ~ .illegal ~ ":" ~ .enclosing) } } }; { CONTROL { when CX::My { @r.push("ctl-no-resume") } }; CX::My.new(:msg<z>).throw; @r.push("after-z") }; try { for 1..3 { @r.push($_); CX::Last.new.throw } }; @r.push($!.^name ~ ":" ~ $!.illegal ~ ":" ~ $!.enclosing); { CONTROL { when CX::Last { @r.push("cl:" ~ .message) } }; for 1..2 { @r.push("i$_"); CX::Last.new.throw; @r.push("no3") } }; { CONTROL { when CX::Warn { @r.push("cw:" ~ .message); .resume } }; CX::Warn.new(:message<cw>).throw; @r.push("warn-resumed") }; @r.join(" | ") }
# rakudo 2026.08: control:CX::My:hi | resumed | True:True:m:<next control exception>:<take control exception>:<done control exception>:True:True | outer-catch:X::ControlFlow:control exception:handler | ctl-no-resume | 1 | X::ControlFlow:control exception:handler | i1 | cl:<last control exception> | cw:cw | warn-resumed
```
rakupp 4.0.1-84: differs — an unhandled custom control exception reaches the outer CATCH as itself (no X::ControlFlow, so `.illegal` died), and a `when CX::My` CONTROL does not catch it (the exception ends the program).

### EX-20  given/when: the value, succeed and proceed                    D:partial R:partial V:bug
A `do given` yields the value of the `when` block that matched, or
`succeed`'s argument (a list stays a List); the first matching block
wins, a `default` placed first wins too; `proceed` goes on to the
following `when`s; when no `when` matches the value is the block's last
statement (a failed bare `when` yields False, `"tail"` yields the
string); an empty matching `when` yields Nil. The bug: `succeed` with
no argument leaves an engine-internal null in the result — any method
call on it, even `.raku`, throws `X::Method::NotFound` with `.typename`
`VMNull`. Do not imitate; Nil is the sane value.
```
say do { my $y = do given 5 { when Int { succeed }; "tail" }; my @r = (try $y.raku) // $!.^name ~ ":" ~ $!.typename; @r.push((do given 5 { when 6 { } }).raku, (do given 5 { when Int { } }).raku, (do given 5 { when Int { succeed "s"; "no" } }).raku, (do given 5 { when Int { succeed 1, 2 } }).raku, (do given 5 { when Int { "int" }; when 5 { "five" } }).raku, (do given 5 { default { "d" }; when Int { "int" } }).raku, (do given 5 { when Int { proceed }; when 5 { succeed "five" }; default { "dflt" } }).raku, (do given 5 { when Int { proceed }; "fell" }).raku, (do given 5 { when 6 { "six" }; "tail" }).raku, (do given 5 { when 5 { }; "tail2" }).raku); @r.join(" | ") }
# rakudo 2026.08: X::Method::NotFound:VMNull | Bool::False | Nil | "s" | (1, 2) | "int" | "d" | "five" | "fell" | "tail" | Nil
```
rakupp 4.0.1-84: differs in two fields — a bare `succeed` yields Any (the sane value; keep it), and `succeed 1, 2` yields an Array `[1, 2]` where Rakudo keeps a List.

## D. Failure

### EX-21  fail and Failure.new: every form                              D:yes R:yes V:spec
`fail` with a Str, an Int, a list (`"a", "b", 3` joins to `ab3`), an
Array (`1 2`) wraps an X::AdHoc; with an Exception instance keeps it
(`X::NYI` keeps `.feature`); with nothing gives `Failed` unless the
routine's `$!` is set (then that exception); with a type object
(Exception, X::NYI, Failure) gives X::AdHoc `Failed with undefined
<name>`. `Failure.new` with nothing, a Str, an Exception or several
values behaves the same (`"a", "b"` joins to `ab`). `X::NYI.new.fail`
is the method form. `fail` of a handled Failure re-arms it: the same
object comes back with `.handled` False. Every Failure starts unhandled
and `.so` marks it.
```
say do { sub F(&c) { my $f = c(); my $h = $f.handled; $f.so; $f.^name ~ ":" ~ $f.exception.^name ~ ":" ~ ((try $f.exception.message.raku) // "?") ~ ":" ~ $h ~ ":" ~ $f.handled }; (F({ (sub { fail "s" })() }), F({ (sub { fail 42 })() }), F({ (sub { fail "a", "b", 3 })() }), F({ (sub { fail X::NYI.new(:feature<f>) })() }), F({ (sub { fail })() }), F({ (sub { fail Exception })() }), F({ (sub { fail X::NYI })() }), F({ (sub { fail Failure })() }), F({ (sub { try { die "prior" }; fail })() }), F({ Failure.new }), F({ Failure.new("p") }), F({ Failure.new(X::NYI.new(:feature<n>)) }), F({ Failure.new("a", "b") }), F({ (sub { fail [1, 2] })() }), do { my $f = (sub { fail "re" })(); $f.so; my $g = (sub { fail $f })(); my $h = $g.handled; $g.so; "rearm:" ~ ($g === $f) ~ ":" ~ $h }, F({ (sub { X::NYI.new(:feature<m>).fail })() })).join(" | ") }
# rakudo 2026.08: Failure:X::AdHoc:"s":False:True | Failure:X::AdHoc:"42":False:True | Failure:X::AdHoc:"ab3":False:True | Failure:X::NYI:"f not yet implemented. Sorry.":False:True | Failure:X::AdHoc:"Failed":False:True | Failure:X::AdHoc:"Failed with undefined Exception":False:True | Failure:X::AdHoc:"Failed with undefined X::NYI":False:True | Failure:X::AdHoc:"Failed with undefined Failure":False:True | Failure:X::AdHoc:"prior":False:True | Failure:X::AdHoc:"Failed":False:True | Failure:X::AdHoc:"p":False:True | Failure:X::NYI:"n not yet implemented. Sorry.":False:True | Failure:X::AdHoc:"ab":False:True | Failure:X::AdHoc:"1 2":False:True | rearm:True:False | Failure:X::NYI:"m not yet implemented. Sorry.":False:True
```
rakupp 4.0.1-84: differs — the method form `.fail` throws the exception, so the line died at the last field; with it removed: a list keeps its first element only, a type object gives an empty message, `Failure.new` with a Str or several values has no `.exception` (Any), and re-arming makes a new Failure (`===` False).

### EX-22  Which `$!` a bare fail / Failure.new picks up                  D:partial R:partial V:quirk
`fail` with no argument takes `$!` from any enclosing scope of the
call: in a sub after its try (`hq`), in a `do` block inside such a sub
(`hp`), in a try block after an inner try (`bp`). `Failure.new` with no
argument takes `$!` only when the calling block itself is a routine
body or the mainline (`d`, `e`, `s`); from a `do` block — in the
mainline or in a sub — it is `Failed` although `$!` is set outside, and
an anonymous sub's own `$!` is fresh (`Failed`). `fail` inside a nested
block inside a sub returns from the SUB. The inconsistency between the
two is the quirk.
```
say do { my @r; try { try { die "bp" }; fail }; @r.push("try:" ~ $!.message); sub h { try { die "hp" }; my $r = do { fail }; "after" }; my $v = h(); $v.so; @r.push("do-in-sub:" ~ $v.exception.message); sub h2 { try { die "hq" }; fail }; my $v2 = h2(); $v2.so; @r.push("sub:" ~ $v2.exception.message); sub h3 { my $x = do { fail "inner-do" }; "after3" }; my $v3 = h3(); $v3.so; @r.push("nested:" ~ $v3.^name ~ ":" ~ $v3.exception.message); my $f1 = do { try { die "b" }; Failure.new }; $f1.so; @r.push("new-do:" ~ $f1.exception.message); try { die "d" }; my $f2 = Failure.new; $f2.so; @r.push("new-main:" ~ $f2.exception.message); sub s { try { die "e" }; Failure.new }; my $f3 = s(); $f3.so; @r.push("new-sub:" ~ $f3.exception.message); my $f4 = (sub { Failure.new })(); $f4.so; @r.push("new-anon:" ~ $f4.exception.message); sub s2 { try { die "g" }; do { Failure.new } }; my $f5 = s2(); $f5.so; @r.push("new-do-in-sub:" ~ $f5.exception.message); sub s3 { try { die "k" }; my $x = (sub { fail })(); $x.so; $x.exception.message }; @r.push("fail-anon:" ~ s3()); @r.join(" | ") }
# rakudo 2026.08: try:bp | do-in-sub:hp | sub:hq | nested:Failure:inner-do | new-do:Failed | new-main:Failed | new-sub:e | new-anon:Failed | new-do-in-sub:Failed | fail-anon:Failed
try { die "d" }; my $f = Failure.new; $f.so; say "new-main:", $f.exception.message, " ", do { try { die "n" }; my $g = Failure.new; $g.so; "in-do:" ~ $g.exception.message }, " ", do { my $h = (sub { try { die "s" }; Failure.new })(); $h.so; "in-sub:" ~ $h.exception.message }
# rakudo 2026.08: new-main:d in-do:Failed in-sub:s
```
rakupp 4.0.1-84: differs — `fail` in a try block outside a routine is `X::ControlFlow::Return`, and `Failure.new` always finds `$!` (`b`, `d`, `g`, `k`, `n`) where Rakudo answers `Failed`.

### EX-23  Which contexts mark a Failure handled                          D:partial R:yes V:spec
`.Bool`, `?`, `so`, `!`, `.not`, `.defined`, `defined()`, `//`, `||`,
`&&`, `?? !!`, `if`, `with`/`else`, `without`, `orelse`, `andthen`
(which yields Empty) and `notandthen` all treat the Failure as False or
undefined and mark it handled; `&&` returns the Failure itself, now
handled. `.^name`, `.WHAT`, `.exception`, `.handled`, `~~ Failure`, `~~
Nil`, `.isa(Nil)`, `.DEFINITE` (True), `eqv`, `===`, `.raku` (which
starts `Failure.new(`), `.backtrace` and `.exception.message` leave it
unhandled.
```
say do { sub mk { (sub { fail "v" })() }; sub H(&c) { my $f = mk; my $r = try { c($f) }; $r = "THREW:" ~ $!.^name if $!; my $h = $f.handled; $f.so; $r.raku ~ ":" ~ $h }; (H({ $^f.Bool }), H({ ?$^f }), H({ so $^f }), H({ !$^f }), H({ $^f.not }), H({ $^f.defined }), H({ defined $^f }), H({ $^f // "dflt" }), H({ $^f || "or" }), H({ $^f && "and" }), H({ $^f ?? "t" !! "f" }), H({ if $^f { "t" } else { "e" } }), H({ with $^f { "w" } else { "wo" } }), H({ without $^f { "wo" } }), H({ $^f orelse "oe" }), H({ $^f andthen "at" }), H({ $^f notandthen "nat" }), H({ $^f.^name }), H({ $^f.WHAT.^name }), H({ $^f.exception.^name }), H({ $^f.handled }), H({ $^f ~~ Failure }), H({ $^f ~~ Nil }), H({ $^f.isa(Nil) }), H({ $^f.DEFINITE }), H({ $^f eqv $^f }), H({ $^f === $^f }), H({ $^f.raku.substr(0, 11) }), H({ $^f.backtrace.^name }), H({ $^f.exception.message })).join(" | ") }
# rakudo 2026.08: Bool::False:True | Bool::False:True | Bool::False:True | Bool::True:True | Bool::True:True | Bool::False:True | Bool::False:True | "dflt":True | "or":True | &CORE::infix:<orelse>(Failure.new(exception => X::AdHoc.new(payload => "v"), backtrace => Backtrace.new), *.self):True | "f":True | "e":True | "wo":True | "wo":True | "oe":True | Empty:True | "nat":True | "Failure":False | "Failure":False | "X::AdHoc":False | Bool::False:False | Bool::True:False | Bool::True:False | Bool::True:False | Bool::True:False | Bool::True:False | Bool::True:False | "Failure.new":False | "Backtrace":False | "v":False
```
rakupp 4.0.1-84: differs — only `.Bool`, `.not` and `.defined` mark handled; `?`, `so`, `!`, `//`, `||`, `?? !!`, `if`, `with`, `without`, `orelse`, `notandthen` do not; `&&` and `andthen` throw; `.DEFINITE` is False and `.isa(Nil)` False; `.raku` is a hash-like `${:exception…`; `.backtrace` throws.

### EX-24  Using an unhandled Failure as a value throws                  D:yes R:partial V:spec
`.Str`, `~`, `+`, `+ 1`, `.gist`, `.Int`, `.Numeric`, `.elems`,
`.list`, an unknown method, `.self`, `.Capture`, `.Set`, `.iterator`,
`.map` and calling it as a function all throw the wrapped exception.
`[0]` and `<k>` return the Failure itself; assigning it to an array
gives one element and `for` over it runs once, without throwing;
`.mess` and `.exception.message` answer. Once handled: `.Str` and
`.gist` are `(HANDLED) v` plus the backtrace, `.Int` is the Int type
object, `.Numeric` and `.Num` are NaN, `.self` is the Failure,
`.Capture` throws `X::Cannot::Capture`, `.Set` is a one-element set,
`[0]` is still the Failure, and `.elems` and `.list` still throw.
```
say do { sub mk { (sub { fail "v" })() }; sub T(&c) { my $f = mk; my $r = try { c($f) }; my $out = $! ?? "THREW:" ~ $!.^name !! $r.raku; $f.so; $out }; (T({ $^f.Str }), T({ ~$^f }), T({ +$^f }), T({ $^f + 1 }), T({ $^f.gist }), T({ $^f.Int }), T({ $^f.Numeric }), T({ $^f.elems }), T({ $^f.list }), T({ $^f.nosuchmethod }), T({ $^f[0].^name }), T({ $^f<k>.^name }), T({ $^f.self }), T({ $^f.Capture }), T({ my @a = $^f; @a.elems }), T({ (for $^f { $_ }).elems }), T({ $^f.Set }), T({ $^f.iterator }), T({ $^f.map(*.^name) }), T({ $^f.mess.lines[0] }), T({ $^f.exception.message }), T({ $^f() }), T({ $^f.Bool; $^f.Str.lines[0] }), T({ $^f.so; $^f.gist.lines[0] }), T({ $^f.so; $^f.Int.raku }), T({ $^f.so; $^f.Numeric.raku }), T({ $^f.so; $^f.Num.raku }), T({ $^f.so; $^f.self.^name }), T({ $^f.so; $^f.Capture }), T({ $^f.so; $^f.Set.elems }), T({ $^f.so; $^f.elems }), T({ $^f.so; $^f[0].^name }), T({ $^f.so; $^f.list })).join(" | ") }
# rakudo 2026.08: THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | "Failure" | "Failure" | THREW:X::AdHoc | THREW:X::AdHoc | 1 | 1 | THREW:X::AdHoc | THREW:X::AdHoc | THREW:X::AdHoc | "v" | "v" | THREW:X::AdHoc | "(HANDLED) v" | "(HANDLED) v" | "Int" | "NaN" | "NaN" | "Failure" | THREW:X::Cannot::Capture | 1 | THREW:X::AdHoc | "Failure" | THREW:X::AdHoc
```
rakupp 4.0.1-84: differs — `+$f` is 2, `$^f` inside a `for` block is refused (`X::Placeholder::Block`, a parser difference), `.mess` and calling the Failure throw other types, and after handling `.Str`/`.gist` lack the `(HANDLED) ` prefix while `.Int`, `.Numeric`, `.Num`, `.Capture` and `.Set` throw.

### EX-25  Sink context, `use fatal`, `no fatal`, try                     D:yes R:yes V:spec
Inside a `try`, a Failure returned by a call throws whether the call is
sunk, assigned, bound, assigned to an array, or `sink`ed (a try is a
fatal scope); `use fatal` does the same, and `//` under it still takes
the default without throwing; `no fatal` inside a try turns it off.
`try { fail "tf" }` is Nil with `$!` set. `fail` outside any routine
throws (like `die`). Assigning a Failure to a typed variable that cannot
hold it throws its exception. Outside a fatal scope an assigned or
`do`-produced Failure stays a Failure, and only a call in sink context
throws (a CATCH sees `v`).
```
say do { my @r; sub mk { (sub { fail "v" })() }; { try { mk(); @r.push("after-sink") }; @r.push("sink:" ~ $!.^name ~ ":" ~ $!.message) }; { try { my $x = mk(); @r.push("assigned"); $x.so }; @r.push("assign:" ~ $!.raku) }; { try { sink mk(); }; @r.push("sinkprefix:" ~ $!.^name) }; { try { use fatal; my $x = mk(); @r.push("fatal-after") }; @r.push("fatal:" ~ $!.^name) }; { try { use fatal; my $x = mk() // 0; @r.push("fatal-dor:$x") }; @r.push("fd:" ~ $!.raku) }; { try { no fatal; my $x = mk(); $x.so; @r.push("nofatal-ok") }; @r.push("nf:" ~ $!.raku) }; { my $v = try { fail "tf" }; @r.push("try-fail:" ~ $v.raku ~ ":" ~ $!.^name) }; { try { my $d = fail "outside"; @r.push("reached") }; @r.push("outside:" ~ $!.^name ~ ":" ~ $!.message) }; { try { sub s { fail "typed" }; my Int $x = s(); @r.push("typed-ok") }; @r.push("typed:" ~ $!.^name) }; { try { my $x := mk(); @r.push("bound"); $x.so }; @r.push("bind:" ~ $!.raku) }; { try { my @a = mk(); @r.push("arr:" ~ @a.elems) }; @r.push("arr:" ~ $!.^name) }; { my $x = mk(); @r.push("plain-assign:" ~ $x.^name); $x.so }; { mk(); @r.push("plain-sink"); CATCH { default { @r.push("plain-sink-threw:" ~ .message) } } }; { my $z = do { mk() }; @r.push("do:" ~ $z.^name); $z.so }; @r.join(" | ") }
# rakudo 2026.08: sink:X::AdHoc:v | assign:X::AdHoc.new(payload => "v") | sinkprefix:X::AdHoc | fatal:X::AdHoc | fatal-dor:0 | fd:Any | nofatal-ok | nf:Any | try-fail:Any:X::AdHoc | outside:X::AdHoc:outside | typed:X::AdHoc | bind:X::AdHoc.new(payload => "v") | arr:X::AdHoc | plain-assign:Failure | plain-sink-threw:v | do:Failure
```
rakupp 4.0.1-84: differs — inside a try an assigned, bound or array-assigned Failure does not throw, `use fatal` and the `sink` prefix are ignored, and `try { fail }` / `fail` in a try block are `X::ControlFlow::Return`; the sunk-call, typed-variable and outside-try cases match.

### EX-26  Failure: backtrace, mess, raku round trip, new, handled       D:partial R:yes V:spec
`.raku` of an unhandled Failure starts `Failure.new(`; `.new` on a
Failure instance throws its exception and marks the Failure handled;
`with`/`else` takes the else branch with the Failure handled;
`.handled` is assignable both ways; `.exception` does not mark. Once
handled, `.mess` and `.gist` start `(HANDLED) bt`, `.raku` is
`&CORE::infix:<orelse>(…, *.self)` and EVALs back to a Failure whose
`.handled` is True. The Failure owns the backtrace: `.backtrace` lists
the frames from the creation point (`s2,s1,…`) while
`.exception.backtrace` is Nil.
```
say do { my @r; sub s2 { fail "bt" }; sub s1 { s2() }; { my $u = (sub { fail "un" })(); my $s = $u.raku.substr(0, 12); $u.so; @r.push("un:$s") }; { my $b = Failure.new("nb"); try { $b.new; @r.push("nn") }; @r.push("new:" ~ $!.^name ~ ":" ~ $!.payload ~ ":" ~ $b.handled) }; { my $f = (sub { fail "wh" })(); with $f { @r.push("with") } else { @r.push("else:" ~ .^name ~ ":" ~ .handled) } }; { my $f = (sub { fail "ck" })(); $f.so; $f.handled = False; my $y = $f.handled; $f.handled = True; @r.push("rw:$y:" ~ $f.handled) }; { my $f = (sub { fail "gc" })(); @r.push("ex-marks:" ~ do { my $x = $f.exception; $f.handled }); $f.so }; { my $f = s1(); $f.so; @r.push("mess:" ~ $f.mess.lines[0]); @r.push("gist:" ~ $f.gist.lines[0]); @r.push("rk:" ~ $f.raku.substr(0, 20)); @r.push("rt:" ~ $f.raku.EVAL.handled); @r.push("exbt:" ~ $f.exception.backtrace.raku); @r.push("bt:" ~ $f.backtrace.list.map(*.subname).join(",")) }; @r.join(" | ") }
# rakudo 2026.08: un:Failure.new( | new:X::AdHoc:nb:True | else:Failure:True | rw:False:True | ex-marks:False | mess:(HANDLED) bt | gist:(HANDLED) bt | rk:&CORE::infix:<orelse | rt:True | exbt:Nil | bt:s2,s1,,,<unit>
```
rakupp 4.0.1-84: differs — `Failure.new("nb").new` is `X::Method::NotFound` (the line died at `$!.payload`), and with that made safe the last block dies with the Failure's own exception at `.mess`/`.backtrace`.

## E. Backtrace

### EX-27  Frames and the views of a thrown exception's backtrace        D:partial R:partial V:quirk
For a `die` in `inner`, called by `outer`, called in a `do` block at
the unit, the backtrace has 7 frames, each a `Backtrace::Frame` with
`.subname`, `.file`, `.line`, `.is-routine`, `.is-hidden`, `.is-setting`,
`.subtype` (`method`, `sub`, `block`), `.code` (the Code object,
`.package`, `.name`): `throw` (setting, method), `die` (setting, sub),
`inner`, `outer`, two anonymous blocks (empty subname), `<unit>`.
`.Str` (= `.nice`) prints the non-setting frames, one line each, with
adjacent anonymous blocks collapsed to one; `.full` all 7; `.summary`
the frames that are routines or outside the setting (all 7 here);
`.concise` the routines outside the setting; `.first-none-setting-line`
inner's line. `.next-interesting-index` with no argument, with 0 and
with `:named` is 2, then 3, 4 and 6 for 2, 3, 5. `.flat eqv .list`,
`.map` and `.first` walk the frames, `[*-1]` is `<unit>`, an index past
the end is Nil. The quirk: `.nice(:oneline)` prints the SECOND
interesting frame (`outer`), not the first.
```
say do { sub inner { die "bt" }; sub outer { inner() }; try { outer() }; my $bt = $!.backtrace; my @f = $bt.list; sub F($f) { $f.subname ~ "/" ~ ($f.is-setting ?? "SETTING" !! $f.file ~ "/" ~ $f.line) ~ "/" ~ $f.is-routine ~ "/" ~ $f.is-hidden ~ "/" ~ $f.subtype ~ "/" ~ $f.code.^name }; ($bt.^name, $bt.gist, @f.elems, @f.map(&F).join(","), $bt.Str.raku, $bt.full.lines.elems, $bt.summary.lines.elems, $bt.concise.raku, $bt.first-none-setting-line.raku, $bt.next-interesting-index, $bt.next-interesting-index(0), $bt.next-interesting-index(:named), $bt.next-interesting-index(2), $bt.next-interesting-index(3), $bt.next-interesting-index(5), $bt.nice(:oneline).raku, ($bt.flat eqv $bt.list), $bt.map(*.subname).raku, $bt.first(*.is-routine).subname, $bt[0].^name, $bt[*-1].subname, $bt[2].Str.raku, $bt[0].package.^name, $bt[2].package.^name, $bt[2].code.name, $bt[99].raku, $bt.list.elems, $bt.Str eq $bt.nice, $bt.summary.lines.grep(*.contains("-e")).elems).join(" | ") }
# rakudo 2026.08: Backtrace | Backtrace(7 frames) | 7 | throw/SETTING/True/False/method/Method,die/SETTING/True/False/sub/Sub+{Callable[Nil]},inner/-e/1/True/False/sub/Sub,outer/-e/1/True/False/sub/Sub,/-e/1/False/False/block/Block,/-e/1/False/False/block/Block,<unit>/-e/1/False/False/block/Block | "  in sub inner at -e line 1\n  in sub outer at -e line 1\n  in block  at -e line 1\n  in block <unit> at -e line 1\n" | 7 | 7 | "  in sub inner at -e line 1\n  in sub outer at -e line 1\n" | "  in sub inner at -e line 1\n" | 2 | 2 | 2 | 3 | 4 | 6 | "  in sub outer at -e line 1\n" | True | ("throw", "die", "inner", "outer", "", "", "<unit>").Seq | throw | Backtrace::Frame | <unit> | "  in sub inner at -e line 1\n" | Exception | GLOBAL | inner | Nil | 7 | True | 5
```
rakupp 4.0.1-84: differs — `Backtrace::Frame` has no `.subtype`, so the line died at the first frame; with it removed, `.backtrace` is a List without `.first-none-setting-line`.

### EX-28  Backtrace.new outside an exception; one backtrace per throw   D:yes R:no V:spec
`Backtrace.new` inside `inner` lists `new` (in the setting), `inner`,
`outer`, the anonymous block and `<unit>`; its `.Str` shows `inner`,
`outer`, `<unit>`; `.summary` has 5 lines and `.concise` 2;
`Backtrace.new(1)` drops the first frame; `Backtrace.new(0)` and
`Backtrace.new` start at the setting's `new`. An unthrown exception's
`.backtrace` is Nil. Throwing the same exception again records a NEW
Backtrace (`===` False), and `.throw($bt)` keeps the one given.
```
say do { sub inner { Backtrace.new }; sub outer { inner() }; my $bt = outer(); ($bt.^name, $bt.list.map(*.subname).join(","), $bt.Str.raku, $bt.summary.lines.elems, $bt.concise.lines.elems, $bt.gist, do { sub off { Backtrace.new(1) }; off().list.map(*.subname).join(",") }, Backtrace.new.list.head.subname, Backtrace.new.list.head.is-setting, Backtrace.new.list.head.file.starts-with("SETTING::"), X::AdHoc.new.backtrace.raku, do { try die "t"; $!.backtrace.^name }, do { my $e = X::AdHoc.new(:payload<v>); try $e.throw; my $b1 = $e.backtrace; try $e.throw; ($e.backtrace === $b1) ~ ":" ~ $e.backtrace.gist }, do { my $e = X::AdHoc.new(:payload<w>); try $e.throw; my $b = $e.backtrace; try $e.throw($b); ($e.backtrace === $b) }, Backtrace.new(0).list.head.subname, do { try die "x"; $!.backtrace.Str.lines.elems }).join(" | ") }
# rakudo 2026.08: Backtrace | new,inner,outer,,<unit> | "  in sub inner at -e line 1\n  in sub outer at -e line 1\n  in block <unit> at -e line 1\n" | 5 | 2 | Backtrace(5 frames) | off,,,<unit> | new | True | True | Nil | Backtrace | False:Backtrace(5 frames) | True | new | 2
```
rakupp 4.0.1-84: differs — `Backtrace.new` is a List of hashes holding absolute file paths, `.Str` keeps the anonymous block, the offset form has no frame to drop, an unthrown exception's `.backtrace` is not Nil, and a rethrow keeps the same object.

### EX-29  `is hidden-from-backtrace`                                     D:partial R:no V:spec
A routine with the trait stays in `.list` with `.is-hidden` True but is
omitted from `.Str`, `.concise`, `.summary` and the exception's gist;
`next-interesting-index` skips it unless `:reveal`; the trait gives the
routine an `is-hidden-from-backtrace` method answering True, which a
plain routine lacks (`X::Method::NotFound`). It works for methods
(subtype `method`) as for subs.
```
say do { sub hid() is hidden-from-backtrace { die "h" }; sub vis() { hid() }; try { vis() }; my $bt = $!.backtrace; my $g = $!.gist.lines.map(*.trim).join(";"); ($bt.list.map({ .subname ~ ":" ~ .is-hidden }).join(","), $bt.Str.lines.map(*.trim).join(";"), $bt.concise.lines.map(*.trim).join(";"), $bt.summary.lines.map({ .contains("SETTING::") ?? "setting" !! .trim }).join(";"), $bt.next-interesting-index(-1), $bt.next-interesting-index(-1, :reveal), &hid.is-hidden-from-backtrace, (try &vis.is-hidden-from-backtrace) // $!.^name, $g, do { class C { method m() is hidden-from-backtrace { die "cm" }; method n() { self.m } }; try { C.new.n }; $!.backtrace.list.map({ .subname ~ ":" ~ .is-hidden ~ ":" ~ .subtype }).join(",") ~ " " ~ $!.backtrace.Str.lines.map(*.trim).join(";") }).join(" | ") }
# rakudo 2026.08: throw:False,die:False,hid:True,vis:False,:False,:False,<unit>:False | in sub vis at -e line 1;in block  at -e line 1;in block <unit> at -e line 1 | in sub vis at -e line 1 | setting;setting;in sub vis at -e line 1;in block  at -e line 1;in block  at -e line 1;in block <unit> at -e line 1 | 3 | 2 | True | X::Method::NotFound | h;in sub vis at -e line 1;in block  at -e line 1;in block <unit> at -e line 1 | throw:False:method,die:False:sub,m:True:method,n:False:method,:False:block,:False:block,:False:block,<unit>:False:block in method n at -e line 1;in block  at -e line 1;in block <unit> at -e line 1
```
rakupp 4.0.1-84: differs — the trait adds no `is-hidden-from-backtrace` method (the line died there), and `Backtrace::Frame` has no `.subtype`.

## F. Boundaries and phasers

### EX-30  Exceptions from start/await and from lazy map                  D:partial R:partial V:spec
`await` of a broken promise throws the cause's type with the
`X::Await::Died` role mixed in (`X::AdHoc+{X::Await::Died}`, still `~~
X::AdHoc`, with `.await-backtrace` a Backtrace); `.status` is Broken,
`.cause` is the original object, `.result` throws with
`X::Promise::Broken` mixed in; a typed exception keeps its attributes
(`.feature`). A `start` block that `fail`s breaks the promise with the
Failure's exception. A `die` inside a `.map` block fires when the Seq is
reified (here `.eager`), not when `.map` is called; eager array
assignment fires it at once; Failures produced per element stay
Failures in the array. A `die` in a sub called inside a try leaves the
sub's frame in the backtrace.
```
say do { my @r; { my $p = start { die "in-start" }; try { await $p }; @r.push("await:" ~ $!.^name ~ ":" ~ $!.message ~ ":" ~ ($! ~~ X::AdHoc) ~ ":" ~ $!.await-backtrace.^name); @r.push("status:" ~ $p.status ~ ":" ~ $p.cause.^name ~ ":" ~ $p.cause.message); try { $p.result }; @r.push("result:" ~ $!.^name ~ ":" ~ $!.message) }; { my $p = start { X::NYI.new(:feature<async>).throw }; try { await $p }; @r.push("typed:" ~ $!.^name ~ ":" ~ $!.feature ~ ":" ~ ($! ~~ X::NYI)) }; { my $p = start { fail "sf" }; try { await $p }; @r.push("startfail:" ~ $p.status ~ ":" ~ $!.^name ~ ":" ~ $!.message) }; { my $s = (1, 2, 3).map({ die "map$_" if $_ == 2; $_ }); @r.push("lazy-made"); try { my @a = $s.eager; @r.push("reified") }; @r.push("map:" ~ $!.message) }; { try { my @a = (1, 2).map({ die "eager" }); @r.push("no") }; @r.push("eager:" ~ $!.message) }; { my @c = (1, 2, 3).map({ +"x$_" }); @r.push("fails:" ~ @c.elems ~ ":" ~ @c[0].^name); @c».so }; { sub thrower { die "ts" }; try { thrower(); @r.push("no") }; @r.push("sub:" ~ $!.message ~ ":" ~ $!.backtrace.list.map(*.subname).grep("thrower").elems) }; @r.join(" | ") }
# rakudo 2026.08: await:X::AdHoc+{X::Await::Died}:in-start:True:Backtrace | status:Broken:X::AdHoc:in-start | result:X::AdHoc+{X::Promise::Broken}:in-start | typed:X::NYI+{X::Await::Died}:async:True | startfail:Broken:X::AdHoc+{X::Await::Died}:sf | lazy-made | map:map2 | eager:eager | fails:3:Failure | sub:ts:1
```
rakupp 4.0.1-84: differs — `await` throws the bare cause (no `X::Await::Died`, so `.await-backtrace` died); measured separately, a `start` that `fail`s is Kept with the Failure as result, and `.map` is eager (the die fires at the map call, outside the try).

### EX-31  LEAVE, KEEP, UNDO, ENTER around exceptions                     D:yes R:yes V:bug
On a `die` the LEAVE queue runs in reverse declaration order, UNDO
included (`UL`); on success KEEP and LEAVE (`KL`); `fail` and `return
Nil` count as failure (UNDO), `return 0` as success (KEEP). Inside a
LEAVE run during unwinding `$!` is the exception. A `die` in an ENTER
aborts the ENTER queue, still runs LEAVE, and is the exception caught;
a `die` in a LEAVE does not abort the queue (`b` then `a`) and is
rethrown afterwards; a LEAVE that dies while an exception is already
unwinding REPLACES it (`lx`, not `orig`); two dying LEAVEs give
`X::PhaserExceptions` with `.exceptions`. CATCH runs before LEAVE
(`CL`). The bug: in a loop LEAVE runs before NEXT (`lnlnL`; Roast
expects NEXT first, `nlnl`, and marks Rakudo todo) — do not imitate.
```
say do { my @r; { my $s = ""; try { LEAVE { $s ~= "L" }; KEEP { $s ~= "K" }; UNDO { $s ~= "U" }; die "d" }; @r.push("die:$s") }; { my $s = ""; { LEAVE { $s ~= "L" }; KEEP { $s ~= "K" }; UNDO { $s ~= "U" }; 1 }; @r.push("ok:$s") }; { my $s = ""; sub f { LEAVE { $s ~= "L" }; KEEP { $s ~= "K" }; UNDO { $s ~= "U" }; fail "ff" }; my $v = f(); $v.so; @r.push("fail:$s") }; { my $s = ""; sub g { KEEP { $s ~= "K" }; UNDO { $s ~= "U" }; return Nil }; g(); @r.push("nil:$s") }; { my $s = ""; sub g2 { KEEP { $s ~= "K" }; UNDO { $s ~= "U" }; return 0 }; g2(); @r.push("zero:$s") }; { my $s = ""; try { LEAVE { $s ~= ($! // "undef") }; die "lv" }; @r.push("leave-bang:$s") }; { my $s = ""; try { ENTER { $s ~= "1"; die "e" }; ENTER { $s ~= "2" }; LEAVE { $s ~= "L" } }; @r.push("enter:$s:" ~ $!.message) }; { my $s = ""; try { LEAVE { $s ~= "a"; die "la" }; LEAVE { $s ~= "b" }; 1 }; @r.push("leave-die:$s:" ~ $!.^name ~ ":" ~ $!.message) }; { try { LEAVE { die "lx" }; die "orig" }; @r.push("both:" ~ $!.message) }; { my $s = ""; try { LEAVE { $s ~= "L" }; CATCH { default { $s ~= "C" } }; die "x" }; @r.push("order:$s") }; { my $s = ""; for 1..2 { NEXT { $s ~= "n" }; LEAVE { $s ~= "l" }; LAST { $s ~= "L" } }; @r.push("loop:$s") }; { try { LEAVE { die "x1" }; LEAVE { die "x2" }; 1 }; @r.push("multi:" ~ $!.^name ~ ":" ~ $!.exceptions.map(*.message).sort.join(",")) }; @r.join(" | ") }
# rakudo 2026.08: die:UL | ok:KL | fail:UL | nil:U | zero:K | leave-bang:lv | enter:1L:e | leave-die:ba:X::AdHoc:la | both:lx | order:CL | loop:lnlnL | multi:X::PhaserExceptions:x1,x2
```
rakupp 4.0.1-84: differs — `X::PhaserExceptions` does not exist (the line died at `.exceptions`); with that made safe: `fail` and `return Nil` run KEEP instead of UNDO, `$!` is not visible in a LEAVE during unwinding, and the other nine fields match, including the LEAVE-before-NEXT order.

### EX-32  PRE and POST                                                   D:yes R:yes V:spec
A false PRE or POST throws `X::Phaser::PrePost` with `.phaser` `PRE` or
`POST` and `.condition` the phaser's source text (`{ $i < 5 }` in block
form, `$x >= 0` blockless); POST sees the return value in `$_`. A
failing PRE runs nothing else (no ENTER, LEAVE or POST); POSTs run after
LEAVE, in reverse order, and the first false one stops the rest; a POST
run during an exception sees `$!` as that exception. A PRE/POST
exception is not caught by a CATCH in the same block. Roast asserts the
`Precondition '…' failed` message.
```
say do { my @r; sub pre(Int $i) { PRE { $i < 5 }; 4 }; sub post(Int $i) { return 4; POST { $i < 5 } }; @r.push(pre(2)); try { pre(10) }; @r.push($!.^name ~ ":" ~ $!.phaser ~ ":" ~ $!.condition.trim.raku ~ ":" ~ $!.message); try { post(10) }; @r.push($!.phaser ~ ":" ~ $!.message); sub bl($x) { PRE $x >= 0; POST $_ == 4; $x }; @r.push(bl(4)); try { bl(-1) }; @r.push($!.phaser ~ ":" ~ $!.condition.raku); try { bl(5) }; @r.push($!.phaser ~ ":" ~ $!.condition.raku); { my $s = ""; try { { PRE { $s ~= "("; 0 }; ENTER { $s ~= "[" }; $s ~= "x"; LEAVE { $s ~= "]" }; POST { $s ~= ")"; 1 } } }; @r.push("order:$s") }; { my $s = ""; try { { POST { $s ~= "z"; 1 }; POST { $s ~= "x"; 0 }; LEAVE { $s ~= "y" } } }; @r.push("post:$s") }; { my $s = ""; try { POST { $s ~= ($! // "u"); 1 }; die "pe" }; @r.push("postbang:$s") }; { try { { PRE { 0 }; CATCH { default { @r.push("caught-inside") } } } }; @r.push("pre-catch:" ~ $!.^name) }; { my $s = ""; my $pv = do { POST { $s ~= $_ ~ ";"; 1 }; 42 }; @r.push("posttopic:$s$pv") }; @r.join(" | ") }
# rakudo 2026.08: 4 | X::Phaser::PrePost:PRE:"\{ \$i < 5 }":Precondition '{ $i < 5 }' failed | POST:Postcondition '{ $i < 5 }' failed | 4 | PRE:"\$x >= 0" | POST:"\$_ == 4" | order:( | post:yx | postbang:pe | pre-catch:X::Phaser::PrePost | posttopic:42;42
```
rakupp 4.0.1-84: differs — PRE and POST never throw; the phasers run as plain blocks (`[(x)]`, `zxy`).

## G. The typed exceptions: which situation, which attributes

### EX-33  X::Comp via EVAL                                                D:yes R:partial V:spec
A compile-time error inside EVAL is caught by `try` as an `X::Comp`
with `.is-compile-time` True, `.line` (3 for a symbol on line 3),
`.filename` (the EVAL pseudo-file, `EVAL_N`) and `.pos` (an Int; Any
for the mixin cases): `1 +` is `X::Comp::AdHoc`, an unknown variable
`X::Undeclared`, `for 1, 2` `X::Syntax::Missing`, `1 <=> 2 <=> 3`
`X::Syntax::NonAssociative`, `unless … else` `X::Syntax::UnlessElse`,
`my $0` `X::Syntax::Variable::Numeric`, two CATCHes
`X::Phaser::Multiple`, a dying BEGIN `X::Comp::BeginTime`. A runtime
`die` or method-not-found inside EVAL is not X::Comp and has no
`.line`, `.filename` or `.pos`. A call with a literal of the wrong type
and a missing private method are compile-time: `X::TypeCheck::Argument`
and `X::Method::NotFound` with `X::Comp` mixed in.
```
use MONKEY-SEE-NO-EVAL; say do { sub E($c) { try { EVAL $c }; $!.^name ~ ":" ~ $!.is-compile-time ~ ":" ~ ($! ~~ X::Comp) ~ ":" ~ ($!.?line).raku ~ ":" ~ (($!.?filename) andthen .IO.basename.subst(/\d+$/, "N") orelse "Nil") ~ ":" ~ ($!.?pos).^name }; (E('1 +'), E('$k'), E('for 1, 2'), E('1 <=> 2 <=> 3'), E('unless 1 { } else { }'), E('my $0'), E('CATCH { }; CATCH { }'), E('BEGIN { die "bt" }'), E("\n\n\$zz"), E('die "rt"'), E('1.nosuch'), E('sub foo(Str) { }; foo 42'), E('class Priv { method x { self!foo } }; Priv.x')).join(" | ") }
# rakudo 2026.08: X::Comp::AdHoc:True:True:1:EVAL_N:Int | X::Undeclared:1:True:1:EVAL_N:Int | X::Syntax::Missing:1:True:1:EVAL_N:Int | X::Syntax::NonAssociative:1:True:1:EVAL_N:Int | X::Syntax::UnlessElse:1:True:1:EVAL_N:Int | X::Syntax::Variable::Numeric:1:True:1:EVAL_N:Int | X::Phaser::Multiple:1:True:1:EVAL_N:Int | X::Comp::BeginTime:True:True:1:EVAL_N:Any | X::Undeclared:1:True:3:EVAL_N:Int | X::AdHoc:False:False:Nil:Nil:Nil | X::Method::NotFound:False:False:Nil:Nil:Nil | X::TypeCheck::Argument+{X::Comp}:True:True:1:EVAL_N:Any | X::Method::NotFound+{X::Comp}:True:True:1:EVAL_N:Any
```
rakupp 4.0.1-84: differs — no `.is-compile-time`, `.line`, `.filename` or `.pos` on any of them (the line died at the first), `for 1, 2` is `X::Syntax::Confused`, a dying BEGIN is a plain X::AdHoc, the wrong-literal call is a runtime `X::TypeCheck::Binding::Parameter` and the private-method case lacks the X::Comp mixin.

### EX-34  Attributes of the compile-time types                            D:partial R:yes V:spec
`X::Undeclared` `.what` (`Variable`), `.symbol`, `.suggestions`
(`["$foo"]` for `$fooo`); `X::Syntax::NonAssociative` `.left`/`.right`;
`X::Comp::BeginTime` `.exception` (the X::AdHoc that died) and
`.use-case`; `X::Syntax::Malformed` and `X::Syntax::Missing` `.what`;
`X::Comp::FailGoal` `.line`, `.dba`, `.goal`; `X::Package::Stubbed`
`.packages`; `X::Comp::Group` `.sorrows` (a `X::Syntax::BlockGobbled`),
`.panic` (`X::Syntax::Missing`), `.worries`; `X::Phaser::Multiple`
`.block`; `X::Comp::AdHoc` is X::Comp but not X::Syntax and has a
Backtrace; the gist heading starts `===SORRY!=== Error while compiling`
and names the EVAL file; `X::TypeCheck::Argument` `.arguments`
(`["Int"]`), `.objname`, `.signature` (`("(Str)",)`), `.protoguilt`;
the compile-time `X::Method::NotFound` `.method`, `.private` True,
`.typename`. A runtime `die` inside EVAL has frames `throw, die,
<unit>, EVAL, …` in the files setting, `EVAL_N` and `-e`.
```
use MONKEY-SEE-NO-EVAL; say do { (do { try { EVAL '$zz' }; $!.what ~ ":" ~ $!.symbol ~ ":" ~ $!.suggestions.raku }, do { try { EVAL 'my $foo = 1; say $fooo' }; $!.^name ~ ":" ~ $!.suggestions.raku }, do { try { EVAL '1 <=> 2 <=> 3' }; $!.left ~ ":" ~ $!.right }, do { try { EVAL 'BEGIN { die "bt" }' }; $!.exception.^name ~ ":" ~ $!.exception.message ~ ":" ~ $!.use-case }, do { try { EVAL 'my Int a' }; $!.^name ~ ":" ~ $!.what }, do { try { EVAL 'for 1, 2' }; $!.what }, do { try { EVAL "\n\nfoo(" }; $!.^name ~ ":" ~ $!.line ~ ":" ~ $!.dba ~ ":" ~ $!.goal }, do { try { EVAL 'class A {...}' }; $!.^name ~ ":" ~ $!.packages.raku }, do { try { EVAL 'for 1,2,3, { say 3 }' }; $!.^name ~ ":" ~ $!.sorrows.map(*.^name).join(",") ~ ":" ~ $!.panic.^name ~ ":" ~ $!.worries.elems }, do { try { EVAL '1 +' }; ($! ~~ X::Comp) ~ ":" ~ ($! ~~ X::Syntax) ~ ":" ~ ($! ~~ Exception) ~ ":" ~ $!.backtrace.^name }, do { try { EVAL 'CATCH { }; CATCH { }' }; $!.block }, do { try { EVAL '1 +' }; $!.gist.lines[0].subst(/\e .*? m/, "", :g).words.head(4).join(" ") ~ ":" ~ $!.filename.contains("EVAL") }, do { try { EVAL 'sub foo(Str) { }; foo 42' }; $!.^name ~ ":" ~ $!.arguments.raku ~ ":" ~ $!.objname ~ ":" ~ $!.signature.raku ~ ":" ~ $!.protoguilt.raku }, do { try { EVAL 'class Priv { method x { self!foo } }; Priv.x' }; $!.^name ~ ":" ~ $!.method ~ ":" ~ $!.private ~ ":" ~ $!.typename }, do { try { EVAL 'die "rt"' }; $!.^name ~ ":" ~ $!.backtrace.list.map(*.subname).join(",") ~ ":" ~ $!.backtrace.list.map({ .is-setting ?? "SETTING" !! .file.subst(/\d+$/, "N") }).unique.join(",") }).join(" | ") }
# rakudo 2026.08: Variable:$zz:[] | X::Undeclared:["\$foo"] | <=>:<=> | X::AdHoc:bt:evaluating a BEGIN | X::Syntax::Malformed:my (did you mean to declare a sigilless \a or $a?) | block | X::Comp::FailGoal:3:argument list:')' | X::Package::Stubbed:["A"] | X::Comp::Group:X::Syntax::BlockGobbled:X::Syntax::Missing:0 | True:False:True:Backtrace | CATCH | ===SORRY!=== Error while compiling:True | X::TypeCheck::Argument+{X::Comp}:["Int"]:foo:("(Str)",):Bool::False | X::Method::NotFound+{X::Comp}:foo:True:Priv | X::AdHoc:throw,die,<unit>,EVAL,,,,<unit>:SETTING,EVAL_N,-e
```
rakupp 4.0.1-84: differs — `X::Undeclared` has no `.what`/`.suggestions` (the line died there); with the calls made safe: no `X::Comp::BeginTime`, `X::Comp::FailGoal`, `X::Comp::Group` or `X::TypeCheck::Argument` (`X::Syntax::Confused` stands in), `X::Package::Stubbed` and `X::Phaser::Multiple` carry their attributes, and the private-method `X::Method::NotFound` matches.

### EX-35  Runtime dispatch and type-check errors                         D:partial R:yes V:spec
`X::Multi::NoMatch` `.dispatcher` (the proto; `.name`) and `.capture`
(a Capture of the arguments); `X::Method::NotFound` `.method`,
`.typename`, `.invocant`, `.private` (False), `.suggestions`
(`["cosech"]` for `nosuch` on an Int, `[]` for `lenght`), `.tips`;
`X::TypeCheck::Assignment` `.got`, `.expected`, `.symbol`, `.operation`
`assignment` (for a typed array element `.symbol` `@a` and `.desc.name`
`@a`); `X::TypeCheck::Binding` `.got`, `.expected`, `.symbol` (Any),
`.operation` `binding`; `X::TypeCheck::Binding::Parameter` adds
`.parameter` (a Parameter, `.name` `$x`) and `.constraint` (undefined
for a type mismatch, True for a failed `where`); `X::TypeCheck::Return`
`.got`, `.expected`, `.operation` `returning`; `X::Parameter::RW`
`.got`, `.symbol`; `X::Parameter::InvalidConcreteness` `.expected`
`Exception`, `.got` `X::NYI`, `.routine` `throw`, `.param` `<anon>`,
`.should-be-concrete`, `.param-is-invocant`. A runtime arity mismatch
(too few or too many positionals) is a plain `X::AdHoc`.
```
say do { my @r; my $s = "s"; { proto p(|) {*}; multi p(Int $x) { }; try { p($s) }; @r.push($!.^name ~ ":" ~ $!.dispatcher.name ~ ":" ~ $!.capture.^name ~ ":" ~ $!.capture.list.elems ~ ":" ~ $!.capture[0].raku) }; { try { 1.nosuch }; @r.push($!.^name ~ ":" ~ $!.method ~ ":" ~ $!.typename ~ ":" ~ $!.invocant.raku ~ ":" ~ $!.private ~ ":" ~ $!.suggestions.raku ~ ":" ~ $!.tips.elems) }; { try { "s".lenght }; @r.push($!.method ~ ":" ~ $!.suggestions.raku ~ ":" ~ $!.invocant.raku) }; { try { my Int $x = "foo" }; @r.push($!.^name ~ ":" ~ $!.got.raku ~ ":" ~ $!.expected.^name ~ ":" ~ $!.symbol ~ ":" ~ $!.operation) }; { try { my Int @a; @a[0] = "s" }; @r.push($!.^name ~ ":" ~ $!.symbol.raku ~ ":" ~ $!.expected.^name ~ ":" ~ $!.desc.name) }; { try { my Str $x := 3 }; @r.push($!.^name ~ ":" ~ $!.got.raku ~ ":" ~ $!.expected.^name ~ ":" ~ $!.symbol.raku ~ ":" ~ $!.operation) }; { sub f(Int $x) { }; try { f($s) }; @r.push($!.^name ~ ":" ~ $!.got.raku ~ ":" ~ $!.expected.^name ~ ":" ~ $!.symbol ~ ":" ~ $!.parameter.^name ~ ":" ~ $!.parameter.name ~ ":" ~ $!.constraint.raku ~ ":" ~ $!.operation) }; { sub g($x where * > 5) { }; try { g(1) }; @r.push($!.^name ~ ":" ~ $!.constraint ~ ":" ~ $!.symbol) }; { sub h(--> Str) { 5 }; try { h() }; @r.push($!.^name ~ ":" ~ $!.got.raku ~ ":" ~ $!.expected.^name ~ ":" ~ $!.operation) }; { sub w($x is rw) { }; my $c = 5; try { w($c + 0) }; @r.push($!.^name ~ ":" ~ $!.got ~ ":" ~ $!.symbol) }; { try { X::NYI.throw }; @r.push($!.^name ~ ":" ~ $!.expected ~ ":" ~ $!.got ~ ":" ~ $!.routine ~ ":" ~ $!.param ~ ":" ~ $!.should-be-concrete ~ ":" ~ $!.param-is-invocant) }; { sub two($a, $b) { }; my @a = 1; try { two(|@a) }; @r.push($!.^name ~ ":" ~ $!.message); try { two(|(1, 2, 3)) }; @r.push($!.message) }; @r.join(" | ") }
# rakudo 2026.08: X::Multi::NoMatch:p:Capture:1:"s" | X::Method::NotFound:nosuch:Int:1:False:["cosech"]:0 | lenght:[]:"s" | X::TypeCheck::Assignment:"foo":Int:$x:assignment | X::TypeCheck::Assignment:"\@a":Int:@a | X::TypeCheck::Binding:3:Str:Any:binding | X::TypeCheck::Binding::Parameter:"s":Int:$x:Parameter:$x:Bool:binding | X::TypeCheck::Binding::Parameter:True:$x | X::TypeCheck::Return:5:Str:returning | X::Parameter::RW:5:$x | X::Parameter::InvalidConcreteness:Exception:X::NYI:throw:<anon>:True:True | X::AdHoc:Too few positionals passed; expected 2 arguments but got 1 | Too many positionals passed; expected 2 arguments but got 3
```
rakupp 4.0.1-84: differs — `X::Multi::NoMatch` has no `.dispatcher`/`.capture` (the line died there); with the calls made safe: `X::Method::NotFound` lacks `.invocant`, `.suggestions`, `.tips`; the TypeCheck family lacks `.operation`, `.desc`, `.symbol` (Binding), `.parameter`, `.constraint`; `X::Parameter::RW` lacks `.got`/`.symbol`; `X::NYI.throw` on the type object is `X::Method::NotFound`; an arity mismatch is an `X::Signature::ArityMismatch`, a type Rakudo does not have.

### EX-36  Value and container errors                                     D:partial R:yes V:spec
`1.0 = 3` throws `X::Assignment::RO` with `.value` 1.0 and `.typename`
`Rat`; `.push` on a List `X::Immutable` `.method`/`.typename`;
`Date.new("2012-02-30")` `X::Temporal::OutOfRange` (an `X::OutOfRange`)
`.what` `Day`, `.got` 30, `.range` `1..29`, `.comment` Any; `"foo"[2]`
a Failure whose exception is `X::OutOfRange` `.what` `Index`, `.got` 2,
`.range` `0..0`; `+"5 foo"` `X::Str::Numeric` `.source`, `.pos` 1,
`.reason`; `(1+2i).Num` `X::Numeric::Real` `.target` Num, `.source`,
`.reason`; `X::NYI` `.feature`; `(1..*).elems` `X::Cannot::Lazy`
`.action` `.elems`, `.what` `""`; `pop` on an empty array
`X::Cannot::Empty` `.action` `pop`, `.what` `Array`; `...` `X::StubCode`
(message `Stub code executed`, Roast-asserted) and `!!! 42` message
`42`; `1 div 0` `X::Numeric::DivideByZero` `.using` `div`, `.numerator`
1; `Mu.new(1)` `X::Constructor::Positional` `.type`; `my %h = 1`
`X::Hash::Store::OddNumber` `.found` 1, `.last` 1; `X::AdHoc.new.throw`
message `Unexplained error`; `my Int:D $y = Int`
`X::TypeCheck::Assignment` expected `Int:D`, got `Int`; an unknown
method on Nil answers Nil, no exception; the assignment exception's
`.symbol` is `$i` and `.desc` a ContainerDescriptor.
```
say do { my @r; { try { 1.0 = 3; @r.push("assigned") }; @r.push("ro:" ~ $!.^name ~ ":" ~ $!.value.raku ~ ":" ~ $!.typename) }; { try { my $l := (1, 2); $l.push(3) }; @r.push($!.^name ~ ":" ~ $!.method ~ ":" ~ $!.typename) }; { try { Date.new("2012-02-30") }; @r.push($!.^name ~ ":" ~ $!.what ~ ":" ~ $!.got ~ ":" ~ $!.range.raku ~ ":" ~ $!.comment.raku) }; { try { "foo"[2].self }; @r.push($!.^name ~ ":" ~ $!.what ~ ":" ~ $!.got ~ ":" ~ $!.range.raku) }; { try { +"5 foo" }; @r.push($!.^name ~ ":" ~ $!.source ~ ":" ~ $!.pos ~ ":" ~ $!.reason.substr(0, 8)) }; { try { (1+2i).Num }; @r.push($!.^name ~ ":" ~ $!.target.^name ~ ":" ~ $!.source.raku ~ ":" ~ $!.reason.raku) }; { try { X::NYI.new(:feature<fx>).throw }; @r.push($!.^name ~ ":" ~ $!.feature ~ ":" ~ $!.message) }; { try { (1..*).elems.self }; @r.push($!.^name ~ ":" ~ $!.action ~ ":" ~ $!.what.raku) }; { try { my @a; @a.pop.self }; @r.push($!.^name ~ ":" ~ $!.action ~ ":" ~ $!.what) }; { try { ... }; @r.push($!.^name ~ ":" ~ $!.message) }; { try { !!! 42 }; @r.push($!.^name ~ ":" ~ $!.message) }; { try { 1 div 0 }; @r.push($!.^name ~ ":" ~ $!.using.raku ~ ":" ~ $!.numerator.raku) }; { try { Mu.new(1) }; @r.push($!.^name ~ ":" ~ $!.type.^name) }; { try { my %h = 1 }; @r.push($!.^name ~ ":" ~ $!.found ~ ":" ~ $!.last.raku) }; { try { X::AdHoc.new.throw }; @r.push($!.message) }; { try { my Int:D $y = Int }; @r.push($!.^name ~ ":" ~ $!.expected.^name ~ ":" ~ $!.got.raku) }; @r.push((Nil.nosuch).raku); { try { my Int $i = "s" }; @r.push($!.^name ~ ":" ~ $!.symbol ~ ":" ~ $!.desc.^name) }; @r.join(" | ") }
# rakudo 2026.08: ro:X::Assignment::RO:1.0:Rat | X::Immutable:push:List | X::Temporal::OutOfRange:Day:30:"1..29":Any | X::OutOfRange:Index:2:"0..0" | X::Str::Numeric:5 foo:1:trailing | X::Numeric::Real:Num:<1+2i>:"imaginary part not zero" | X::NYI:fx:fx not yet implemented. Sorry. | X::Cannot::Lazy:.elems:"" | X::Cannot::Empty:pop:Array | X::StubCode:Stub code executed | X::StubCode:42 | X::Numeric::DivideByZero:"div":1 | X::Constructor::Positional:Mu | X::Hash::Store::OddNumber:1:1 | Unexplained error | X::TypeCheck::Assignment:Int:D:Int | Nil | X::TypeCheck::Assignment:$i:ContainerDescriptor
```
rakupp 4.0.1-84: differs — `X::Assignment::RO` has no `.value`/`.typename` (the line died there); with the calls made safe: `X::Immutable`, `X::Cannot::Empty`, `X::StubCode`, `X::Hash::Store::OddNumber`, the `Int:D`-less symbol and `Nil.nosuch` match; `X::OutOfRange`, `X::Str::Numeric`, `X::Numeric::Real` and `X::Numeric::DivideByZero` lack their attributes; `(1..*).elems`, `Mu.new(1)` and `my Int:D $y = Int` throw nothing; `!!! 42` loses its message; `X::AdHoc.new.throw` has an empty message.

## Counts

| | items |
|---|---|
| total | 36 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 15 |
| Rakudo bugs (do not imitate) | 3 — EX-17 a rethrown CX::Return/Take/Emit replaces the value with the CX object, EX-20 a bare `succeed` yields an engine null, EX-31 LEAVE runs before NEXT |
| quirks (recorded, step two decides) | 5 — EX-07 a trailing CATCH makes a try Nil, EX-13 Nil or Any after a handled CATCH, EX-16 `.resume` refused for next/last/redo, EX-22 `fail` and `Failure.new` search `$!` differently, EX-27 `nice(:oneline)` skips the first frame |
| rakupp 4.0.1-84 differs | 35 |
| rakupp 4.0.1-84 matches | 1 — EX-12 |

Recurring rakupp gaps, for step two. The `$!` lifecycle is the
foundation: a successful try must reset `$!` to Any, bare and `do`
blocks must share the routine's `$!`, a sub must have its own, and
inside a CATCH `$!` must stay the enclosing one while `$/` is fresh
(EX-07, 08, 09, 10). A `try` is a fatal scope: Failures returned into
it — assigned, bound, sunk or `sink`ed — must throw, `use fatal` must
be honoured, and `fail` in a try block must not be a return (EX-07, 22,
25). `.resume` must continue after the throwing statement (EX-02, 11,
13). CONTROL must see every CX class (EX-16, 17, 19) and an escaped
control must become a catchable `X::ControlFlow` with `.illegal` and
`.enclosing` (EX-18, 19). The Failure value protocol needs the full
list of disarming contexts, `.DEFINITE` True, `.raku` as
`Failure.new(...)`, the `(HANDLED) ` prefix and a real `.backtrace`
(EX-23, 24, 26). `Backtrace` must be an object of `Backtrace::Frame`s
with `.subtype`, `.is-setting`, the four views and `Backtrace.new`
offsets, and `is hidden-from-backtrace` must exist (EX-27, 28, 29).
The X:: surface is mostly names without attributes: `X::AdHoc.payload`,
`X::Comp`'s `.line`/`.filename`/`.pos`/`.is-compile-time`,
`X::Multi::NoMatch`, `X::Method::NotFound`, the TypeCheck family,
`X::Assignment::RO`, `X::OutOfRange`, `X::Str::Numeric`,
`X::Numeric::Real`, `X::Phaser::PrePost`, `X::PhaserExceptions`,
`X::Comp::BeginTime`, `X::Comp::Group`, `X::Comp::FailGoal`,
`X::TypeCheck::Argument`, `X::Await::Died` (EX-04, 06, 30, 31, 32, 33,
34, 35, 36). PRE/POST do not assert (EX-32). One rakupp answer is
better than Rakudo's and should be kept: Any for a bare `succeed`
(EX-20).

## Method (how this sheet was produced)

As for [Supply.md](Supply.md), with three rounds of exploratory probes
before the final set. Traps met, for later sheets: a `try` whose block
has a CATCH lets anything the CATCH does not handle escape, so a probe
that wants to inspect such an exception needs an outer `try` around
the inner one (four first-round lines died of this); inside a
double-quoted EVAL string `{ ... }` is a closure interpolation, so
`EVAL "BEGIN { die 'x' }"` dies at -e run time, not at EVAL compile
time — EVAL code must be single-quoted; `EVAL $var` needs
`MONKEY-SEE-NO-EVAL`; a call with a literal argument of the wrong type
or arity is rejected at compile time (`X::TypeCheck::Argument`), so
runtime dispatch errors need a variable argument or `|@a`; `my @a = $s`
with a Seq in a scalar itemizes it (one element, no reification) —
reify with `.eager`; `Nil.nosuch` is Nil, not an exception, and leaves
`$!` alone; an expression statement before a CATCH is in sink context
and a constant there warns at compile time (`42.self` does not); a
Failure created inside a try is thrown at once, so `Failure.new` cannot
be observed there; `$!` is reset to Any by any successful try in the
same routine, including one inside a nested block, so save `$!` in a
variable before the next try; the Frame `.raku` prints the code
object's address; the EVAL pseudo-file name is an absolute path under
the sandbox — print its basename; `.full` and `.summary` print setting
paths, map them to `SETTING`; stdout and stderr interleave
unpredictably in one pipe, so `note`/`warn` output was read from a
child process via `run(:out, :err)`; rakupp prints nothing at all when a
line dies, so its column was completed with rakupp-only variants in
which the missing calls were made safe with `.?`, described in prose.
D flags from `doc/Type/Exception.rakudoc`, `Failure.rakudoc`,
`Backtrace.rakudoc`, `Backtrace/Frame.rakudoc`, `X/AdHoc.rakudoc`,
`X/Control.rakudoc`, `X/ControlFlow.rakudoc`, `X/ControlFlow/Return.rakudoc`,
`CX/Warn.rakudoc`, `X/Phaser/PrePost.rakudoc`, `X/Comp.rakudoc`,
`Language/exceptions.rakudoc`, `Language/phasers.rakudoc`,
`Language/control.rakudoc` (succeed/proceed) and the existence of the
per-type `X/*.rakudoc` pages; R flags from `S04-exceptions/*.t`,
`S32-exceptions/misc.t`, `misc2.t`, `S04-statements/try.t`,
`S32-basics/warn.t`, `S04-exception-handlers/catch.t`, `control.t`,
`S04-phasers/pre-post.t`, `keep-undo.t`, `enter-leave.t`, `next.t`,
`S04-statements/given.t` and `S17-promise/nonblocking-await.t`.
