# Instant, Duration, Date, DateTime, Dateish — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/Instant.rakumod` (144
lines), `Duration.rakumod` (78), `Dateish.rakumod` (306), `Date.rakumod`
(477), `DateTime.rakumod` (755), read in full on 2026-09-23; the
leap-second tables and the POSIX/TAI conversions of
`Rakudo/Internals.rakumod` (lines 915–1145) read for what `now`,
`from-posix` and `to-posix` guarantee; the `X::Temporal` classes in
`Exception.rakumod` and the fallback `cmp` candidate in `Order.rakumod`
read for the exception attributes and for what a `cmp` on a Dateish falls
back to. Out of scope: `sleep`, `sleep-timer` and `sleep-until` (they live
in `Date.rakumod` but are issue #41's territory), the `$*TZ` DST
computation beyond the offset it yields, `localtime`/`gmtime` of
`time.t`, and any strftime-like formatting. Oracle: Homebrew Rakudo
v2026.08 on macOS. Compared against Raku++ 4.0.1-84-ga4291988
(build-arm64, 2026-09-23). Format and legend: [README.md](README.md).

Where this sits against the declared spec: Rakudo passes 8 of the 9
`S32-temporal` files on this machine (all but `time.t`); Raku++ passes 5
(`baum-gregorian-data.t`, `calendar.t`, `greg-jd-frac-seconds.t`,
`juliandate.t`, `local.t`) and fails `Date.t`, `DateTime.t` and
`DateTime-Instant-Duration.t`. The rules below are what those three files
and real programs lean on: what an Instant is a count of and how leap
seconds enter and leave it, which constructor forms and strings are
accepted and which exception type with which attributes refuses the rest,
how a Date or DateTime moves, truncates, clones and compares, and what the
default `Str` looks like at the edges. 4 of the 33 items are neither
fully documented nor fully asserted by Roast. Four Rakudo behaviours are
recorded as bugs and thirteen as quirks; step two should not imitate the
bugs.

Every probe ran with `alarm 10` and standard input closed. The machine's
local zone was CEST (`$*TZ` = 7200); every field that depends on the clock
or on `$*TZ` is printed as a derived Boolean or a type name, and the item
says so. Where a probe line also exercises neighbouring items, the
statement says which fields matter.

## A. Instant, `now`, `time`

### DT-01  `time`, `now`, `$*INIT-INSTANT`, and the TAI offset             D:partial R:yes V:spec
`time` is an Int of whole POSIX seconds; `now` is an Instant, a count of
atomic (TAI-like) seconds with nanosecond resolution; `now - now` is a
Duration; `$*INIT-INSTANT` is an Instant not after `now`. `Instant.new`
throws `X::Cannot::New`. An Instant is Cool and Real, not Dateish. The
difference between `now.Int` and `time`, and exactly `now.Int -
DateTime.new(now).posix`, is 37: the 10 seconds TAI was ahead of UTC at
the POSIX epoch plus the 27 leap seconds of 1972–2016 (see DT-02).
`now.to-posix[1]` is False outside a leap second. `now.DateTime` is in
UTC and its `.second` is a Rat; `now.tai` is a Rat. Every field here is
time-dependent and printed as a Boolean or a type.
```
say time.^name, " ", now.^name, " ", (now - now).^name, " ", (now > Instant.from-posix(0)), " ", (time > 1_600_000_000), " ", ((now.Int - time).abs <= 38), " ", ((now.Int - time).abs >= 36), " ", (now.Int - DateTime.new(now).posix), " ", $*INIT-INSTANT.^name, " ", ($*INIT-INSTANT <= now), " ", (try Instant.new(5)) // $!.^name, " ", now.to-posix[1], " ", ((time - now.to-posix[0]).abs < 2), " ", now.Str.starts-with("Instant:"), " ", now.gist.starts-with("Instant:"), " ", now.raku.starts-with("Instant.from-posix("), " ", (now ~~ Real), " ", (now ~~ Cool), " ", (now ~~ Numeric), " ", (now ~~ Dateish), " ", (now ~~ Instant), " ", now.defined, " ", (now.DateTime.timezone), " ", (now.DateTime.second.^name), " ", (time.Int == time), " ", ((now - $*INIT-INSTANT).^name), " ", now.tai.^name
# rakudo 2026.08: Int Instant Duration True True True True 37 Instant True X::Cannot::New False True True True True True True True False True True 0 Rat True Duration Rat
```
rakupp 4.0.1-84: differs — the offset is 10, not 37 (no leap-second table, so `now.Int - time` fails the `>= 36` test and the posix difference prints 10); `Instant.new` is `X::Method::NotFound`; `.Str`/`.gist`/`.raku` are bare numbers rather than `Instant:…`/`Instant.from-posix(…)`; `now.DateTime.second` is a Num; `.tai` does not exist, so the line died at its last field.

### DT-02  from-posix, to-posix, the leap-second table, Str and raku      D:yes R:yes V:spec
`Instant.from-posix($posix, $prefer-leap-second = False)` builds an
Instant whose `.tai` (a Rat, nanosecond precision: 1/3 becomes
0.333333333) is `$posix + 10 + n`, n being the number of leap seconds
whose POSIX label is below `$posix`; at a POSIX value that labels a leap
second the default takes the later of the two seconds (the following
00:00:00, n counts that leap second) and `:prefer-leap-second` takes the
leap second itself (one less). The 27 leap seconds known to 2026.08 end
the UTC days 1972-06-30, 1972-12-31, 1973-12-31, 1974-12-31,
1975-12-31, 1976-12-31, 1977-12-31, 1978-12-31, 1979-12-31, 1981-06-30,
1982-06-30, 1983-06-30, 1985-06-30, 1987-12-31, 1989-12-31, 1990-12-31,
1992-06-30, 1993-06-30, 1994-06-30, 1995-12-31, 1997-06-30, 1998-12-31,
2005-12-31, 2008-12-31, 2012-06-30, 2015-06-30 and 2016-12-31 (POSIX
labels 78796800 … 1483228800); before 1972 the offset is 10
(`from-posix(-1).tai` is 9). `to-posix` is a two-element List: the POSIX
time as a Rat and a Bool that is True only inside a leap second. `.Str`
and `.gist` are `Instant:` followed by the tai Rat (an integral Rat
prints without a fraction); `.raku` is `Instant.from-posix(<posix>)` with
the Rat's own `.raku` (so `0.0`) plus `,True` inside a leap second.
`.to-nanos` is the Int count. A numeric Str or a Num is accepted (the tai
stays a Rat). `.DateTime` is the UTC DateTime, `.Date` the UTC date
(`from-posix(-1).Date` is 1969-12-31). Additions finer than a nanosecond
vanish.
```
say Instant.from-posix(0).Str, " ", Instant.from-posix(0).raku, " ", Instant.from-posix(0).gist, " ", Instant.from-posix(1.5).Str, " ", Instant.from-posix(1/3).raku, " ", Instant.from-posix(1483228800).to-posix.raku, " ", Instant.from-posix(1483228800, True).to-posix.raku, " ", Instant.from-posix(1483228800, True).raku, " ", Instant.from-posix(1483228800.25, True).to-posix.raku, " ", Instant.from-posix(0).DateTime.Str, " ", Instant.from-posix(1483228800, True).DateTime.Str, " ", Instant.from-posix(1483228800).DateTime.Str, " ", Instant.from-posix(0).to-posix.^name, " ", Instant.from-posix(0).to-nanos, " ", Instant.from-posix("5").raku, " ", Instant.from-posix(1e0).Str, " ", Instant.from-posix(0).tai, " ", Instant.from-posix(1.5).tai, " ", Instant.from-posix(1/3).tai.raku, " ", Instant.from-posix(1483228800).tai, " ", Instant.from-posix(1483228800, True).tai, " ", Instant.from-posix(1483228799).tai, " ", Instant.from-posix(1483228801).tai, " ", Instant.from-posix(78796800).tai, " ", Instant.from-posix(78796799).tai, " ", Instant.from-posix(78796800, True).tai, " ", Instant.from-posix(-1).tai, " ", Instant.from-posix(0, True).tai, " ", Instant.from-posix(1e0).tai.^name, " ", Instant.from-posix(1483228800.25, True).tai, " ", Instant.from-posix(2**40).tai, " ", Instant.from-posix(0).Date.Str, " ", Instant.from-posix(-1).Date.Str
# rakudo 2026.08: Instant:10 Instant.from-posix(0.0) Instant:10 Instant:11.5 Instant.from-posix(0.333333333) (1483228800.0, Bool::False) (1483228800.0, Bool::True) Instant.from-posix(1483228800.0,True) (1483228800.25, Bool::True) 1970-01-01T00:00:00Z 2016-12-31T23:59:60Z 2017-01-01T00:00:00Z List 10000000000 Instant.from-posix(5.0) Instant:11 10 11.5 10.333333333 1483228837 1483228836 1483228835 1483228838 78796811 78796809 78796810 9 10 Rat 1483228836.25 1099511627813 1970-01-01 1969-12-31
```
rakupp 4.0.1-84: differs — no leap seconds: every `.tai` is posix + 10, `:prefer-leap-second` is ignored and the `to-posix` flag is always False (the 2016-12-31T23:59:60Z round trip comes back as 2017-01-01T00:00:00Z); `.Str` is a bare number, `.raku` a Num (`10e0`), fractions are Nums; `.to-nanos` is missing, so the line died there.

### DT-03  Instant arithmetic                                             D:partial R:yes V:spec
`Instant + Real`, `Real + Instant`, `Instant + Duration`, `Duration +
Instant`, `Instant - Real` and `Instant - Duration` give an Instant;
`Instant - Instant` gives a Duration (negative when the right one is
later; zero is `Duration.new(0.0)`); `Instant + Instant` throws
`X::AdHoc`. The docs leave the rest undefined and Rakudo answers through
the Num bridge: `Instant * 2`, `Instant / 2` and `-Instant` are Nums.
`.Num` is the tai as a Num, `.Rat` the tai Rat, `.Int` the floor of the
tai (5.5 posix, tai 15.5, gives 15), `.narrow` an Int for a whole tai and
a Rat otherwise, `.Bridge` a Num, `.Instant` the invocant.
```
say (Instant.from-posix(1.5) + 2).raku, " ", (2 + Instant.from-posix(1.5)).raku, " ", (Instant.from-posix(5) - 2).raku, " ", (Instant.from-posix(5) - Instant.from-posix(2)).raku, " ", (Instant.from-posix(5) - Instant.from-posix(2)).^name, " ", (Instant.from-posix(2) - Instant.from-posix(5)).raku, " ", (Instant.from-posix(1) + Duration.new(2.5)).raku, " ", (Duration.new(2.5) + Instant.from-posix(1)).raku, " ", (Instant.from-posix(5) - Duration.new(2)).raku, " ", (Instant.from-posix(5) - Duration.new(2)).^name, " ", (try Instant.from-posix(1) + Instant.from-posix(2)) // $!.^name, " ", (Instant.from-posix(5) - 10).raku, " ", (Instant.from-posix(5) + 2).^name, " ", (Instant.from-posix(5) + Duration.new(1)).^name, " ", (Instant.from-posix(5) - Instant.from-posix(5)).raku, " ", (Instant.from-posix(5) * 2).^name, " ", (Instant.from-posix(5) * 2), " ", (Instant.from-posix(5) / 2).^name, " ", (-Instant.from-posix(5)).^name, " ", Instant.from-posix(5).Num, " ", Instant.from-posix(5).Rat.raku, " ", Instant.from-posix(5).Int, " ", Instant.from-posix(5.5).Int, " ", Instant.from-posix(5).narrow.^name, " ", Instant.from-posix(5.5).narrow.^name, " ", Instant.from-posix(5).Bridge.^name, " ", (Instant.from-posix(5) + 1/3).tai, " ", (Instant.from-posix(0) + 1e-10).tai, " ", (Instant.from-posix(5) - 2.5).tai, " ", (Instant.from-posix(5) + 1e0).tai.^name, " ", (Instant.from-posix(5) - Duration.new(1)).tai, " ", (Instant.from-posix(5) + 2**40).tai, " ", (Instant.from-posix(5) + 2).Instant.^name
# rakudo 2026.08: Instant.from-posix(3.5) Instant.from-posix(3.5) Instant.from-posix(3.0) Duration.new(3.0) Duration Duration.new(-3.0) Instant.from-posix(3.5) Instant.from-posix(3.5) Instant.from-posix(3.0) Instant X::AdHoc Instant.from-posix(-5.0) Instant Instant Duration.new(0.0) Num 30 Num Num 15 15.0 15 15 Int Rat Num 15.333333333 10 12.5 Rat 14 1099511627791 Instant
```
rakupp 4.0.1-84: differs — the results print as Nums (`13.5e0` for `Instant.from-posix(1.5) + 2`) instead of `Instant.from-posix(3.5)`, `Instant + Instant` is 23 instead of a refusal, `.narrow` answers `Instant`, and `.tai` is missing, so the line died there.

### DT-04  Comparing Instants; identity                                   D:no R:partial V:quirk
`==`, `!=`, `<`, `<=`, `>`, `>=`, `<=>` and `cmp` compare the nanosecond
counts, so `sort`, `min`, `max` work; against a plain number the tai
seconds are compared (`Instant.from-posix(1) == 11`, `< 12`, `~~ 11`,
`~~ 11.0`, `cmp 11` Same) and against a Duration likewise. `eqv` holds
for equal Instants, but `===` does NOT: `WHICH` is a plain ObjAt, so two
Instants built from the same POSIX time are not identical (the quirk).
`~ "!"` gives `Instant:11!` and `.chars` 10. `Instant.DateTime` on the
type object is the DateTime type object, `Instant.Instant` the Instant
type object, and `Instant.Date` on the type object throws
`X::Parameter::InvalidConcreteness`.
```
say do { my $a = Instant.from-posix(1); my $b = Instant.from-posix(2); ($a < $b, $a <= $a, $a == Instant.from-posix(1), $a != $b, $a > $b, $a >= $b, ($a <=> $b).raku, ($a cmp $b).raku, ($b cmp $a).raku, ($a cmp $a).raku, ($b, $a).sort.map(*.Str).raku, ($a === Instant.from-posix(1)), ($a eqv Instant.from-posix(1)), ($a eqv $b), $a.WHICH.^name, ($a == 11), ($a < 12), ($a == 1), (11 == $a), ($a cmp 11).raku, ($a ~~ Instant), ($a ~~ 11), ($a max $b).Str, ([min] $b, $a).Str, ($a ~ "!"), $a.chars, ($a === $a), ($a eqv $a), ($a ~~ 11.0), ($a <=> 11).raku, ($a, $b).max.Str, ($a cmp Duration.new(11)).raku, ($a == Duration.new(11)), ($a.Instant === $a), $a.DateTime.^name, Instant.DateTime.^name, Instant.Instant.^name, $a.Date.^name, (try Instant.Date.^name) // $!.^name).join(" ") }
# rakudo 2026.08: True True True True False False Order::Less Order::Less Order::More Order::Same ("Instant:11", "Instant:12").Seq False True False ObjAt True True False True Order::Same True True Instant:12 Instant:11 Instant:11! 10 True True True Order::Same Instant:12 Order::Same True True DateTime DateTime Instant Date X::Parameter::InvalidConcreteness
```
rakupp 4.0.1-84: differs — the comparisons all agree, but `.Str` is a bare number (so `.chars` is 2 and the sorted list prints `("11", "12")`), and `.Instant` is missing, so the line died there.

## B. Duration

### DT-05  Duration construction and printing                             D:partial R:yes V:quirk
`Duration.new` takes one Real-ish value (Int, Rat, Num, Bool, a numeric
Str, an allomorph) and stores whole nanoseconds; `.tai` is the seconds as
a Rat; `.raku` is `Duration.new(` plus the Rat's `.raku` plus `)`, so
`Duration.new(4)` and `Duration.new(4e0)` both print `Duration.new(4.0)`
and 2**70 prints in full; `.Str` and `.gist` are the Rat's Str (`4`,
`4.5`); `Duration.new` with no argument is 0.0; a non-numeric Str throws
`X::Str::Numeric`; two positionals throw `X::Constructor::Positional`;
finer than a nanosecond is dropped. The quirk: `Duration.new(Inf)` and
`Duration.new(NaN)` do not return a Duration but the Rat `<1/0>` /
`<0/0>` with a mixin (`Rat+{Duration::add-tai}`) whose `.tai` is itself,
and `~~ Duration` is False for it. `.narrow` is an Int for a whole
value, `.Int` truncates toward zero, `.Num`/`.Rat` as expected; a
Duration is Numeric, Real and Cool; only 0 is False; `WHICH` is an ObjAt
so `===` on equal Durations is False while `eqv` is True (as DT-04).
`Duration.from-posix-nanos(Int)` builds from nanoseconds; `.to-nanos`
reads them.
```
say Duration.new(4.5).raku, " ", Duration.new(4).raku, " ", Duration.new(4e0).raku, " ", Duration.new(4).Str, " ", Duration.new(4).gist, " ", Duration.new.raku, " ", Duration.new(1/3).raku, " ", (try Duration.new("meow")) // $!.^name, " ", Duration.new("2.5").raku, " ", Duration.new(Inf).^name, " ", Duration.new(Inf).raku, " ", (Duration.new(Inf) ~~ Duration), " ", Duration.new(NaN).raku, " ", Duration.new(4e0).narrow.raku, " ", Duration.new(4.5).narrow.raku, " ", Duration.new(4.5).Num.raku, " ", Duration.new(4.5).Rat.raku, " ", Duration.new(4.5).Int, " ", Duration.new(-4.5).Int, " ", (Duration.new(4.5) ~~ Numeric), " ", (Duration.new(4.5) ~~ Real), " ", (Duration.new(4.5) ~~ Cool), " ", Duration.new(4.5).Bool, " ", Duration.new(0).Bool, " ", Duration.new(4.5).WHICH.^name, " ", (Duration.new(4) === Duration.new(4)), " ", (Duration.new(4) eqv Duration.new(4)), " ", Duration.new(<5>).raku, " ", Duration.new(4.5).Str.^name, " ", Duration.new(-0.5).raku, " ", Duration.new(1e-9).raku, " ", Duration.new(0.1e0).raku, " ", Duration.new(0.1).raku, " ", (try Duration.new(1, 2)) // $!.^name, " ", Duration.new(True).raku, " ", Duration.new(1e-10).raku, " ", Duration.new(2**70).raku, " ", Duration.new(4.5).tai.raku, " ", Duration.new(4).tai.raku, " ", Duration.new.tai.raku, " ", Duration.new(1/3).tai.raku, " ", Duration.new(Inf).tai.raku, " ", Duration.new(NaN).tai.^name, " ", Duration.new(4.5).to-nanos, " ", Duration.from-posix-nanos(1500000000).raku
# rakudo 2026.08: Duration.new(4.5) Duration.new(4.0) Duration.new(4.0) 4 4 Duration.new(0.0) Duration.new(0.333333333) X::Str::Numeric Duration.new(2.5) Rat+{Duration::add-tai} <1/0> False <0/0> 4 4.5 4.5e0 4.5 4 -4 True True True True False ObjAt False True Duration.new(5.0) Str Duration.new(-0.5) Duration.new(0.000000001) Duration.new(0.1) Duration.new(0.1) X::Constructor::Positional Duration.new(1.0) Duration.new(0.0) Duration.new(1180591620717411303424.0) 4.5 4.0 0.0 0.333333333 <1/0> Rat+{Duration::add-tai} 4500000000 Duration.new(1.5)
```
rakupp 4.0.1-84: differs — `.raku` prints a Num (`4.5e0`), `"meow"` gives 0 instead of `X::Str::Numeric`, `Duration.new(Inf)` IS a Duration (`~~ Duration` True) and `Duration.new(1, 2)` is 1; `WHICH` is a ValueObjAt yet `===` is still False; `.tai` is missing, so the line died there.

### DT-06  Duration arithmetic                                            D:partial R:yes V:spec
`Duration ± Real`, `Real + Duration`, `Duration ± Duration`, `-Duration`
and `Duration % Real` (floored, a 2**66 divisor fine) give a Duration;
`% 0` throws `X::Numeric::DivideByZero`; `[+]` and `.sum` over Durations
stay Durations; `+ "1"` numifies the string. `Real - Duration` has no
candidate and answers a Num (`-3.5e0`), as do `*`, `/`, `**` and
`Duration / Duration` (`3e0`) — the docs call these unspecified. `.abs`
keeps the Duration, `.floor`/`.round`/`div`/`mod` answer Ints,
`.fmt("%.2f")` formats the value, `x` repeats by the Int. Comparisons
(`==`, `<`, `<=>`, `cmp`, `sort`) go by value.
```
say (Duration.new(4.5) + 1).raku, " ", (1 + Duration.new(4.5)).raku, " ", (Duration.new(4.5) + Duration.new(1)).raku, " ", (Duration.new(4.5) - 1).raku, " ", (Duration.new(4.5) - Duration.new(1)).raku, " ", (-Duration.new(4.5)).raku, " ", (Duration.new(4.5) % 2).raku, " ", (Duration.new(-4.5) % 2).raku, " ", (try Duration.new(1) % 0) // $!.^name, " ", (Duration.new(4.5) * 2).raku, " ", (Duration.new(4.5) * 2).^name, " ", (2 * Duration.new(4.5)).^name, " ", (Duration.new(4.5) / 2).raku, " ", (Duration.new(4.5) / 2).^name, " ", (Duration.new(4.5) / Duration.new(1.5)).raku, " ", (Duration.new(4.5) == 4.5), " ", (Duration.new(4.5) < Duration.new(5)), " ", (Duration.new(4.5) <=> 5).raku, " ", (Duration.new(4.5) cmp Duration.new(5)).raku, " ", (Duration.new(5), Duration.new(1)).sort.raku, " ", (Duration.new(4.5) + 1e0).raku, " ", (Duration.new(4.5) - 1/3).raku, " ", (1 - Duration.new(4.5)).raku, " ", (1 - Duration.new(4.5)).^name, " ", (Duration.new(4.5) ** 2).raku, " ", Duration.new(4.5).abs.raku, " ", Duration.new(-4.5).abs.^name, " ", Duration.new(4.5).floor.raku, " ", Duration.new(4.5).round.raku, " ", (Duration.new(4.5) + Instant.from-posix(0)).^name, " ", ([+] Duration.new(1), Duration.new(2)).raku, " ", (Duration.new(4.5).fmt("%.2f")), " ", ("x" x Duration.new(2)), " ", (try (Duration.new(4.5) mod 2).raku) // $!.^name, " ", (try (Duration.new(4.5) div 2).raku) // $!.^name, " ", (Duration.new(4.5) % 2).^name, " ", (Duration.new(4.5) % 2.5e0).raku, " ", (Duration.new(0.5) + Duration.new(0.5)).raku, " ", (Duration.new(1) - Duration.new(1)).raku, " ", (Duration.new(4.5) == Duration.new(4.5)), " ", (Duration.new(4.5) === Duration.new(4.5)), " ", ((Duration.new(1), Duration.new(2)).sum).^name, " ", (Duration.new(4.5) + "1").raku, " ", (Duration.new(3) x 2), " ", (Duration.new(10) % 2**66).raku
# rakudo 2026.08: Duration.new(5.5) Duration.new(5.5) Duration.new(5.5) Duration.new(3.5) Duration.new(3.5) Duration.new(-4.5) Duration.new(0.5) Duration.new(1.5) X::Numeric::DivideByZero 9e0 Num Num 2.25e0 Num 3e0 True True Order::Less Order::Less (Duration.new(1.0), Duration.new(5.0)).Seq Duration.new(5.5) Duration.new(4.166666667) -3.5e0 Num 20.25e0 Duration.new(4.5) Duration 4 5 Instant Duration.new(3.0) 4.50 xx Duration.new(0.5) 2 Duration Duration.new(2.0) Duration.new(1.0) Duration.new(0.0) True False Duration Duration.new(5.5) 33 Duration.new(10.0)
```
rakupp 4.0.1-84: differs — `Duration ± Real`, `Duration ± Duration`, `-Duration`, `%`, `[+]`, `.sum` and `.abs` all fall out of the type and print as Nums (`5.5e0`), while `1 - Duration` answers a `Duration` where Rakudo answers a Num; the numeric values themselves agree.

## C. Date

### DT-07  Date.new forms, formatter, printing                            D:yes R:yes V:quirk
`Date.new($y, $m, $d)`: year and month go through `Int()` (Str "2018",
Rat 12/2 accepted; a fractional Rat truncates, 2.9 is February); the day
may be an Int, `*` (the month's last day), a Callable called with the
days-in-month (`*-1`, `{ $_ div 2 }`), or anything else through `.Int`
(3.7 is 3). `Date.new(:year!, :month = 1, :day = 1)` likewise (`:day(*)`
works). `Date.new(Str)` takes `YYYY-MM-DD` (DT-08). `Date.new(Instant)`
is the UTC date. `Date.new(Dateish)` copies year, month and day from a
Date or DateTime and, unless `:formatter` is given, also its formatter —
so a Date built from a DateTime with a formatter prints through a
DateTime formatter (the quirk). `:formatter` is a Callable given the
Date; `.formatter` is the Callable type object when none was given.
`.raku` is `Date.new(2015,12,24)` (formatter omitted), `.Str` and
`.gist` the formatter's output or `yyyy-mm-dd`. `Date()` coerces a Str
or Instant. Unknown nameds (`:foo`, `:timezone`) are ignored; one or two
positionals throw `X::Multi::NoMatch`; `Date.new` with no arguments, or
with only unknown nameds, returns a Failure (an `X::AdHoc`), not a
throw. A Date is Dateish and not Cool; `.IO` is an IO::Path of the Str;
a Date is True.
```
say Date.new(2015, 12, 24).raku, " ", Date.new(2015, 12, 24).Str, " ", Date.new(2015, 12, 24).gist, " ", Date.new("2015-12-24").raku, " ", Date.new(:year(2015), :month(12), :day(24)).raku, " ", Date.new(:year(2015)).raku, " ", Date.new(:year(2015), :month(3)).raku, " ", Date.new("2018", "1", "4").raku, " ", Date.new(2010, 12/2, 3).raku, " ", Date.new(2010, 2.9, 3).raku, " ", Date.new(2010, 1, 3.7).raku, " ", Date.new(Instant.from-posix(1234567890)).raku, " ", Date.new(DateTime.new("2020-05-06T23:30:00-05:00")).raku, " ", Date.new(Date.new("2020-05-06")).raku, " ", Date.new(2015,12,29, :formatter({ sprintf "%02d/%02d/%d", .day, .month, .year })).Str, " ", Date.new("2015-12-29", :formatter({ "F" })).gist, " ", Date.new("2015-12-29", :formatter({ "F" })).raku, " ", Date.new("2015-12-29").formatter.^name, " ", Date.new("2015-12-29", :formatter({ "F" })).formatter.^name, " ", Date("2020-01-02").raku, " ", Date(Instant.from-posix(0)).raku, " ", (Date.new(2020, 1, 2) ~~ Dateish), " ", (Date.new(2020,1,2) ~~ Cool), " ", Date.new(2020,1,2).IO.^name, " ", Date.new(2020,1,2).IO.Str, " ", Date.new("2020-01-02").Bool, " ", Date.new(Instant.from-posix(1234567890), :formatter({ "It is {.year}" })).Str, " ", Date.new(DateTime.new("2020-05-06T23:30:00Z", :formatter({ "DTF" }))).Str, " ", Date.new(DateTime.new("2020-05-06T23:30:00Z", :formatter({ "DTF" })), :formatter({ "DF" })).Str, " ", Date.new(2020, 1, 2, :foo).raku, " ", Date.new("2020-01-02", :timezone(3600)).raku, " ", (try Date.new(2020, 1)) // $!.^name, " ", (try Date.new(2020)) // $!.^name, " ", Date.new(2042, 2, *).raku, " ", Date.new(2044, 2, *).raku, " ", Date.new(2044, 2, *-1).raku, " ", Date.new(:year(2044), :month(2), :day(*)).raku, " ", Date.new(2044, 2, { $_ div 2 }).raku, " ", Date.new(:2020year, :day(*)).raku, " ", Date.new.^name, " ", do { my $f = Date.new; $f.so; $f.exception.^name }, " ", Date.new(:foo).^name
# rakudo 2026.08: Date.new(2015,12,24) 2015-12-24 2015-12-24 Date.new(2015,12,24) Date.new(2015,12,24) Date.new(2015,1,1) Date.new(2015,3,1) Date.new(2018,1,4) Date.new(2010,6,3) Date.new(2010,2,3) Date.new(2010,1,3) Date.new(2009,2,13) Date.new(2020,5,6) Date.new(2020,5,6) 29/12/2015 F Date.new(2015,12,29) Callable Block Date.new(2020,1,2) Date.new(1970,1,1) True False IO::Path 2020-01-02 True It is 2009 DTF DF Date.new(2020,1,2) Date.new(2020,1,2) X::Multi::NoMatch X::Multi::NoMatch Date.new(2042,2,28) Date.new(2044,2,29) Date.new(2044,2,28) Date.new(2044,2,29) Date.new(2044,2,14) Date.new(2020,1,31) Failure X::AdHoc Failure
```
rakupp 4.0.1-84: differs — `Date ~~ Cool` is True, the DateTime's formatter is not inherited, `Date.new(2020)` and `Date.new(2020, 1)` are accepted as 2020-01-01, `*-1` gives the last day instead of the day before it, and a Callable day is called with 0 (the line died with "Day out of range" there); `Date.new` with no arguments throws instead of failing.

### DT-08  Date validation: X::Temporal::OutOfRange, X::Temporal::InvalidFormat  D:partial R:yes V:spec
The month is checked first (1..12), then the day against the month's
length in that year (leap days: 2000, 2016, 2024 have 29 February; 1900,
1999, 2015 do not); each failure throws `X::Temporal::OutOfRange`, which
is an `X::OutOfRange` and does `X::Temporal`, with `.what` `Month` or
`Day`, `.got` the offending Int, `.range` `1..12` or `1..28`/`1..29`/
`1..31`, and an undefined `.comment`; day 0 and a negative day are Day
errors; `clone` re-validates the same way. The string form accepts
exactly `YYYY-MM-DD`, the year having a `+`/`-` sign and four or more
digits (`+12345-01-01`, `-1234-12-24`, `0000-01-01`); anything else —
three-digit year, one-digit month, leading or trailing whitespace or
newline, a time suffix, `/` separators, `20151224`, the empty string,
`malformed`, a synthetic (combining) codepoint — throws
`X::Temporal::InvalidFormat` with `.invalid-str` the string, `.target`
`Date` and `.format` `yyyy-mm-dd`. Non-ASCII decimal digits are accepted
(Arabic-Indic digits parse as 2010-01-02). A non-numeric year or month,
or a day that cannot `.Int`, throws `X::AdHoc`; a Rat day truncates
(24.5 is 24).
```
say do { sub E(&c) { my $r = try c(); $r.defined ?? "ok:" ~ $r.Str !! $!.^name }; (E({ Date.new(2015, 13, 1) }), E({ Date.new(2015, 12, 42) }), E({ Date.new(2015, 42, 42) }), E({ Date.new(2015, 0, 1) }), E({ Date.new(2015, 1, 0) }), E({ Date.new("2015-12-42") }), E({ Date.new(:year(2015), :month(2), :day(30)) }), E({ Date.new("1999-02-29") }), E({ Date.new("1900-02-29") }), E({ Date.new("2000-02-29") }), E({ Date.new("2024-02-29") }), E({ Date.new("malformed") }), E({ Date.new("2010-1-1") }), E({ Date.new("999-01-01") }), E({ Date.new("2010-01-01 ") }), E({ Date.new("2010-01-01T00:00:00") }), E({ Date.new("+12345-01-01") }), E({ Date.new("-1234-12-24") }), E({ Date.new("0000-01-01") }), E({ Date.new("2016-07\x[308]-05") }), E({ Date.new("٢٠١٠-٠١-٠٢") }), E({ Date.new(2015, 12, -1) }), E({ Date.new("2015-12-24").clone(:month(2), :day(30)) }), E({ Date.new(2015, "x", 1) }), E({ Date.new(2015, 1, "x") }), E({ Date.new("") }), E({ Date.new("2015/12/24") }), E({ Date.new("20151224") }), E({ Date.new(" 2015-12-24") }), E({ Date.new("2015-12-24\n") }), E({ Date.new(2015, 12, 24.5) }), E({ Date.new(2015, 2, 29) }), E({ Date.new(2016, 2, 29) }), E({ Date.new(2015, 12, 0) })).join(" ") }
# rakudo 2026.08: X::Temporal::OutOfRange X::Temporal::OutOfRange X::Temporal::OutOfRange X::Temporal::OutOfRange X::Temporal::OutOfRange X::Temporal::OutOfRange X::Temporal::OutOfRange X::Temporal::OutOfRange X::Temporal::OutOfRange ok:2000-02-29 ok:2024-02-29 X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat ok:+12345-01-01 ok:-1234-12-24 ok:0000-01-01 X::Temporal::InvalidFormat ok:2010-01-02 X::Temporal::OutOfRange X::Temporal::OutOfRange X::AdHoc X::AdHoc X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat ok:2015-12-24 X::Temporal::OutOfRange ok:2016-02-29 X::Temporal::OutOfRange
say do { sub EX(&c) { my $e; { c(); CATCH { default { $e = $_ } } }; $e }; (EX({ Date.new(2015,13,1) }).^name, EX({ Date.new("x") }).^name, (EX({ Date.new(2015,13,1) }) ~~ X::OutOfRange) ~ (EX({ Date.new(2015,13,1) }) ~~ X::Temporal) ~ (EX({ Date.new(2015,13,1) }) ~~ X::Temporal::OutOfRange), (EX({ Date.new("x") }) ~~ X::Temporal) ~ (EX({ Date.new("x") }) ~~ X::Temporal::InvalidFormat) ~ (EX({ Date.new("x") }) ~~ X::OutOfRange), do { my $e = EX({ Date.new(2015,13,1) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.got.^name ~ "|" ~ $e.comment.defined }, do { my $e = EX({ Date.new(2015,2,30) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ Date.new(2016,2,30) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ Date.new("2015-12-42") }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.got.^name }, do { my $e = EX({ Date.new(2015,42,42) }); $e.what ~ "|" ~ $e.got }, do { my $e = EX({ Date.new("malformed") }); $e.invalid-str ~ "|" ~ $e.target ~ "|" ~ $e.format }, do { my $e = EX({ Date.new(2015, 1, 0) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ Date.new(1900, 2, 29) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ Date.new("2015-12-31").clone(:month(2)) }); $e.^name ~ "|" ~ $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ Date.new(2015, 12, 24).clone(:day(0)) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ Date.new("2016-07\x[308]-05") }); $e.invalid-str.chars ~ "|" ~ $e.target ~ "|" ~ $e.format }).join(" ") }
# rakudo 2026.08: X::Temporal::OutOfRange X::Temporal::InvalidFormat TrueTrueTrue TrueTrueFalse Month|13|1..12|Int|False Day|30|1..28 Day|30|1..29 Day|42|1..31|Int Month|42 malformed|Date|yyyy-mm-dd Day|0|1..31 Day|29|1..28 X::Temporal::OutOfRange|Day|31|1..28 Day|0|1..31 10|Date|yyyy-mm-dd
```
rakupp 4.0.1-84: differs — the range checks and the leap-day rule agree, but the string parser accepts nearly everything (`malformed`, whitespace, a time suffix, `20151224` all give a date, most of them 0000-01-01, and Arabic-Indic digits give 0000-01-01 instead of 2010-01-02), a non-numeric month is an OutOfRange instead of `X::AdHoc`, `Date.new("x")` does not throw, and the exception lacks `.comment`, so the second line died at its fifth field.

### DT-09  Dateish accessors and the date strings                         D:yes R:yes V:spec
`year`, `month`, `day` (= `day-of-month`) are Ints; `day-of-week` is 1
for Monday to 7 for Sunday; `day-of-year` 1..366; `week` is a
two-element List (ISO week-year, ISO week number: 2014-12-31 is (2015,
1), 2016-01-02 is (2015, 53), 2005-01-01 is (2004, 53)); `week-year` and
`week-number` are its halves; `weekday-of-month` is `(day - 1) div 7 +
1`; `days-in-month`, `days-in-year`, `is-leap-year` (Gregorian rule);
`daycount` is the Modified Julian Day (1858-11-17 is 0, 1970-01-01 is
40587); `.Int`, `.Numeric`, `.Real` and `+` are the daycount.
`yyyy-mm-dd`, `mm-dd-yyyy`, `dd-mm-yyyy` take an optional separator
(`("/")`, `(".")`, `("")`); `yyyy-mm` and `mm-dd` exist. The class-level
`Date.days-in-month($y, $m)` and `Date.days-in-year($y)` need no
instance.
```
say do { my $d = Date.new("2000-02-28"); ($d.year, $d.month, $d.day, $d.day-of-month, $d.day-of-week, $d.day-of-year, $d.week.raku, $d.week-year, $d.week-number, $d.weekday-of-month, $d.is-leap-year, $d.daycount, $d.yyyy-mm-dd, $d.yyyy-mm-dd("/"), $d.mm-dd-yyyy, $d.dd-mm-yyyy, $d.dd-mm-yyyy("."), $d.Str, $d.gist, $d.raku, $d.Int, $d.Numeric, $d.Real, +$d, $d.year.^name, Date.new("1995-09-27").daycount, Date.new("1858-11-17").daycount, Date.new("1858-11-16").daycount, Date.new("1970-01-01").daycount, Date.new("2014-12-31").week.raku, Date.new("2016-01-02").week.raku, Date.new("2003-06-09").weekday-of-month, Date.new("2015-12-31").day-of-year, Date.new("2016-12-31").day-of-year, Date.new("2015-12-31").day-of-week, Date.new("2024-09-22").day-of-week, Date.new("1900-01-01").is-leap-year, Date.new("2000-01-01").is-leap-year, Date.new("2100-01-01").is-leap-year, Date.new("2020-01-01").is-leap-year, Date.new("2005-01-01").week.raku, Date.new("2007-12-31").week.raku, Date.new("2008-12-29").week-year, Date.new("1977-08-20").week-number, $d.week.^name, $d.yyyy-mm-dd("").^name, Date.new("2020-03-31").weekday-of-month, Date.new("2020-03-29").weekday-of-month, Date.new("2019-12-30").week.raku, Date.new("2021-01-03").week.raku, Date.days-in-month(2024, 2), Date.days-in-year(2024), Date.days-in-year(1900), $d.yyyy-mm, $d.mm-dd, $d.days-in-month, $d.days-in-year).join(" ") }
# rakudo 2026.08: 2000 2 28 28 1 59 (2000, 9) 2000 9 4 True 51602 2000-02-28 2000/02/28 02-28-2000 28-02-2000 28.02.2000 2000-02-28 2000-02-28 Date.new(2000,2,28) 51602 51602 51602 51602 Int 49987 0 -1 40587 (2015, 1) (2015, 53) 2 365 366 4 7 False True False True (2004, 53) (2008, 1) 2009 33 List Str 5 5 (2020, 1) (2020, 53) 29 366 365 2000-02 02-28 29 366
```
rakupp 4.0.1-84: differs — the separator argument of `yyyy-mm-dd`/`dd-mm-yyyy` is ignored (`2000-02-28` for `("/")`), and `days-in-month`, `days-in-year` (instance and class forms), `yyyy-mm` and `mm-dd` are missing, so the line died at `$d.days-in-month`; every field before it agrees.

### DT-10  Years outside 1000..9999                                       D:no R:yes V:spec
A year from 0 to 999 prints zero-padded to four digits (`0999-01-01`,
`0005-01-01`, `0000-01-01`); a negative year or one above 9999 prints
with its sign and at least five digits (`-0001-12-27`, `+10000-01-01`,
`+10173-10-16`, `-13000000000-01-01`), in `Str`, `yyyy-mm-dd`,
`mm-dd-yyyy` (`06-07-+12345`), `dd-mm-yyyy`, `yyyy-mm` and in
DateTime's Str alike; `.raku` prints the bare Int. The same forms parse
back (`"0999-01-01"` is year 999, `"+10000-01-01"` 10000,
`"-0001-01-01"` −1). The calendar is proleptic Gregorian: year 0 exists
and is a leap year, daycounts and weekdays continue smoothly (0000-01-01
is a Saturday, -4713-11-24 has daycount −2400001), ISO weeks too
(`-0001-01-01` is week 53 of −2).
```
say Date.new(999, 1, 1).Str, " ", Date.new(0, 1, 1).Str, " ", Date.new(-1, 12, 27).Str, " ", Date.new(10000, 1, 1).Str, " ", Date.new(-13_000_000_000, 1, 1).Str, " ", Date.new(999,1,1).yyyy-mm-dd, " ", Date.new(999,1,1).mm-dd-yyyy, " ", Date.new(999,1,1).dd-mm-yyyy, " ", Date.new(999,1,1).raku, " ", Date.new(10000,1,1).raku, " ", Date.new("0999-01-01").year, " ", Date.new("+10000-01-01").year, " ", Date.new("-0001-01-01").year, " ", Date.new(12345, 6, 7).mm-dd-yyyy, " ", Date.new(5, 1, 1).Str, " ", Date.new(0, 1, 1).is-leap-year, " ", Date.new(0,1,1).daycount, " ", Date.new(-1, 3, 1).day-of-week, " ", Date.new("9900-01-01") + 100000, " ", Date.new("0000-01-01").truncated-to("week"), " ", DateTime.new(999,1,1,0,0,0).Str, " ", DateTime.new(10000,1,1,0,0,0).Str, " ", DateTime.new(-1,1,1,0,0,0).Str, " ", DateTime.new("+9992000-01-01T00:00:00").Str, " ", DateTime.new("-4004-10-23T00:00:00").Str, " ", Date.new(1000, 1, 1).Str, " ", Date.new(9999, 12, 31).Str, " ", Date.new(-99999, 1, 1).Str, " ", DateTime.new(0,1,1,0,0,0).Str, " ", Date.new(-4713, 11, 24).daycount, " ", Date.new(0, 1, 1).day-of-week, " ", Date.new(-1, 1, 1).week.raku, " ", Date.new(-1,1,1).yyyy-mm
# rakudo 2026.08: 0999-01-01 0000-01-01 -0001-12-27 +10000-01-01 -13000000000-01-01 0999-01-01 01-01-0999 01-01-0999 Date.new(999,1,1) Date.new(10000,1,1) 999 10000 -1 06-07-+12345 0005-01-01 True -678941 1 +10173-10-16 -0001-12-27 0999-01-01T00:00:00Z +10000-01-01T00:00:00Z -0001-01-01T00:00:00Z +9992000-01-01T00:00:00Z -4004-10-23T00:00:00Z 1000-01-01 9999-12-31 -99999-01-01 0000-01-01T00:00:00Z -2400001 6 (-2, 53) -0001-01
```
rakupp 4.0.1-84: differs — a negative year prints with three digits (`-001-12-27`, also in DateTime's Str) and the `+` is dropped in `mm-dd-yyyy`; before year 1 the calendar is one day off (`Date.new(-1, 3, 1).day-of-week` 2, `Date.new(0, 1, 1).day-of-week` 7, week `(-2, 52)`); `yyyy-mm` is missing, so the line died at its last field.

### DT-11  Date ± Int, Date − Date, succ and pred, the Numeric fallback   D:yes R:yes V:quirk
`Date + Int`, `Int + Date` and `Date - Int` move by that many days
(negative, zero, `True` as 1, 2**40 all fine); `Date - Date` is the
signed Int number of days; `.succ`/`.pred` step one day (so `++`, `--`,
`+=` work); a formatter survives. Any other operand falls to the Numeric
fallback on the daycount with no error (the quirk): `Date + Date` is an
Int, `Date + 1.5` and `Date - 1.5` Rats, `Date + 1e0` a Num, `Date + "1"`
an Int (the string is numified), `Date * 2` an Int, `Date + Duration` a
Duration (daycount plus seconds), `Date - Duration` a Num, `1 - Date` an
Int.
```
say (Date.new("2000-02-28") + 7).Str, " ", (7 + Date.new("2000-02-28")).Str, " ", (Date.new("2000-03-06") - 14).Str, " ", (Date.new("2000-02-28") - Date.new("2000-02-21")).raku, " ", (Date.new("2000-02-21") - Date.new("2000-02-28")).raku, " ", (Date.new("2019-01-01") + -100).Str, " ", (Date.new("2019-01-01") - -100).Str, " ", (Date.new("2000-02-21") + 0).Str, " ", Date.new("2010-04-12").succ.Str, " ", Date.new("2010-04-12").pred.Str, " ", Date.new("2000-02-28").succ.Str, " ", Date.new("2000-02-29").succ.Str, " ", Date.new("2000-03-01").pred.Str, " ", Date.new("2000-12-31").succ.Str, " ", Date.new("2000-01-01").pred.Str, " ", (Date.new("2020-01-01") + Date.new("2020-01-01")).raku, " ", (Date.new("2020-01-01") + 1.5).raku, " ", (Date.new("2020-01-01") - 1.5).raku, " ", (Date.new("2020-01-01") + Duration.new(1)).raku, " ", (Date.new("2020-01-01") - Duration.new(1)).raku, " ", (Date.new("2020-01-01") * 2).raku, " ", (Date.new("2020-01-01") + 2**40).Str, " ", (Date.new("2020-01-01") - 2**40).Str, " ", (Date.new("2020-01-01") + "1").raku, " ", (Date.new("2020-01-01") + True).raku, " ", do { my $d = Date.new("2020-01-31"); $d++; $d.Str }, " ", do { my $d = Date.new("2020-01-01"); $d--; $d.Str }, " ", do { my $d = Date.new("2020-01-01"); $d += 3; $d.Str }, " ", Date.new("2020-01-01").succ.^name, " ", (Date.new("2020-01-01") + 1e0).raku, " ", (Date.new("2020-01-01", :formatter({"F"})) + 1).Str, " ", (Date.new("2020-01-01", :formatter({"F"})).succ).Str, " ", (Date.new("2020-01-01") - Date.new("2020-01-01", :formatter({"F"}))).raku, " ", (Date.new("2020-01-01") - Date.new("2020-01-01")).^name, " ", (1 - Date.new("2020-01-01")).raku, " ", (Date.new("2020-01-01") + 1).daycount
# rakudo 2026.08: 2000-03-06 2000-03-06 2000-02-21 7 -7 2018-09-23 2019-04-11 2000-02-21 2010-04-13 2010-04-11 2000-02-29 2000-03-01 2000-02-29 2001-01-01 1999-12-31 117698 58850.5 58847.5 Duration.new(58850.0) 58848e0 117698 +3010362609-12-15 -3010358570-01-18 58850 Date.new(2020,1,2) 2020-02-01 2019-12-31 2020-01-04 Date 58850e0 F F 0 Int -58848 58850
```
rakupp 4.0.1-84: differs — the day moves and `Date - Date` agree, but the fallback results are all Nums (`117698e0`, `Date + Duration` `58850e0`, `1 - Date` `-58848e0`).

### DT-12  Comparing Dates: numeric by daycount, `cmp` by Str, identity   D:partial R:yes V:quirk
`==`, `!=`, `<`, `<=`, `>`, `>=` and `<=>` compare daycounts (so `== 38202`
and `== $a.daycount` are True and `< 0` False); `before`/`after` work;
`cmp` and `leg` — hence `sort`, `min`, `max` — compare the `.Str`, which
is the ISO string by default but the formatter's output otherwise: two
Dates with a constant formatter are `cmp` Same whatever their day, and
`cmp "1963-07-02"` is Same while `cmp 38202` is a string Less (the
quirk). `WHICH` is `Date|<daycount>` (a ValueObjAt), so `===`, `eqv`,
`~~` against a Date and `.unique` treat equal days as one value,
formatter ignored; a subclass has `D2|<daycount>` and is neither `===`
nor `eqv` a plain Date, though `==` holds. `eq`/`lt` use the Str; `~~
Str` is string equality, `Str ~~ Date` is False; `Date ~~ DateTime` is
False (DateTime.ACCEPTS is `==` on Instants) while `DateTime ~~ Date`
compares year, month and day as stored, ignoring the zone (23:59:59-05:00
matches its own calendar date, not the UTC one); `Date ~~ Date` (type
object) is False, `Date:U ~~ instance` False.
```
say do { my $a = Date.new("1963-07-02"); my $b = Date.new("1964-02-01"); ($a == $a, $a == $b, $a != $b, $a < $b, $a <= $a, $a > $b, $a >= $b, ($a cmp $b).raku, ($b cmp $a).raku, ($a cmp $a).raku, ($a <=> $b).raku, ($a === Date.new("1963-07-02")), ($a eqv Date.new("1963-07-02")), ($a eqv $b), ($a ~~ Date.new("1963-07-02")), ($a ~~ $b), $a.WHICH, $a.WHICH.^name, ($a eq "1963-07-02"), ($a lt $b), ($a before $b), ($b after $a), ($b, $a, Date.new("1963-01-01")).sort.map(*.Str).raku, ($a == 38202), ($a == $a.daycount), ($a < 0), ($a cmp "1963-07-02").raku, ($a cmp 38202).raku, (Date.new("2020-01-01", :formatter({"x"})) cmp Date.new("2021-01-01", :formatter({"x"}))).raku, (Date.new("2020-01-01", :formatter({"x"})) === Date.new("2021-01-01", :formatter({"x"}))), (Date.new("2020-01-01", :formatter({"x"})) == Date.new("2020-01-01")), ($a ~~ DateTime.new("1963-07-02T23:59:59-05:00")), ($a ~~ DateTime.new("1963-07-03T00:00:00Z")), (DateTime.new("1963-07-02T10:00:00Z") ~~ $a), ($a ~~ Date), (Date ~~ $a), ($a.clone === $a), (($a, $a).unique.elems), do { class D2 is Date {}; D2.new("1963-07-02").WHICH ~ " " ~ (D2.new("1963-07-02") === $a) ~ " " ~ (D2.new("1963-07-02") == $a) ~ " " ~ (D2.new("1963-07-02") eqv $a) }, ($a leg $b).raku, ($a eqv "1963-07-02"), ($a max $b).Str, (($b, $a).min).Str, ($a ~~ "1963-07-02"), ("1963-07-02" ~~ $a)).join(" ") }
# rakudo 2026.08: True False True True True False False Order::Less Order::More Order::Same Order::Less True True False True False Date|38212 ValueObjAt True True True True ("1963-01-01", "1963-07-02", "1964-02-01").Seq False True False Order::Same Order::Less Order::Same False True False False True True False True 1 D2|38212 False True False Order::Less False 1964-02-01 1963-07-02 True False
```
rakupp 4.0.1-84: differs — `$a ~~ Date.new("1963-07-02")` is False, `Date ~~ $a` warns "uninitialized value of type Date in string context", a subclass's `WHICH` is an address and it is `eqv` a plain Date; everything else, including the `cmp`-by-Str quirk, agrees.

### DT-13  Date ranges                                                    D:partial R:yes V:bug
`Date .. Date` is a Range: `.elems` (an Int) and `.list` walk by `succ`;
`..^`, `^..^` exclude; a reversed pair is empty; `.reverse` walks down;
`Date .. *` is lazy and infinite and indexable; `[n]`, `.tail`, `.pick`,
`.roll` give Dates; `.Str` joins the days with spaces, `.raku` is
`Date.new(2019,5,1)..Date.new(2019,5,5)`, `.minmax`/`.bounds` the
endpoints, `.is-int` and `.infinite` False; `Date ~~ Range` and
`for` work. Because containment and iteration compare with `cmp`, i.e.
by `.Str` (DT-12), a Str topic matches a Date range (`"2019-05-03" ~~
$r` True) and a mixed `Date .. "2019-05-03"` has three elements — and,
the bug, a formatter on one endpoint breaks the range: a start formatted
as `~.day` against a plain end yields only `1,2` (iteration stops when
`"3"` sorts after `"2019-05-03"`). A second bug: `.sum` of a Date range
is a Date (the Ints re-add to a Date). Do not imitate either.
```
say do { my $r = Date.new("2019-05-01") .. Date.new("2019-05-05"); ($r.^name, $r.elems, $r.list.map(*.Str).raku, $r.min.Str, (Date.new("2019-05-03") ~~ $r), (Date.new("2019-05-06") ~~ $r), (Date.new("2019-05-05") ~~ Date.new("2019-05-01")..^Date.new("2019-05-05")), (Date.new("2019-05-01")..^Date.new("2019-05-05")).elems, (Date.new("2019-05-05") .. Date.new("2019-05-01")).elems, $r.reverse.map(*.Str).raku, (Date.new("2019-05-01") .. *).is-lazy, (Date.new("2019-05-01") .. *).head(2).map(*.Str).raku, (Date.new("2019-05-01") .. *)[3].Str, $r[1].Str, $r.tail.Str, (Date.new("2019-05-01", :formatter({ ~.day })) .. Date.new("2019-05-03")).join(","), $r.sum.^name, +$r, (Date.new("2019-05-01") ..^ Date.new("2019-05-01")).elems, (Date.new("2020-02-27") .. Date.new("2020-03-02")).map(*.day).raku, do { my $n = 0; for Date.new("2020-01-30") .. Date.new("2020-02-02") { $n++ }; $n }, (Date.new("2020-01-01")..Date.new("2020-12-31")).elems, $r.pick.^name, $r.roll.^name, $r.Str, $r.raku, (Date.new("2019-05-01") ^..^ Date.new("2019-05-05")).elems, $r.elems.^name, ($r.list.head ~~ Date), (Date.new("2019-05-03") ~~ Date.new("2019-05-01")..*), (Date.new("2018-05-03") ~~ Date.new("2019-05-01")..*), ("2019-05-03" ~~ $r), (Date.new("2019-05-01") .. "2019-05-03").elems, ($r.list.head === Date.new("2019-05-01")), $r.minmax.map(*.Str).raku, $r.bounds.map(*.Str).raku, $r.is-int, $r.infinite).join(" ") }
# rakudo 2026.08: Range 5 ("2019-05-01", "2019-05-02", "2019-05-03", "2019-05-04", "2019-05-05").Seq 2019-05-01 True False False 4 0 ("2019-05-05", "2019-05-04", "2019-05-03", "2019-05-02", "2019-05-01").Seq True ("2019-05-01", "2019-05-02").Seq 2019-05-04 2019-05-02 2019-05-05 1,2 Date 5 0 (27, 28, 29, 1, 2).Seq 4 366 Date Date 2019-05-01 2019-05-02 2019-05-03 2019-05-04 2019-05-05 Date.new(2019,5,1)..Date.new(2019,5,5) 3 Int True True False True 3 True ("2019-05-01", "2019-05-05").Seq ("2019-05-01", "2019-05-05").Seq False False
```
rakupp 4.0.1-84: differs — `Date .. Date` is a List (its `.raku` is a list of hashes), `Date ~~ $r` is False, `Date .. *` yields Ints (`58604`), `.sum` is a Num, `.is-int`/`.bounds` are missing (the line died at `.bounds`); it iterates by value, so the formatter case gives `1,2,3`.

### DT-14  later and earlier on a Date                                    D:yes R:yes V:quirk
`.later(unit => n)` / `.earlier` take exactly ONE named unit: `day`/
`days` and `week`/`weeks` (×7) move by days; `month`/`months` add to the
month with year carry and clip the day to the new month (2014-01-31 +1
month is 02-28, +13 months 2015-02-28, 2015-12-31 +2 months 2016-02-29;
so two single-month moves differ from one two-month move: 2015-03-28 vs
2015-03-31); `year`/`years` likewise (2016-02-29 +1 year is 2017-02-28,
+4 years 2020-02-29); negative counts and `:month` (= 1) are fine;
`earlier` negates. A List of Pairs applies each in order; an empty List
leaves the date; a non-Pair element throws `X::AdHoc`. Two named units,
no unit, or `hour`/`minute`/`second` on a Date throw `X::AdHoc`; an
unknown unit (`fortnight`) throws `X::TypeCheck::Return` (the move
yields nothing — the quirk); the count must be an Int (Bool or an IntStr
`<2>` pass): a Rat, Num or Str count throws `X::AdHoc`. The formatter is
kept; counts of 10**12 years or 2**40 days are fine.
```
say do { my $d = Date.new("2014-01-31"); ($d.later(day => 1).Str, $d.later(days => 2).Str, $d.later(week => 1).Str, $d.later(weeks => 2).Str, $d.later(month => 1).Str, $d.later(months => 13).Str, $d.later(year => 1).Str, $d.later(years => 3).Str, $d.later(:month).Str, $d.later(day => -1).Str, $d.earlier(day => 1).Str, $d.earlier(days => 31).Str, $d.earlier(month => 1).Str, $d.earlier(months => 15).Str, $d.earlier(year => 1).Str, $d.earlier(day => -1).Str, Date.new("2016-02-29").later(:1year).Str, Date.new("2016-02-29").earlier(:1year).Str, Date.new("2016-02-29").later(:4years).Str, Date.new("2015-12-31").later(months => 2).Str, Date.new("2015-03-31").earlier(month => 1).Str, Date.new("2015-01-31").later(month => 1).later(month => 1).Str, Date.new("2015-01-31").later(months => 2).Str, Date.new("2015-12-25").later(years => 1_000_000_000_000).Str, Date.new("2015-12-25").earlier(days => 1_000_000_000_000).Str, Date.new("2021-03-31").earlier((year => 3, month => 2, day => 8)).Str, Date.new("2021-03-31").later((:1month, :2days)).Str, Date.new("2021-03-31").later((:2days, :1month)).Str, Date.new("2021-03-31").later(()).Str, (try Date.new("2021-03-31").later(:1day, :1month)) // $!.^name, (try Date.new("2021-03-31").later()) // $!.^name, (try Date.new("2021-03-31").later(:1hour)) // $!.^name, (try Date.new("2021-03-31").later(:1second)) // $!.^name, (try Date.new("2021-03-31").later(:1fortnight).raku) // $!.^name, (try Date.new("2021-03-31").later((1, 2))) // $!.^name, (try Date.new("2021-03-31").later(days => 2.7).Str) // $!.^name, (try Date.new("2021-03-31").later(day => "2").Str) // $!.^name, Date.new("2021-03-31", :formatter({"F"})).later(:1day).Str, Date.new("2021-03-31").later(day => 0).Str, Date.new("2021-01-01").later(:month(-13)).Str, Date.new("2021-01-31").later(:month(-11)).Str, Date.new("2021-03-31").later(:1month).daycount, (try Date.new("2021-03-31").later(:1minute)) // $!.^name, Date.new("2021-03-31").later(:1day).^name, (try Date.new("2021-03-31").later((:1day, 5))) // $!.^name, Date.new("2021-03-31").later(:day(2**40)).Str, Date.new("2021-01-31").later(:24months).Str, Date.new("2021-01-31").later(:1years).Str, (try Date.new("2021-03-31").later(days => 2e0).Str) // $!.^name, Date.new("2021-03-31").later(days => <2>).Str).join(" ") }
# rakudo 2026.08: 2014-02-01 2014-02-02 2014-02-07 2014-02-14 2014-02-28 2015-02-28 2015-01-31 2017-01-31 2014-02-28 2014-01-30 2014-01-30 2013-12-31 2013-12-31 2012-10-31 2013-01-31 2014-02-01 2017-02-28 2015-02-28 2020-02-29 2016-02-29 2015-02-28 2015-03-28 2015-03-31 +1000000002015-12-25 -2737904992-12-29 2018-01-23 2021-05-02 2021-05-02 2021-03-31 X::AdHoc X::AdHoc X::AdHoc X::AdHoc X::TypeCheck::Return X::AdHoc X::AdHoc X::AdHoc F 2021-03-31 2019-12-01 2020-02-29 59334 X::AdHoc Date X::AdHoc +3010362611-03-15 2023-01-31 2022-01-31 X::AdHoc 2021-04-02
```
rakupp 4.0.1-84: differs — every valid move agrees, but nothing is refused: two units are both applied (2021-05-01), no unit, `:1hour`, `:1second`, `:1minute`, `:1fortnight` and a non-Pair list leave the date, `days => 2.7`, `days => 2e0` and `day => "2"` move by 2.

### DT-15  truncated-to on a Date                                         D:yes R:yes V:quirk
`year` gives January 1, `month` day 1, `week` the Monday on or before
(across months, years and 29 February); the formatter is kept, and a
date already at the boundary comes back equal. The unit is matched by
its first characters, so `months`, `yearly` and `weeks` work (the quirk);
`day`, `hour`, `fortnight`, the empty string, `Year` (case) and a
non-Str (1) throw `X::AdHoc` — a Date has no `day` truncation.
```
say Date.new("2012-12-24").truncated-to("year").Str, " ", Date.new("2012-12-24").truncated-to("month").Str, " ", Date.new("2012-12-24").truncated-to("week").Str, " ", Date.new("1999-01-17").truncated-to("week").Str, " ", Date.new("1999-01-03").truncated-to("week").Str, " ", Date.new("2000-03-01").truncated-to("week").Str, " ", Date.new("1988-03-03").truncated-to("week").Str, " ", (try Date.new("2012-12-24").truncated-to("day").Str) // $!.^name, " ", (try Date.new("2012-12-24").truncated-to("hour").Str) // $!.^name, " ", (try Date.new("2012-12-24").truncated-to("fortnight").Str) // $!.^name, " ", Date.new("2012-12-24").truncated-to("months").Str, " ", Date.new("2012-12-24").truncated-to("yearly").Str, " ", Date.new("2012-12-24").truncated-to("weeks").Str, " ", (try Date.new("2012-12-24").truncated-to("").Str) // $!.^name, " ", Date.new("2012-12-24", :formatter({"F"})).truncated-to("year").Str, " ", Date.new("2012-12-24").truncated-to("week").day-of-week, " ", Date.new("2012-12-24").truncated-to("year").daycount, " ", Date.new("2012-12-01").truncated-to("month").Str, " ", (Date.new("2012-12-01").truncated-to("month") === Date.new("2012-12-01")), " ", Date.new("2012-12-24").truncated-to("week").^name, " ", (try Date.new("2012-12-24").truncated-to("Year").Str) // $!.^name, " ", Date.new("2012-12-24").truncated-to("month").raku, " ", (try Date.new("2012-12-24").truncated-to(1).Str) // $!.^name
# rakudo 2026.08: 2012-01-01 2012-12-01 2012-12-24 1999-01-11 1998-12-28 2000-02-28 1988-02-29 X::AdHoc X::AdHoc X::AdHoc 2012-12-01 2012-01-01 2012-12-24 X::AdHoc F 1 55927 2012-12-01 True Date X::AdHoc Date.new(2012,12,1) X::AdHoc
```
rakupp 4.0.1-84: differs — the valid truncations agree, but every refused unit returns the date unchanged.

### DT-16  clone, first/last-date-in-month, new-from-daycount            D:yes R:yes V:spec
`.clone(:year, :month, :day, :formatter)` replaces fields and
re-validates (1999-01-29 with `:month(2)` throws
`X::Temporal::OutOfRange`; with `:year(2000)` too it is 2000-02-29); the
day may be `*`, `*-1`, a Str or a Rat (through `.Int`), but a Str or Rat
month (or year) throws `X::AdHoc` — they must be Ints; unknown nameds
are ignored; `:formatter(Callable)` restores the default; with no
arguments the copy is `===` the original. `first-date-in-month` and
`last-date-in-month` keep the formatter, return the invocant when already
there, and on the type object throw `X::Parameter::InvalidConcreteness`.
`Date.new-from-daycount($mjd, :formatter)` accepts any Int (0 is
1858-11-17, −1 the day before, 40587 is 1970-01-01, 2**40 works) and
throws `X::AdHoc` for a Str, a Rat or no argument; called on an instance
it keeps that instance's formatter unless one is given; a subclass stays
its own class.
```
say Date.new("2015-11-24").clone(month => 12).Str, " ", Date.new("2015-11-24").clone(:1day, :2month, :2017year).Str, " ", (try Date.new("1999-01-29").clone(month => 2).Str) // $!.^name, " ", Date.new("1999-01-29").clone(:month(2), :year(2000)).Str, " ", Date.new("2015-11-24").clone(day => *).Str, " ", Date.new("2015-11-24").clone(:month(2), :day(*)).Str, " ", Date.new("2015-11-24").clone(day => *-1).Str, " ", Date.new("2015-11-24").clone(day => "5").Str, " ", Date.new("2015-11-24").clone().Str, " ", (Date.new("2015-11-24").clone() === Date.new("2015-11-24")), " ", Date.new("2015-11-24", :formatter({"F"})).clone(:1day).Str, " ", Date.new("2015-11-24").clone(:formatter({"G"})).Str, " ", Date.new("2015-11-24", :formatter({"F"})).clone(:formatter(Callable)).Str, " ", Date.new("2015-11-24").clone(:foo(1)).Str, " ", Date.new("2015-11-24").first-date-in-month.Str, " ", Date.new("2015-11-24").last-date-in-month.Str, " ", Date.new("2016-02-10").last-date-in-month.Str, " ", Date.new("2015-11-01").first-date-in-month.Str, " ", Date.new("2020-02-20", :formatter({ "F" })).first-date-in-month.Str, " ", (try Date.first-date-in-month) // $!.^name, " ", (try Date.last-date-in-month) // $!.^name, " ", (try Date.new("2015-11-24").clone(:month("12")).Str) // $!.^name, " ", (try Date.new("2015-11-24").clone(:month(12.9)).Str) // $!.^name, " ", (try Date.new("2015-11-24").clone(:day(3.9)).Str) // $!.^name, " ", (Date.new("2015-11-30").last-date-in-month === Date.new("2015-11-30")), " ", Date.new("2015-11-24").last-date-in-month.daycount, " ", Date.new-from-daycount(49987).Str, " ", Date.new-from-daycount(0).Str, " ", Date.new-from-daycount(-1).Str, " ", Date.new-from-daycount(40587).Str, " ", Date.new-from-daycount(49987, :formatter({"F"})).Str, " ", Date.new("2020-01-01", :formatter({"F"})).new-from-daycount(49987).Str, " ", Date.new("2020-01-01", :formatter({"F"})).new-from-daycount(49987, :formatter({"G"})).Str, " ", (try Date.new-from-daycount("49987").Str) // $!.^name, " ", (try Date.new-from-daycount(49987.9).Str) // $!.^name, " ", Date.new-from-daycount(49987).daycount, " ", do { class D3 is Date {}; D3.new-from-daycount(49987).^name }, " ", Date.new-from-daycount(2**40).Str, " ", (try Date.new-from-daycount()) // $!.^name
# rakudo 2026.08: 2015-12-24 2017-02-01 X::Temporal::OutOfRange 2000-02-29 2015-11-30 2015-02-28 2015-11-29 2015-11-05 2015-11-24 True F G 2015-11-24 2015-11-24 2015-11-01 2015-11-30 2016-02-29 2015-11-01 F X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness X::AdHoc X::AdHoc 2015-11-03 True 57356 1995-09-27 1858-11-17 1858-11-16 1970-01-01 F F G X::AdHoc X::AdHoc 49987 D3 +3010362448-10-30 X::AdHoc
```
rakupp 4.0.1-84: differs — `*-1` gives the last day (2015-11-30), `Date.first-date-in-month` on the type object is `X::Method::NotFound`, a Str or Rat month is accepted, `new-from-daycount` ignores `:formatter` and its instance form is missing, so the line died there.

### DT-17  Date to DateTime and back; the Date type object                D:yes R:yes V:spec
`Date.DateTime` is midnight UTC (timezone 0) and does NOT carry the
formatter; `.Date` is the invocant; `DateTime(Date)` coerces the same
way; `DateTime.new(Date)` as a positional throws `X::Multi::NoMatch`,
the named `DateTime.new(:date(Date), :hour, :minute, :second,
:timezone)` works (a `:year` beside `:date` throws `X::AdHoc`, an
out-of-range `:minute(60)` `X::Temporal::OutOfRange`, the Date's
formatter is not used). On the type object `Date.DateTime` and
`Date.Date` are the type objects, `.gist` is `(Date)`, `.raku` `Date`,
`.Str` `""` with a warning, `.defined` and `?` False; `.year`,
`.daycount`, `.yyyy-mm-dd` throw `X::AdHoc`, `.succ` and `.is-leap-year`
`X::Parameter::InvalidConcreteness`, `Date.days-in-month` without
arguments `X::Multi::NoMatch`, `Date.is-leap-year(2024)` `X::AdHoc` (no
class form). `Date ~~ Dateish` True, `~~ Cool` False, MRO Date, Any, Mu,
roles (Dateish). The attributes are read-only: assigning `.year`, `.day`
or `.formatter` throws `X::Assignment::RO`.
```
say Date.new("2015-12-24").DateTime.Str, " ", Date.new("2015-12-24").DateTime.timezone, " ", Date.new("2015-12-24").DateTime.^name, " ", Date.new("2015-12-24").Date.Str, " ", (Date.new("2015-12-24").Date === Date.new("2015-12-24")), " ", Date.DateTime.^name, " ", Date.DateTime.defined, " ", Date.Date.^name, " ", Date.Date.defined, " ", Date.gist, " ", Date.raku, " ", (quietly Date.Str).raku, " ", Date.defined, " ", ?Date, " ", (try Date.year) // $!.^name, " ", (try Date.daycount) // $!.^name, " ", (try Date.succ) // $!.^name, " ", (try Date.yyyy-mm-dd) // $!.^name, " ", (try Date.is-leap-year) // $!.^name, " ", (Date ~~ Dateish), " ", (Date ~~ Cool), " ", (Date ~~ Any), " ", Date.^mro.map(*.^name).raku, " ", (try Date.new("2015-12-24").year = 2000) // $!.^name, " ", Date.new("2015-12-24").DateTime.hh-mm-ss, " ", Date.new("2015-12-24", :formatter({"F"})).DateTime.Str, " ", DateTime(Date.new("2015-12-24")).Str, " ", (try DateTime.new(Date.new("2015-12-24")).Str) // $!.^name, " ", DateTime.new(:date(Date.new("2015-12-24"))).Str, " ", DateTime.new(:date(Date.new("2015-12-24")), :hour(5), :timezone(3600)).Str, " ", DateTime.new(date => Date.new("2015-12-24"), minute => 59).Str, " ", (try DateTime.new(date => Date.new(1999, 1, 1), minute => 60)) // $!.^name, " ", Date.new("2015-12-24").DateTime.Date.Str, " ", (Date.new("2015-12-24").DateTime.Date === Date.new("2015-12-24")), " ", (try DateTime.new(:date(Date.new("2015-12-24")), :year(2000)).Str) // $!.^name, " ", DateTime.new(:date(Date.new("2015-12-24", :formatter({"F"})))).Str, " ", (try Date.is-leap-year(2024)) // $!.^name, " ", (try Date.new("2015-12-24").day = 1) // $!.^name, " ", (try Date.new("2015-12-24").formatter = { 1 }) // $!.^name, " ", (try Date.days-in-month) // $!.^name, " ", Date.days-in-month(2023, 2), " ", Date.^roles.map(*.^name).raku
# rakudo 2026.08: 2015-12-24T00:00:00Z 0 DateTime 2015-12-24 True DateTime False Date False (Date) Date "" False False X::AdHoc X::AdHoc X::Parameter::InvalidConcreteness X::AdHoc X::Parameter::InvalidConcreteness True False True ("Date", "Any", "Mu").Seq X::Assignment::RO 00:00:00 2015-12-24T00:00:00Z 2015-12-24T00:00:00Z X::Multi::NoMatch 2015-12-24T00:00:00Z 2015-12-24T05:00:00+01:00 2015-12-24T00:59:00Z X::Temporal::OutOfRange 2015-12-24 True 2000-01-01T00:00:00Z 2015-12-24T00:00:00Z X::AdHoc X::Assignment::RO X::Assignment::RO X::Multi::NoMatch 28 ("Dateish",).Seq
```
rakupp 4.0.1-84: differs — on the type object `.year`/`.daycount` are `X::Method::NotFound` and `.succ` answers 1; `Date ~~ Cool` True; `DateTime.new(Date)` positional is accepted; `:date` with `:year(2000)` gives 2000-12-24 instead of `X::AdHoc`; `Date.days-in-month` is missing, so the line died there.

## D. DateTime construction and validation

### DT-18  ISO 8601 strings                                               D:yes R:yes V:spec
`DateTime.new(Str)` accepts `YYYY-MM-DDThh:mm:ss` followed by nothing, `Z`,
`z`, `±hh`, `±hhmm` or `±hh:mm`; `t` for `T`; a fraction after the
seconds with `.` or `,` and 1 to 12 digits, kept exactly
(`10.123456789012`, `"10,5"` is 10.5); a signed year of four or more
digits; and a bare `YYYY-MM-DD`, which is midnight. Without an offset the
zone is `:timezone` if given, else 0; an offset sets `.timezone` in
seconds (`-00:30` is −1800) with no bound on the hours (`+99:00` is
356400). `.second` is an Int when the string has no fraction and a Rat
otherwise (`.000` is `0.0`, `.10` is `0.1`). `.Str` prints `Z` for zone
0 (so `+0000`, `-00`, `-00:00` all print `Z`) and `±hh:mm` otherwise, six
decimals for a fractional second (DT-23). `.raku` is
`DateTime.new(2009,12,31,22,33,44)` with `,:timezone(3600)` appended when
the zone is not 0 and the second as given (`44.5`); `.gist` is `.Str`
(so a formatter shows there, not in `.raku`); `.formatter` is the
Callable type object when none was given. A 13-digit fraction is not a
match (`X::Temporal::InvalidFormat`) and hour 24 is
`X::Temporal::OutOfRange` — there is no `24:00:00`.
```
say DateTime.new("2009-12-31T22:33:44Z").Str, " ", DateTime.new("2009-12-31T22:33:44+0000").Str, " ", DateTime.new("2009-12-31T22:33:44+1100").Str, " ", DateTime.new("2009-12-31T22:33:44").Str, " ", DateTime.new("2009-12-31T22:33:44", :timezone(12*3600+34*60)).Str, " ", DateTime.new("2012-12-22T07:02:00+12").Str, " ", DateTime.new("2012-12-22T07:02:00-12").Str, " ", DateTime.new("2012-12-22T07:02:00+12:45").Str, " ", DateTime.new("2012-12-22T07:02:00-12:45").Str, " ", DateTime.new("2012-12-22T07:02:00-00").Str, " ", DateTime.new("2012-12-22T07:02:00-00:30").Str, " ", DateTime.new("2012-12-22T07:02:00-00:30").timezone, " ", DateTime.new("2012-12-22T07:02:00+00:30").timezone, " ", DateTime.new("2015-12-11T20:41:10.5Z").Str, " ", DateTime.new("2015-12-11T20:41:10.5Z").second.raku, " ", DateTime.new("2015-12-11T20:41:10,5Z").second.raku, " ", DateTime.new("2015-12-11T20:41:10.987654321Z").Str, " ", DateTime.new("2015-12-11T20:41:10.987654321Z").second.raku, " ", DateTime.new("2015-12-11T20:41:10Z").second.raku, " ", DateTime.new("2015-08-23t02:27:33z").Str, " ", DateTime.new("2015-08-23t02:27:33-07:00").Str, " ", DateTime.new("2023-03-04").Str, " ", DateTime.new("2023-03-04", :timezone(3600)).Str, " ", DateTime.new("+9992000-01-01T00:00:00").Str, " ", DateTime.new("-4004-10-23T00:00:00").Str, " ", DateTime.new("2009-12-31T22:33:44Z", :formatter({ .hour ~ "h" })).Str, " ", DateTime.new("2009-12-31T22:33:44Z").raku, " ", DateTime.new("2009-12-31T22:33:44+01:00").raku, " ", DateTime.new("2009-12-31T22:33:44.5+01:00").raku, " ", DateTime.new("2009-12-31T22:33:44Z").gist, " ", DateTime.new("2009-12-31T22:33:44Z").timezone, " ", DateTime.new("2009-12-31T22:33:44+05:30").timezone, " ", DateTime.new("2009-12-31T22:33:44-05:30").timezone, " ", DateTime.new("2015-12-11T20:41:10.123456789012Z").second.raku, " ", (try DateTime.new("2000-01-01T00:00:00.1234567890123Z").Str) // $!.^name, " ", (try DateTime.new("2000-01-01T24:00:00Z").Str) // $!.^name, " ", DateTime.new("2012-12-22T07:02:00+99:00").Str, " ", DateTime.new("2012-12-22T07:02:00+99:00").timezone, " ", DateTime.new("2012-12-22T07:02:00.000Z").second.raku, " ", DateTime.new("2012-12-22T07:02:00.10Z").second.raku, " ", DateTime.new("2012-12-22T07:02:00+0530").timezone, " ", DateTime.new("2009-12-31T22:33:44Z").formatter.^name, " ", DateTime.new("2009-12-31T22:33:44Z", :formatter({ "F" })).raku, " ", DateTime.new("2009-12-31T22:33:44Z", :formatter({ "F" })).gist, " ", DateTime.new("2023-03-04", :formatter({ "F" })).Str, " ", DateTime.new("2023-03-04").second.raku, " ", DateTime.new("2012-12-22T07:02:00.5-00:30").raku
# rakudo 2026.08: 2009-12-31T22:33:44Z 2009-12-31T22:33:44Z 2009-12-31T22:33:44+11:00 2009-12-31T22:33:44Z 2009-12-31T22:33:44+12:34 2012-12-22T07:02:00+12:00 2012-12-22T07:02:00-12:00 2012-12-22T07:02:00+12:45 2012-12-22T07:02:00-12:45 2012-12-22T07:02:00Z 2012-12-22T07:02:00-00:30 -1800 1800 2015-12-11T20:41:10.500000Z 10.5 10.5 2015-12-11T20:41:10.987654Z 10.987654321 10 2015-08-23T02:27:33Z 2015-08-23T02:27:33-07:00 2023-03-04T00:00:00Z 2023-03-04T00:00:00+01:00 +9992000-01-01T00:00:00Z -4004-10-23T00:00:00Z 22h DateTime.new(2009,12,31,22,33,44) DateTime.new(2009,12,31,22,33,44,:timezone(3600)) DateTime.new(2009,12,31,22,33,44.5,:timezone(3600)) 2009-12-31T22:33:44Z 0 19800 -19800 10.123456789012 X::Temporal::InvalidFormat X::Temporal::OutOfRange 2012-12-22T07:02:00+99:00 356400 0.0 0.1 19800 Callable DateTime.new(2009,12,31,22,33,44) F F 0 DateTime.new(2012,12,22,7,2,0.5,:timezone(-1800))
```
rakupp 4.0.1-84: matches on every field but one — a 13-digit fraction is accepted and rounded to `.123457` where Rakudo refuses it.

### DT-19  Strings that are refused; the timezone clash                   D:partial R:yes V:bug
`X::Temporal::InvalidFormat` (`.invalid-str` the string, `.target`
`DateTime`, `.format` the fixed text `an ISO 8601 timestamp
(yyyy-mm-ddThh:mm:ssZ or yyyy-mm-ddThh:mm:ss+01:00)`) is thrown for an
offset with a trailing colon (`+00:`, `+12:`), a one-digit offset hour
(`+0`, `+7`), a space instead of `T`, missing seconds, a trailing `.`,
a space before or after `Z`, `UTC`, `+05:30:00`, the basic format
`20121222T070200Z`, `+0100Z`, `.5,5`, a synthetic codepoint, `1234`,
`2012-1-2`, a three-digit year, `T7:`, a bare `T`, and the empty string
(an IntStr `<1234>` is a POSIX epoch instead; Arabic-Indic digits are
accepted). Offset minutes of 60 or more (`+00:60`, `+00:99`) throw a
plain `X::OutOfRange` — not `X::Temporal` — with `.what` `minutes of
timezone`, `.got` the minutes as a Rat (`99`) and `.range` `0..^60`. A
string ending in `Z`/`z` or without an offset may be combined with
`:timezone` (it is applied); an explicit offset with `:timezone`, even
`+01:00` with `:timezone(0)`, throws `X::DateTime::TimezoneClash` (does
`X::Temporal`); `:timezone("3600")` — a Str — with the Str constructor
throws `X::AdHoc`. A month or day out of range in a full timestamp throws
`X::Temporal::OutOfRange` (`Month`, got Int 13); on the date-only path the
day error has `.got` as the Str `"32"`, and the bug: a bad month on the
date-only path (`2012-13-22`) throws `X::AdHoc` instead of
`X::Temporal::OutOfRange`. Do not imitate that.
```
say do { sub E(&c) { my $r = try c(); $r.defined ?? "ok:" ~ $r.Str !! $!.^name }; (E({ DateTime.new("2012-12-22T07:02:00+00:") }), E({ DateTime.new("2012-12-22T07:02:00+0") }), E({ DateTime.new("2012-12-22T07:02:00+7") }), E({ DateTime.new("2012-12-22T07:02:00+12:") }), E({ DateTime.new("2012-12-22 07:02:00") }), E({ DateTime.new("2012-12-22T07:02") }), E({ DateTime.new("2012-12-22T07:02:00.") }), E({ DateTime.new("2012-12-22T07:02:00 Z") }), E({ DateTime.new("2012-12-22T07:02:00Z ") }), E({ DateTime.new("2012-12-22T07:02:00UTC") }), E({ DateTime.new("2012-12-22T07:02:00+05:30:00") }), E({ DateTime.new("20121222T070200Z") }), E({ DateTime.new("2012-12-22T07:02:00+00:99") }), E({ DateTime.new("2012-12-22T07:02:00+00:60") }), E({ DateTime.new("2012-12-22T07:02:00Z", :timezone(3600)) }), E({ DateTime.new("2012-12-22T07:02:00+01:00", :timezone(3600)) }), E({ DateTime.new("2012-12-22T07:02:00+01:00", :timezone(0)) }), E({ DateTime.new("2012-12-22T07:02:00", :timezone("3600")) }), E({ DateTime.new("20\x[308]16-07-05T00:00:00+01:00") }), E({ DateTime.new("2016-07-05T00:0\x[308]0:00Z") }), E({ DateTime.new("1234") }), E({ DateTime.new(<1234>) }), E({ DateTime.new("2012-1-2T07:02:00Z") }), E({ DateTime.new("999-01-02T07:02:00Z") }), E({ DateTime.new("2012-12-22T7:02:00Z") }), E({ DateTime.new("2012-12-22T") }), E({ DateTime.new("٢٠١٢-١٢-٢٢T٠٧:٠٢:٠٠Z") }), E({ DateTime.new("2012-12-22T07:02:00.5") }), E({ DateTime.new("2012-12-22T07:02:00z", :timezone(0)) }), E({ DateTime.new("2012-12-22", :timezone(60)) }), E({ DateTime.new("2012-12-22T07:02:00+01") }), E({ DateTime.new("2012-12-22T07:02:00+0100Z") }), E({ DateTime.new("2012-12-22T07:02:00.5,5Z") }), E({ DateTime.new("2012-12-22T07:02:60Z") }), E({ DateTime.new("") }), E({ DateTime.new("2012-13-22T07:02:00Z") }), E({ DateTime.new("2012-13-22") }), E({ DateTime.new("2012-12-32") })).join(" ") }
# rakudo 2026.08: X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::OutOfRange X::OutOfRange ok:2012-12-22T07:02:00+01:00 X::DateTime::TimezoneClash X::DateTime::TimezoneClash X::AdHoc X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat ok:1970-01-01T00:20:34Z X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::Temporal::InvalidFormat ok:2012-12-22T07:02:00Z ok:2012-12-22T07:02:00.500000Z ok:2012-12-22T07:02:00Z ok:2012-12-22T00:00:00+00:01 ok:2012-12-22T07:02:00+01:00 X::Temporal::InvalidFormat X::Temporal::InvalidFormat X::OutOfRange X::Temporal::InvalidFormat X::Temporal::OutOfRange X::AdHoc X::Temporal::OutOfRange
say do { sub EX(&c) { my $e; { c(); CATCH { default { $e = $_ } } }; $e }; (EX({ DateTime.new("2012-12-22T07:02:00+00:99") }).^name, EX({ DateTime.new("junk") }).^name, EX({ DateTime.new("2012-12-22T07:02:00Z", :timezone(3600)) }).^name, (EX({ DateTime.new("2012-12-22T07:02:00Z", :timezone(3600)) }) ~~ X::Temporal), (EX({ DateTime.new("2012-12-22T07:02:00+00:99") }) ~~ X::Temporal), EX({ DateTime.new("2012-13-22T07:02:00Z") }).^name, EX({ DateTime.new("2012-13-22") }).^name, EX({ DateTime.new("2012-12-32") }).^name, do { my $e = EX({ DateTime.new("2012-12-22T07:02:00+00:99") }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.got.^name }, do { my $e = EX({ DateTime.new("junk") }); $e.invalid-str ~ "|" ~ $e.target ~ "|" ~ $e.format }, do { my $e = EX({ DateTime.new("2012-13-22T07:02:00Z") }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.got.^name }, do { my $e = EX({ DateTime.new("2012-12-32") }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.got.^name }).join(" ") }
# rakudo 2026.08: X::OutOfRange X::Temporal::InvalidFormat Any False False X::Temporal::OutOfRange X::AdHoc X::Temporal::OutOfRange minutes of timezone|99|0..^60|Rat junk|DateTime|an ISO 8601 timestamp (yyyy-mm-ddThh:mm:ssZ or yyyy-mm-ddThh:mm:ss+01:00) Month|13|1..12|Int Day|32|1..31|Str
```
rakupp 4.0.1-84: differs — the four colon/one-digit offset refusals, the minutes-of-timezone `X::OutOfRange` and the month/day range errors agree, but a space separator, missing seconds, a trailing `.`, spaces around `Z`, `UTC`, `+05:30:00`, `+0100Z`, `.5,5` (as .055), synthetics, `2012-1-2`, a three-digit year, `T7:`, `T` and the empty string are all accepted, an explicit offset plus `:timezone` does not clash, and `:timezone("3600")` works; `X::OutOfRange` has no `.what`, so the second line died there. On the bug it is the consistent one: `DateTime.new("2012-13-22")` throws `X::Temporal::OutOfRange`.

### DT-20  Field validation and leap-second validation                    D:partial R:yes V:quirk
Month 1..12, day 1..days-in-month, hour 0..23 and minute 0..59 throw
`X::Temporal::OutOfRange` with `.what` `Month`/`Day`/`Hour`/`Minute`,
`.got` the value and `.range` `1..12`, `1..29`, `0..23`, `0..59`, no
comment. The second is different (the quirk): it must first lie in
`0..^61` — checked on the value as given, so `-1`, `-0.5`, `61`, `62`, a
Str `"x"`, `Inf` and `NaN` are all refused — with a plain
`X::OutOfRange` (NOT `X::Temporal`) whose `.what` is `Second`, `.range`
the Str `^61` and `.comment` undefined; a second of 60 or more (60,
60.9, 60.99999) is then accepted only when the UTC time is 23:59 on a
leap-second date (DT-02), otherwise a plain `X::OutOfRange` with `.what`
`Second`, `.range` `0..^60`, `.got` the second and a defined `.comment`.
The zone is honoured: `1998-12-31T23:59:60+0200` is refused and
`1999-01-01T01:59:60+0200` accepted, `1997-06-30T21:59:60-0200` accepted.
59.5 is an ordinary second. `:hour("x")` (a coercion failure) throws
`X::AdHoc`. `clone` and the positional form validate identically.
```
say do { sub E(&c) { my $r = try c(); if $r.defined { "ok:" ~ $r.Str } else { my $e = $!; $e.^name ~ (try { ":" ~ $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range } // "") } }; (E({ DateTime.new(:1984year, :0month) }), E({ DateTime.new(:1984year, :13month) }), E({ DateTime.new(:1984year, :1month, :32day) }), E({ DateTime.new(:1984year, :2month, :30day) }), E({ DateTime.new(:1995year, :2month, :29day) }), E({ DateTime.new(:1996year, :2month, :29day) }), E({ DateTime.new(:1984year, :24hour) }), E({ DateTime.new(:1984year, :hour(-1)) }), E({ DateTime.new(:1984year, :60minute) }), E({ DateTime.new(:1984year, :second(-1)) }), E({ DateTime.new(:1984year, :second(-1/2)) }), E({ DateTime.new(:1984year, :second(59.5)) }), E({ DateTime.new(:1984year, :second(60)) }), E({ DateTime.new(:1984year, :second(61)) }), E({ DateTime.new(:1984year, :second(62)) }), E({ DateTime.new(:1984year, :second(60.9)) }), E({ DateTime.new("1999-01-01T12:10:60") }), E({ DateTime.new("1999-01-01T23:59:60") }), E({ DateTime.new("1998-12-31T23:59:60") }), E({ DateTime.new("1998-12-31T23:58:60") }), E({ DateTime.new("1997-06-30T23:59:60") }), E({ DateTime.new("1997-06-30T23:59:61") }), E({ DateTime.new("1998-12-31T23:59:60+0200") }), E({ DateTime.new("1999-01-01T01:59:60+0200") }), E({ DateTime.new("1997-06-30T21:59:60-0200") }), E({ DateTime.new(:1997year, :6month, :30day, :23hour, :59minute, :second(60.9)) }), E({ DateTime.new("2016-12-31T23:59:60Z") }), E({ DateTime.new("2017-01-01T00:59:60+01:00") }), E({ DateTime.new("2020-06-30T23:59:60Z") }), E({ DateTime.new("2012-02-29T12:34:56Z").clone(year => 2015) }), E({ DateTime.new("2000-01-01T00:00:00Z").clone(:hour(24)) }), E({ DateTime.new("2000-01-01T00:00:00Z").clone(:second(60)) }), E({ DateTime.new(2000, 1, 1, 0, 0, 60) }), E({ DateTime.new(2000, 2, 30, 0, 0, 0) }), E({ DateTime.new(:1984year, :second(60.99999)) }), E({ DateTime.new(:1984year, :minute(-1)) }), E({ DateTime.new(:1984year, :day(0)) }), E({ DateTime.new(:1984year, :day(-1)) }), E({ DateTime.new(:1984year, :second("x")) }), E({ DateTime.new(:1984year, :hour("x")) }), E({ DateTime.new(:1984year, :second(Inf)) }), E({ DateTime.new(:1984year, :second(NaN)) })).join(" ") }
# rakudo 2026.08: X::Temporal::OutOfRange:Month|0|1..12 X::Temporal::OutOfRange:Month|13|1..12 X::Temporal::OutOfRange:Day|32|1..31 X::Temporal::OutOfRange:Day|30|1..29 X::Temporal::OutOfRange:Day|29|1..28 ok:1996-02-29T00:00:00Z X::Temporal::OutOfRange:Hour|24|0..23 X::Temporal::OutOfRange:Hour|-1|0..23 X::Temporal::OutOfRange:Minute|60|0..59 X::OutOfRange:Second|-1|^61 X::OutOfRange:Second|-0.5|^61 ok:1984-01-01T00:00:59.500000Z X::OutOfRange:Second|60|0..^60 X::OutOfRange:Second|61|^61 X::OutOfRange:Second|62|^61 X::OutOfRange:Second|60.9|0..^60 X::OutOfRange:Second|60|0..^60 X::OutOfRange:Second|60|0..^60 ok:1998-12-31T23:59:60Z X::OutOfRange:Second|60|0..^60 ok:1997-06-30T23:59:60Z X::OutOfRange:Second|61|^61 X::OutOfRange:Second|60|0..^60 ok:1999-01-01T01:59:60+02:00 ok:1997-06-30T21:59:60-02:00 ok:1997-06-30T23:59:60.900000Z ok:2016-12-31T23:59:60Z ok:2017-01-01T00:59:60+01:00 X::OutOfRange:Second|60|0..^60 X::Temporal::OutOfRange:Day|29|1..28 X::Temporal::OutOfRange:Hour|24|0..23 X::OutOfRange:Second|60|0..^60 X::OutOfRange:Second|60|0..^60 X::Temporal::OutOfRange:Day|30|1..29 X::OutOfRange:Second|60.99999|0..^60 X::Temporal::OutOfRange:Minute|-1|0..59 X::Temporal::OutOfRange:Day|0|1..31 X::Temporal::OutOfRange:Day|-1|1..31 X::OutOfRange:Second|"x"|^61 X::AdHoc X::OutOfRange:Second|Inf|^61 X::OutOfRange:Second|NaN|^61
say do { sub EX(&c) { my $e; { c(); CATCH { default { $e = $_ } } }; $e }; (EX({ DateTime.new("1999-01-01T23:59:60") }).^name, EX({ DateTime.new(:1984year, :13month) }).^name, EX({ DateTime.new(:1984year, :second(62)) }).^name, EX({ DateTime.new(:1984year, :24hour) }).^name, EX({ DateTime.new("1999-01-01T12:10:60") }).^name, EX({ DateTime.new(:1984year, :second(-1/2)) }).^name, EX({ DateTime.new("1998-12-31T23:59:60+0200") }).^name, (EX({ DateTime.new("1999-01-01T23:59:60") }) ~~ X::Temporal), (EX({ DateTime.new(:1984year, :13month) }) ~~ X::Temporal), (EX({ DateTime.new(:1984year, :second(62)) }) ~~ X::Temporal), (EX({ DateTime.new(:1984year, :24hour) }) ~~ X::Temporal), do { my $e = EX({ DateTime.new("1999-01-01T23:59:60") }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.got.^name ~ "|" ~ $e.comment.defined }, do { my $e = EX({ DateTime.new(:1984year, :13month) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.comment.defined }, do { my $e = EX({ DateTime.new(:1984year, :second(62)) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.comment.defined ~ "|" ~ $e.got.^name }, do { my $e = EX({ DateTime.new(:1984year, :24hour) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ DateTime.new(:1984year, :60minute) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ DateTime.new("1999-01-01T12:10:60") }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range ~ "|" ~ $e.comment.defined }, do { my $e = EX({ DateTime.new(:1984year, :second(-1/2)) }); $e.what ~ "|" ~ $e.got.raku ~ "|" ~ $e.range }, do { my $e = EX({ DateTime.new("1998-12-31T23:59:60+0200") }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }, do { my $e = EX({ DateTime.new(:1984year, :2month, :30day) }); $e.what ~ "|" ~ $e.got ~ "|" ~ $e.range }).join(" ") }
# rakudo 2026.08: X::OutOfRange X::Temporal::OutOfRange X::OutOfRange X::Temporal::OutOfRange X::OutOfRange X::OutOfRange X::OutOfRange False True False True Second|60|0..^60|Int|True Month|13|1..12|False Second|62|^61|False|Str Hour|24|0..23 Minute|60|0..59 Second|60|0..^60|True Second|"-0.5"|^61 Second|60|0..^60 Day|30|1..29
```
rakupp 4.0.1-84: differs — month/day/hour/minute and the leap-second rule agree (including the zone cases), but the `^61` check has no attributes (`X::OutOfRange:||`), `:second("x")` and `:hour("x")` are accepted as 0, NaN is accepted and prints `nanZ`; `.comment` is missing, so the second line died there, and the leap-second `.got` is a Str where Rakudo has an Int.

### DT-21  Epoch, Instant, positional and named constructors             D:yes R:yes V:quirk
`DateTime.new(Numeric)` is a POSIX epoch: leap seconds are not modelled,
so 915148800 is 1999-01-01T00:00:00Z and 1483228800 is 2017-01-01;
negatives and huge values work (127317232781632218937129 is
+4034522497029953-07-13T17:38:49Z); the fraction becomes the second's
fraction with the input's type — a Rat epoch gives a Rat second, a Num
epoch a Num second (`1e0`), an allomorph `<12.5>` counts as Numeric; the
`:timezone` (an Int — a Str or Rat throws `X::AdHoc` here) shifts the
wall clock and labels it, `.posix` unchanged; any other named throws
`X::AdHoc`, as does a named `:year` beside a positional; two positionals
`X::Multi::NoMatch`. `DateTime.new(Instant)` is exact: the leap second
and a Rat fraction survive, `:timezone` converts. The six-positional
form `($y, $mo, $d, $h, $mi, $s, :timezone, :formatter)` coerces year,
month, hour and minute with `Int()` (1.9 is 1), the day as in DT-07, and
keeps the second exactly as given — a Str `"5"` stays a Str (the
quirk). The named form needs `:year!` (`:year<2016>` fine), defaults
month and day to 1 and the time to 0, and here `:timezone("3600")` IS
coerced. `.raku` interpolates the second (`1e0` prints as `1`, `0.5` as
`0.5`), and a `.000001` second prints `00.000001`. `DateTime.new` with
no arguments returns a Failure. `DateTime(Instant)` and `DateTime(Str)`
coerce.
```
say DateTime.new(0).Str, " ", DateTime.new(946684799).Str, " ", DateTime.new(946684800).Str, " ", DateTime.new(0).day-of-week, " ", DateTime.new(-1).Str, " ", DateTime.new(-86401).Str, " ", DateTime.new(915148800).Str, " ", DateTime.new(425865600).Str, " ", DateTime.new(1470853583.3).Str, " ", DateTime.new(0.5).second.raku, " ", DateTime.new(1e0).second.raku, " ", DateTime.new(1e0).Str, " ", DateTime.new(1e0).raku, " ", DateTime.new(1/3).second.raku, " ", DateTime.new(0, :timezone(3600)).Str, " ", DateTime.new(0, :timezone(-3600)).Str, " ", DateTime.new(0, :timezone(3600)).posix, " ", DateTime.new(946684799, :timezone(-(5*3600+55*60)), :formatter({ .day ~ "/" ~ .month ~ "/" ~ .year ~ " " ~ .second ~ "s" ~ .minute ~ "m" ~ .hour ~ "h" })).Str, " ", DateTime.new(127317232781632218937129).Str, " ", DateTime.new(<1234>).Str, " ", DateTime.new(1234.5e0).Str, " ", DateTime.new(2**40).Str, " ", DateTime.new(-2**40).Str, " ", DateTime.new(0).timezone, " ", DateTime.new(0).raku, " ", (try DateTime.new(:2016year, 42)) // $!.^name, " ", (try DateTime.new(0, :foo)) // $!.^name, " ", DateTime.new(0, :formatter({"F"})).Str, " ", DateTime.new(Instant.from-posix(0)).Str, " ", DateTime.new(Instant.from-posix(1.25)).Str, " ", DateTime.new(Instant.from-posix(1.25)).second.raku, " ", DateTime.new(Instant.from-posix(0), :timezone(3600)).Str, " ", DateTime.new(Instant.from-posix(0), :timezone(3600)).posix, " ", DateTime.new(Instant.from-posix(1483228800, True)).Str, " ", DateTime.new(Instant.from-posix(1483228800, True) + 0.5).Str, " ", DateTime.new(Instant.from-posix(1483228800)).Str, " ", DateTime.new(Instant.from-posix(0), :formatter({"F"})).Str, " ", DateTime(Instant.from-posix(0)).Str, " ", DateTime("2000-01-01T00:00:00Z").Str, " ", DateTime.new(2020, 1, 2, 3, 4, 5).Str, " ", DateTime.new(2020, 1, 2, 3, 4, 5, :timezone(3600)).Str, " ", DateTime.new("2020", "1", "2", "3", "4", "5").Str, " ", DateTime.new(2020, 1, 2, 3, 4, "5").second.^name, " ", DateTime.new(2020, 1, 2, 3, 4, 5.5).second.raku, " ", DateTime.new(2020, 1, *, 3, 4, 5).Str, " ", DateTime.new(2020, 2, *-1, 3, 4, 5).Str, " ", DateTime.new(:2020year, :day(*)).Str, " ", DateTime.new(:year<2016>).Str, " ", DateTime.new(2020,3,10,11,38,.000001).Str, " ", DateTime.new(2020, 1, 2, 3, 4, 5, :formatter({"F"})).Str, " ", DateTime.new(2020, 1.9, 2.9, 3.9, 4.9, 5).Str, " ", DateTime.new(:2020year, :month(1.9)).Str, " ", (try DateTime.new(0, :timezone("3600")).Str) // $!.^name, " ", (try DateTime.new(0, :timezone(3600.7)).Str) // $!.^name, " ", (try DateTime.new(0, 1)) // $!.^name, " ", DateTime.new(1234.5).second.raku, " ", DateTime.new(-0.5).Str, " ", DateTime.new(<12.5>).Str, " ", DateTime.new(1e0).second.^name, " ", DateTime.new(0).second.^name, " ", DateTime.new(0.5).second.^name, " ", DateTime.new(:2020year, :timezone("3600")).Str, " ", DateTime.new.^name
# rakudo 2026.08: 1970-01-01T00:00:00Z 1999-12-31T23:59:59Z 2000-01-01T00:00:00Z 4 1969-12-31T23:59:59Z 1969-12-30T23:59:59Z 1999-01-01T00:00:00Z 1983-07-01T00:00:00Z 2016-08-10T18:26:23.300000Z 0.5 1e0 1970-01-01T00:00:01Z DateTime.new(1970,1,1,0,0,1) <1/3> 1970-01-01T01:00:00+01:00 1969-12-31T23:00:00-01:00 0 31/12/1999 59s4m18h +4034522497029953-07-13T17:38:49Z 1970-01-01T00:20:34Z 1970-01-01T00:20:34.500000Z +36812-02-20T00:36:16Z -32873-11-12T23:23:44Z 0 DateTime.new(1970,1,1,0,0,0) X::AdHoc X::AdHoc F 1970-01-01T00:00:00Z 1970-01-01T00:00:01.250000Z 1.25 1970-01-01T01:00:00+01:00 0 2016-12-31T23:59:60Z 2016-12-31T23:59:60.500000Z 2017-01-01T00:00:00Z F 1970-01-01T00:00:00Z 2000-01-01T00:00:00Z 2020-01-02T03:04:05Z 2020-01-02T03:04:05+01:00 2020-01-02T03:04:05Z Str 5.5 2020-01-31T03:04:05Z 2020-02-28T03:04:05Z 2020-01-31T00:00:00Z 2016-01-01T00:00:00Z 2020-03-10T11:38:00.000001Z F 2020-01-02T03:04:05Z 2020-01-01T00:00:00Z X::AdHoc X::AdHoc X::Multi::NoMatch 34.5 1970-01-01T00:00:59.500000Z 1970-01-01T00:00:12.500000Z Num Int Rat 2020-01-01T00:00:00+01:00 Failure
```
rakupp 4.0.1-84: differs — a Rat epoch gives a Num second, a Num epoch an Int second, the huge epoch overflows (`+292277026596-12-04T15:30:9223372036854775807Z`), `:foo` is accepted, `:2016year` beside a positional throws `X::Temporal`, the Instant leap-second cases lose the `:60`, `*-1` is 02-29, a Str/Rat `:timezone` is accepted, `DateTime.new(0, 1)` gives 0000-01-01, and `DateTime.new` with no arguments throws, so the line died at its last field.

## E. DateTime accessors, printing, zones, comparison

### DT-22  Accessors, posix, Instant, day-fraction, julian dates          D:yes R:yes V:spec
`.hour`, `.minute` Ints; `.second` as constructed (Rat 45.5);
`.whole-second` its Int floor; `.timezone` = `.offset` in seconds;
`.offset-in-minutes` and `.offset-in-hours` Rats (120.0, 2.0; `+04:05` is
49/12); `.hh-mm-ss` whole seconds. `.posix` is the UTC POSIX Int (the
fraction dropped), `.posix(:real)` keeps the fraction, `.posix(True)`
adds the offset back (the wall clock read as UTC). `.Instant` (also
`.Numeric`, `.Real`, prefix `+`) keeps the fraction; `.Int` throws
`X::Multi::NoMatch`. `.Date` is the local calendar date, `.DateTime` the
invocant, `.daycount`, `.day-of-week`, `.day-of-year`, `.week`,
`.days-in-month`, `.yyyy-mm-dd` etc. follow the LOCAL date; the
separator argument works. `.day-fraction` is `(h·3600 + m·60 + s) /
86400` of the local time as a Rat (86401 on a leap-second day);
`.modified-julian-date` and `.julian-date` use the UTC time
(`julian-date` = MJD + 2400000.5); `.IO` is an IO::Path.
```
say do { my $dt = DateTime.new("2015-12-24T12:23:45.5+02:00"); ($dt.year, $dt.month, $dt.day, $dt.hour, $dt.minute, $dt.second.raku, $dt.whole-second, $dt.timezone, $dt.offset, $dt.offset-in-minutes.raku, $dt.offset-in-hours.raku, $dt.hh-mm-ss, $dt.posix, $dt.posix(:real).raku, $dt.posix(True), $dt.posix(True, :real).raku, $dt.Instant.raku, $dt.Instant.^name, $dt.Numeric.^name, $dt.Real.^name, (+$dt).^name, $dt.Date.Str, $dt.DateTime.Str, ($dt.DateTime === $dt), $dt.daycount, $dt.day-of-week, $dt.day-of-year, $dt.week.raku, $dt.is-leap-year, $dt.days-in-month, $dt.yyyy-mm-dd, $dt.yyyy-mm-dd("/"), $dt.dd-mm-yyyy, $dt.day-fraction.raku, $dt.modified-julian-date.raku, $dt.julian-date.raku, $dt.formatter.^name, $dt.IO.^name, $dt.Str, $dt.gist, $dt.raku, $dt.utc.Str, $dt.utc.daycount, $dt.hour.^name, DateTime.new("2016-10-03T20:20:20+04:05").offset-in-hours.raku, DateTime.new("2016-10-03T20:20:20-04:30").offset-in-minutes.raku, DateTime.new(:1970year, :second(1.2345)).posix(:real).raku, DateTime.new(:1970year, :second(1.2345)).posix, DateTime.new(:1970year, :1hour, :1minute, :1second, :timezone(-3660)).posix, DateTime.new(:1970year, :1hour, :1minute, :1second, :timezone(-3660)).posix(True), DateTime.new("2021-12-24T12:23:00.43Z").day-fraction.raku, DateTime.new("2021-12-24T12:23:00.43Z").julian-date.raku, DateTime.new(2016,12,30,12,0,0).day-fraction.raku, DateTime.new(2016,12,31,12,0,0).day-fraction.raku, DateTime.new("2021-12-24T12:23:00+12:00").julian-date.raku, DateTime.new("2015-12-24T12:23:00Z").Instant.Str, DateTime.new("2015-12-24T12:23:00Z").posix, DateTime.new("2015-12-24T12:23:00Z").Instant.to-posix.raku, $dt.posix.^name, $dt.whole-second.^name, $dt.posix(:real).^name, DateTime.new("2015-12-24T12:23:00Z").posix(:real).^name, DateTime.new("2015-12-24T12:23:00Z").day-fraction.raku, DateTime.new("2015-12-24T00:00:00+02:00").julian-date.raku, DateTime.new("2015-12-24T00:00:00+02:00").modified-julian-date.raku, DateTime.new("2015-12-24T00:00:00+02:00").daycount, $dt.days-in-year, $dt.week-number, $dt.weekday-of-month, $dt.mm-dd-yyyy, $dt.day-of-month, $dt.second.^name, DateTime.new("2015-12-24T12:23:45+02:00").second.^name, (try $dt.Int) // $!.^name, $dt.Date.^name, $dt.yyyy-mm, $dt.mm-dd).join(" ") }
# rakudo 2026.08: 2015 12 24 12 23 45.5 45 7200 7200 120.0 2.0 12:23:45 1450952625 1450952625.5 1450959825 1450959825.5 Instant.from-posix(1450952625.5) Instant Instant Instant Instant 2015-12-24 2015-12-24T12:23:45.500000+02:00 True 57380 4 358 (2015, 52) False 31 2015-12-24 2015/12/24 24-12-2015 <89251/172800> <9915338851/172800> <424635425251/172800> Callable IO::Path 2015-12-24T12:23:45.500000+02:00 2015-12-24T12:23:45.500000+02:00 DateTime.new(2015,12,24,12,23,45.5,:timezone(7200)) 2015-12-24T10:23:45.500000Z 57380 Int <49/12> -270.0 1.2345 1 7321 3661 <4458043/8640000> <21250710858043/8640000> 0.5 <43200/86401> <3541784423/1440> Instant:1450959816 1450959780 (1450959780.0, Bool::False) Int Int Rat Int <743/1440> <29488565/12> <688559/12> 57380 365 52 4 12-24-2015 24 Rat Int X::Multi::NoMatch Date 2015-12 12-24
```
rakupp 4.0.1-84: differs — `.posix(True)` ignores its flag, `.Instant.raku` is a Num, `yyyy-mm-dd("/")` ignores the separator, `.modified-julian-date`/`.julian-date` use the local time instead of UTC, `.Instant.Str` is a bare number, `.Int` answers the posix instead of throwing, and `yyyy-mm` is missing, so the line died there.

### DT-23  The default Str: offsets, fractional seconds, WHICH             D:partial R:partial V:bug
The default formatter prints `YYYY-MM-DDThh:mm:ss`, then `.ffffff` only
when the second is not whole, then `Z` when the zone is 0 or the sign
and `hh:mm` of the zone's absolute value — its seconds are dropped and a
non-zero zone never prints `Z`: 3661 is `+01:01`, 1 is `+00:00`, −30 is
`-00:00`, 360000 is `+100:00`. The six decimals are the second rounded
half-up (0.0000005 is `.000001`, 0.0000004 `.000000`, 0.1234565
`.123457`, 10.9999996 `11.000000`, 59.9999994 `59.999999`); a Num second
prints like a Rat (`05`, `05.250000`); a leap second prints `23:59:60Z`
or `23:59:60.500000Z`. The bugs: rounding may overflow into a second
that does not exist (59.9999999 prints `00:00:60.000000`); a second in
[0.9999995, 1) makes `.Str` throw `X::AdHoc`; a Num second below 5e-7
prints seven decimals (`00.0000000`). `.raku` interpolates the second
and adds `,:timezone(-59)` when non-zero. `WHICH` is `DateTime|` plus
the `.Str` — a ValueObjAt — so `===` holds for equal fields in the same
zone and fails across zones; and, the third bug, it is the FORMATTED
string: two DateTimes with the same constant formatter are `===` and
`.unique` collapses them. The formatter receives the DateTime (a block,
a pointy block or a sub); a non-Callable formatter throws `X::AdHoc` at
`.Str`; a formatter that dies propagates.
```
say DateTime.new(:2000year, :timezone(3661)).Str, " ", DateTime.new(:2000year, :timezone(-30)).Str, " ", DateTime.new(:2000year, :timezone(-30)).raku, " ", DateTime.new(:2000year, :timezone(1)).Str, " ", DateTime.new(:2000year, :timezone(-3600)).Str, " ", DateTime.new(:2000year, :timezone(-1800)).Str, " ", DateTime.new(:2000year, :timezone(360000)).Str, " ", DateTime.new(:2000year, :second(0.5)).Str, " ", DateTime.new(:2000year, :second(0.0000005)).Str, " ", DateTime.new(:2000year, :second(0.0000004)).Str, " ", DateTime.new(:2000year, :second(0.000001)).Str, " ", DateTime.new(:2000year, :second(1.0000005)).Str, " ", DateTime.new(:2000year, :second(59.9999999)).Str, " ", (try DateTime.new(:2000year, :second(0.9999999)).Str) // $!.^name, " ", DateTime.new(:2000year, :second(9.5)).Str, " ", DateTime.new(:2000year, :second(1/3)).Str, " ", DateTime.new(:2000year, :second(5e0)).Str, " ", DateTime.new(:2000year, :second(5.25e0)).Str, " ", DateTime.new(:2000year, :second(<5.5>)).Str, " ", DateTime.new(:2016year, :12month, :31day, :23hour, :59minute, :second(60)).Str, " ", DateTime.new(:2016year, :12month, :31day, :23hour, :59minute, :second(60.5)).Str, " ", DateTime.new(:2000year, :second(0.5)).raku, " ", DateTime.new(:2000year, :second(5e0)).raku, " ", DateTime.new(:2000year, :formatter({ "F" })).Str, " ", DateTime.new(:2000year, :formatter({ "F" })).gist, " ", DateTime.new(:2000year, :formatter({ "F" })).raku, " ", DateTime.new(:2000year).WHICH, " ", DateTime.new(:2000year).WHICH.^name, " ", DateTime.new(:2000year, :timezone(3600)).WHICH, " ", DateTime.new(:2000year, :formatter({ "F" })).WHICH, " ", (DateTime.new(:2000year, :formatter({ "F" })) === DateTime.new(:2001year, :formatter({ "F" }))), " ", (DateTime.new(:2000year) === DateTime.new(:2000year)), " ", (DateTime.new(:2000year) === DateTime.new(:2000year, :timezone(3600))), " ", (DateTime.new(:2000year, :timezone(3600)) === DateTime.new(:2000year, :timezone(3600))), " ", ((DateTime.new(:2000year, :formatter({ "F" })), DateTime.new(:2001year, :formatter({ "F" }))).unique.elems), " ", DateTime.new(:2000year, :second(10.987654321)).Str, " ", DateTime.new(:2000year, :second(10.9999996)).Str, " ", (try DateTime.new(:2000year, :second(0.99999951)).Str) // $!.^name, " ", DateTime.new(:2000year, :second(59.5)).Str, " ", DateTime.new(:2000year, :second(1e-7)).Str, " ", DateTime.new(:2000year, :second(0.1234565)).Str, " ", DateTime.new(:2000year, :timezone(-3661)).Str, " ", DateTime.new(:2000year, :timezone(59)).Str, " ", DateTime.new(:2000year, :timezone(-59)).raku, " ", DateTime.new(:2000year, :formatter(-> $dt { $dt.year ~ "!" })).Str, " ", DateTime.new(:2000year, :formatter(sub ($x) { "S" })).Str, " ", (try DateTime.new(:2000year, :formatter(42)).Str) // $!.^name, " ", (try DateTime.new(:2000year, :formatter({ die "boom" })).Str) // $!.^name, " ", DateTime.new(:2000year, :second(0.9999994)).Str, " ", DateTime.new(:2000year, :second(59.9999994)).Str, " ", (try DateTime.new(:2000year, :second(0.9999996e0)).Str) // $!.^name
# rakudo 2026.08: 2000-01-01T00:00:00+01:01 2000-01-01T00:00:00-00:00 DateTime.new(2000,1,1,0,0,0,:timezone(-30)) 2000-01-01T00:00:00+00:00 2000-01-01T00:00:00-01:00 2000-01-01T00:00:00-00:30 2000-01-01T00:00:00+100:00 2000-01-01T00:00:00.500000Z 2000-01-01T00:00:00.000001Z 2000-01-01T00:00:00.000000Z 2000-01-01T00:00:00.000001Z 2000-01-01T00:00:01.000001Z 2000-01-01T00:00:60.000000Z X::AdHoc 2000-01-01T00:00:09.500000Z 2000-01-01T00:00:00.333333Z 2000-01-01T00:00:05Z 2000-01-01T00:00:05.250000Z 2000-01-01T00:00:05.500000Z 2016-12-31T23:59:60Z 2016-12-31T23:59:60.500000Z DateTime.new(2000,1,1,0,0,0.5) DateTime.new(2000,1,1,0,0,5) F F DateTime.new(2000,1,1,0,0,0) DateTime|2000-01-01T00:00:00Z ValueObjAt DateTime|2000-01-01T00:00:00+01:00 DateTime|F True True False True 1 2000-01-01T00:00:10.987654Z 2000-01-01T00:00:11.000000Z X::AdHoc 2000-01-01T00:00:59.500000Z 2000-01-01T00:00:00.000000Z 2000-01-01T00:00:00.123457Z 2000-01-01T00:00:00-01:01 2000-01-01T00:00:00+00:00 DateTime.new(2000,1,1,0,0,0,:timezone(-59)) 2000! S X::AdHoc X::AdHoc 2000-01-01T00:00:00.999999Z 2000-01-01T00:00:59.999999Z X::AdHoc
```
rakupp 4.0.1-84: differs — it rounds half-down (0.0000005 prints `.000000`, 0.1234565 `.123456`), prints `01.000000` where Rakudo throws, prints the default for a non-Callable formatter, and reproduces the `00:00:60.000000` overflow and the formatter-based `WHICH`; the offsets and the leap seconds agree.

### DT-24  in-timezone, utc, local, $*TZ                                   D:yes R:yes V:spec
`.in-timezone($seconds)` re-expresses the same instant with the new
offset: the fields shift (across days, months and years, by any offset,
even −529402 or −147:03), `==` and `.posix` hold, `.timezone` is the new
value, fractional seconds survive, the formatter is kept; the argument is
`Int(Cool)` (`"7200"` and 7200.9 both give 7200), a non-numeric Str throws
`X::Str::Numeric`, no argument `X::AdHoc`; the same offset returns the
invocant. A leap second keeps its `:60` and only the wall clock moves
(23:59:60Z in +01:00 is 2017-01-01T00:59:60+01:00, in −01:00
22:59:60-01:00, in −00:30 23:29:60-00:30, in −86400 2016-12-30T23:59:60-24:00),
and `.utc` of 00:59:60+01:00 is 23:59:60Z. `.utc` is `in-timezone(0)`,
`.local` is `in-timezone($*TZ)`. `$*TZ` is the process's zone offset in
seconds east of UTC (an Int; 7200 on this machine); it can be assigned
or shadowed with `my $*TZ`, and `.local` follows the current value.
`.daycount` follows the local date after the move. The `$*TZ` fields of
the probe are machine-dependent and print Booleans.
```
say do { sub tz($s) { DateTime.new("2005-02-04T15:25:00$s") }; (tz("+0200").in-timezone(4*3600).Str, tz("+0000").in-timezone(-3600).Str, tz("-0100").in-timezone(0).Str, tz("-0100").utc.Str, tz("+0000").in-timezone(-13*60).Str, tz("+0000").in-timezone(-5).hh-mm-ss, tz("+0000").in-timezone(-5).Str, tz("+0000").in-timezone(44*60).Str, tz("+0000").in-timezone(-18*3600-55*60).Str, tz("-1611").in-timezone(16*3600+55*60).Str, DateTime.new("2005-01-01T02:22:00+0300").in-timezone(35*60).Str, DateTime.new(:1984year, :second(15.5)).in-timezone(5).second.raku, DateTime.new(:2005year, :1month, :3day, :2hour, :22minute, :4second, :timezone(13)).in-timezone(-529402).Str, tz("Z").in-timezone("7200").Str, tz("Z").in-timezone(7200.9).Str, (tz("+0200").in-timezone(7200) === tz("+0200")), tz("+0200").in-timezone(7200).Str, DateTime.new("2016-12-31T23:59:60Z").in-timezone(3600).Str, DateTime.new("2016-12-31T23:59:60Z").in-timezone(-3600).Str, DateTime.new("2016-12-31T23:59:60Z").in-timezone(-1800).Str, DateTime.new("2017-01-01T00:59:60+01:00").utc.Str, (DateTime.new("2016-12-31T23:59:60Z").in-timezone(1800) == DateTime.new("2016-12-31T23:59:60Z")), tz("+0200").utc.timezone, (tz("+0200").utc.posix == tz("+0200").posix), tz("+0200").in-timezone(3600).formatter.^name, DateTime.new("2005-02-04T15:25:00Z", :formatter({ "F" })).in-timezone(3600).formatter.^name, DateTime.new("2005-02-04T15:25:00Z", :formatter({ "F" })).in-timezone(3600).Str, tz("Z").in-timezone(3600).raku, do { my $*TZ = -3600; DateTime.new("2015-12-24T12:23:00+0200").local.Str }, do { my $*TZ = 0; DateTime.new("2015-12-24T12:23:00+0200").local.timezone }, ($*TZ.^name), (DateTime.now.timezone == $*TZ), (DateTime.new(:1995year).local.timezone == $*TZ), (DateTime.new("2003-08-01T02:22:00Z").local.utc.Str), (DateTime.new("1998-12-31T23:59:60Z").local.utc.Str), (DateTime.new("2003-08-01T02:22:00Z").local == DateTime.new("2003-08-01T02:22:00Z")), tz("Z").in-timezone(0).Str, (tz("Z").in-timezone(0) === tz("Z")), tz("Z").utc.^name, (try tz("Z").in-timezone("x")) // $!.^name, (try tz("Z").in-timezone()) // $!.^name, tz("Z").in-timezone(-3600).daycount, tz("Z").in-timezone(-16*3600).daycount, DateTime.new("2016-12-31T23:59:60.5Z").in-timezone(3600).Str, DateTime.new("2016-12-31T23:59:60Z").in-timezone(-86400).Str, DateTime.new(:1984year, :second(15.5)).in-timezone(-20).Str, do { my $*TZ = 7200; DateTime.new("2015-12-24T12:23:00Z").local.Str }, (DateTime.new("2015-12-24T12:23:00Z").in-timezone(7200) eqv DateTime.new("2015-12-24T14:23:00+02:00"))).join(" ") }
# rakudo 2026.08: 2005-02-04T17:25:00+04:00 2005-02-04T14:25:00-01:00 2005-02-04T16:25:00Z 2005-02-04T16:25:00Z 2005-02-04T15:12:00-00:13 15:24:55 2005-02-04T15:24:55-00:00 2005-02-04T16:09:00+00:44 2005-02-03T20:30:00-18:55 2005-02-06T00:31:00+16:55 2004-12-31T23:57:00+00:35 20.5 2004-12-27T23:18:29-147:03 2005-02-04T17:25:00+02:00 2005-02-04T17:25:00+02:00 True 2005-02-04T15:25:00+02:00 2017-01-01T00:59:60+01:00 2016-12-31T22:59:60-01:00 2016-12-31T23:29:60-00:30 2016-12-31T23:59:60Z True 0 True Callable Block F DateTime.new(2005,2,4,16,25,0,:timezone(3600)) 2015-12-24T09:23:00-01:00 0 Int True True 2003-08-01T02:22:00Z 1998-12-31T23:59:60Z True 2005-02-04T15:25:00Z True DateTime X::Str::Numeric X::AdHoc 53405 53404 2017-01-01T00:59:60.500000+01:00 2016-12-30T23:59:60-24:00 1983-12-31T23:59:55.500000-00:00 2015-12-24T14:23:00+02:00 True
```
rakupp 4.0.1-84: differs — `in-timezone(5).second` is a Num, and a non-numeric Str or no argument returns the DateTime unchanged instead of throwing; every conversion, including the leap seconds and `$*TZ`, agrees.

### DT-25  DateTime.now and Date.today                                    D:yes R:yes V:quirk
`DateTime.now(:timezone = $*TZ, :formatter)` is the current time in that
zone: `.timezone` is `$*TZ` by default, `.utc.timezone` 0, `.year` an
Int, `.second` a Num (the clock's fraction), `.formatter` the Callable
type object unless given; it agrees with `now` and `time` to the second.
`Date.today(:formatter)` is the LOCAL calendar date (the system clock in
`$*TZ`) and equals `DateTime.now.Date`. The quirk: `now.DateTime` and
`DateTime.new(now)` are UTC with a Rat `.second`, so the two roads to
"now" differ in zone and in the second's type, and `now.Date` (the UTC
date) can be one day off `Date.today` near local midnight when `$*TZ` is
not 0. Every field is clock-dependent and prints a Boolean or type.
```
say DateTime.now.^name, " ", (DateTime.now.timezone == $*TZ), " ", DateTime.now(:timezone(0)).timezone, " ", DateTime.now(:timezone(7200)).timezone, " ", DateTime.now.utc.timezone, " ", ((DateTime.now.Instant - now).abs < 5), " ", ((DateTime.now(:timezone(0)).posix - time).abs <= 1), " ", DateTime.now.year.^name, " ", DateTime.now.second.^name, " ", DateTime.now(:formatter({ "F" })).Str, " ", (DateTime.now.Date == Date.today || DateTime.now.hour == 0), " ", (Date.today.^name), " ", (Date.today(:formatter({"F"})).Str), " ", (Date.today.year >= 2026), " ", (DateTime.now < DateTime.now.later(:1second)), " ", DateTime.now.formatter.^name, " ", ($*TZ ~~ Int), " ", (DateTime.now(:timezone(0)).Str.ends-with("Z")), " ", (DateTime.now(:timezone(3600)).Str.ends-with("+01:00")), " ", (DateTime.now.Str.chars >= 20), " ", (DateTime.now - DateTime.now).^name, " ", now.DateTime.timezone, " ", now.DateTime.second.^name, " ", (DateTime.now(:timezone(0)).hour == DateTime.new(time).hour || DateTime.now(:timezone(0)).minute == 0), " ", (DateTime.now(:timezone(0), :formatter({ ~.hour })).Str == DateTime.new(time).hour || DateTime.new(time).minute == 59), " ", (Date.today == Date.today.clone), " ", Date.today.formatter.^name, " ", ((DateTime.now.posix - time).abs <= 1), " ", (DateTime.now.timezone == DateTime.now.utc.in-timezone($*TZ).timezone), " ", ((DateTime.now.Instant - $*INIT-INSTANT) < 60), " ", ((Date.today.daycount - Date.new(DateTime.now(:timezone(0))).daycount).abs <= 1), " ", ((Date.today - Date.new(now)).abs <= 1), " ", ((now.Date - Date.today).abs <= 1)
# rakudo 2026.08: DateTime True 0 7200 0 True True Int Num F True Date F True True Callable True True True True Duration 0 Rat True True True Callable True True True True True True
```
rakupp 4.0.1-84: differs — `now.DateTime.second` is a Num and `now.Date` is missing (the line died at its last field); everything else agrees.

### DT-26  Comparing DateTimes: by instant, except `cmp`                  D:partial R:yes V:bug
`==`, `!=`, `<`, `<=`, `>`, `>=`, `<=>`, `before`, `after` and `~~`
(DateTime.ACCEPTS is `==`) compare the instants, so the zone does not
matter: `10:45:00Z == 12:45:00+02:00`, `<=>` of `+48:22` against `-47:38`
is Same, and around a leap second 23:59:59Z < 23:59:60Z < 00:00:00Z with
the `:60` equal across zones. `eqv` is "same type and `==`" (zone and
formatter ignored; a subclass is not `eqv`, `==` still holds); `===` is
by `WHICH`, i.e. by Str (DT-23). The bug: `cmp` and `leg` — and so
`sort`, `min`, `max` — compare the `.Str`, contradicting the docs ("the
equivalent instant"): the same instant in two zones is Less/More, and
with a constant formatter every DateTime is Same; `.unique` keeps both
zones. Against other types: `== Instant` True, `== posix` False, `==
Date` False, `cmp Instant` a string compare; `DateTime ~~ Date` compares
the stored calendar date (DT-12) and `Date ~~ DateTime` is False; `~~
Str` is string equality, `eqv Str` False; `eq`/`lt` on the Str;
`.in-timezone(7200)` of the UTC value is `===` and `eqv` the `+02:00`
literal; `sort(* <=> *)` is stable.
```
say do { my $a = DateTime.new("1971-10-28T10:45:00Z"); my $b = DateTime.new("1998-10-19T02:03:00Z"); my $c = DateTime.new("1971-10-28T12:45:00+02:00"); ($a == $a, $a == $c, $a != $b, $a < $b, $a <= $c, $a > $b, $a >= $c, ($a cmp $b).raku, ($b cmp $a).raku, ($a cmp $c).raku, ($c cmp $a).raku, ($a <=> $c).raku, ($a <=> $b).raku, ($a before $b), ($b after $a), ($a === $c), ($a === DateTime.new("1971-10-28T10:45:00Z")), ($a eqv $c), ($a eqv DateTime.new("1971-10-28T10:45:00Z")), ($a eqv $b), ($a ~~ $c), ($a ~~ $b), ($c ~~ $a), ($a ~~ Date.new("1971-10-28")), (Date.new("1971-10-28") ~~ $a), ($c ~~ Date.new("1971-10-28")), (DateTime.new("1971-10-28T23:30:00-05:00") ~~ Date.new("1971-10-28")), (DateTime.new("1971-10-28T23:30:00-05:00") ~~ Date.new("1971-10-29")), ($a == Date.new("1971-10-28")), ($a == $a.Instant), ($a == $a.posix), ($a < $a.Instant + 1), ($a cmp $a.Instant).raku, ($b, $c, $a).sort.map(*.Str).raku, ($a eq "1971-10-28T10:45:00Z"), ($a lt $b), ($c lt $a), (DateTime.new("2016-12-31T23:59:59Z") < DateTime.new("2016-12-31T23:59:60Z")), (DateTime.new("2016-12-31T23:59:60Z") < DateTime.new("2017-01-01T00:00:00Z")), (DateTime.new("2016-12-31T23:59:60Z") == DateTime.new("2017-01-01T00:00:00Z")), (DateTime.new("2016-12-31T23:59:60Z") == DateTime.new("2016-12-31T22:59:60-01:00")), (DateTime.new("2017-01-01T00:00:00Z") == DateTime.new("2016-12-31T23:00:00-01:00")), (DateTime.new("1986-02-24T22:22:22+48:22") <=> DateTime.new("1986-02-20T22:22:22-47:38")).raku, do { class DT2 is DateTime {}; (DT2.new("1971-10-28T10:45:00Z") eqv $a) ~ " " ~ (DT2.new("1971-10-28T10:45:00Z") == $a) ~ " " ~ DT2.new("1971-10-28T10:45:00Z").WHICH }, ($a max $b).Str, (($a, $b).min).Str, ($a eqv $a.clone), ($a eqv $a.clone(:formatter({"F"}))), ($a === $a.clone(:formatter({"F"}))), (($a, $c).unique.elems), (($a, $c).sort.map(*.Str).raku), (DateTime.new("1971-10-28T10:45:00.5Z") > $a), ($a leg $c).raku, (DateTime.new("1971-10-28T10:45:00Z", :formatter({"x"})) cmp DateTime.new("1999-10-28T10:45:00Z", :formatter({"x"}))).raku, ($a ~~ "1971-10-28T10:45:00Z"), ($a eqv "1971-10-28T10:45:00Z"), ($c eqv $a.in-timezone(7200)), ($c === $a.in-timezone(7200)), ($a, $c).sort(* <=> *).map(*.timezone).raku, ($c, $a).sort(* <=> *).map(*.timezone).raku).join(" ") }
# rakudo 2026.08: True True True True True False True Order::Less Order::More Order::Less Order::More Order::Same Order::Less True True False True True True False True False True True False True True False False True False True Order::Less ("1971-10-28T10:45:00Z", "1971-10-28T12:45:00+02:00", "1998-10-19T02:03:00Z").Seq True True False True True False True True Order::Same False True DT2|1971-10-28T10:45:00Z 1998-10-19T02:03:00Z 1971-10-28T10:45:00Z True True False 2 ("1971-10-28T10:45:00Z", "1971-10-28T12:45:00+02:00").Seq True Order::Less Order::Same True False True True (0, 7200).Seq (7200, 0).Seq
```
rakupp 4.0.1-84: differs — `eqv` and `~~` across zones are False, `~~ Date` in both directions is False, `== $a.Instant` is False while `== $a.posix` is True, `59Z < 60Z` is False (no leap second), and a subclass is `eqv` a plain DateTime with an address `WHICH`; the `cmp`-by-Str bug is reproduced.

## F. DateTime arithmetic and mutation

### DT-27  later and earlier on a DateTime                                D:yes R:yes V:quirk
`second`/`seconds` move through the Instant: the count may be fractional
(0.7, 2.707), a Num or a numeric Str, the result's `.second` is always a
Rat (even for an Int count) and leap seconds are honoured (2012-06-30T23:59:59
+1 is `:60Z`, `:60Z` +1 is the next 00:00:00, 00:00:00 −1 lands on a
`:60` where one exists, 2015-06-30T23:59:60 −1 is `:59`). `minute(s)` and
`hour(s)` carry into days (1500 minutes is +1 day 1 hour, −13 hours the
previous day); `day(s)`/`week(s)` move by daycount; `month(s)`/`year(s)`
behave as on a Date (clip to 02-28) and keep the time; the count for
those units is its `.Int` (2.7 is 2; a Str `"1"` works — no `X::AdHoc`
as on a Date). The zone and the formatter are kept for every unit. A
DateTime sitting on a leap second keeps its `:60` after a non-second
move only when it lands on 23:59 of another leap-second date
(1972-12-31T23:59:60 +1 year is 1973-12-31T23:59:60Z), else the second
is clipped to 59 (2008-12-31T23:59:60 +1 day is 2009-01-01T23:59:59Z,
+1 minute 00:00:59Z, `:60.5` +1 day `:59.5`). One named unit exactly, or
a List of Pairs, else `X::AdHoc`; a non-Pair in the List `X::AdHoc`. The
quirk: an unknown unit (`fortnight`) returns the invocant unchanged
(`===`), where a Date throws.
```
say do { my $d = DateTime.new("2013-12-23T12:34:36Z"); ($d.later(second => 1).Str, $d.later(seconds => 74).Str, $d.later(:second(0.7)).Str, $d.later(:second(2.707)).Str, $d.earlier(:second(0.7)).Str, $d.later(minute => 1).Str, $d.later(minutes => 1500).Str, $d.later(hour => 1).Str, $d.later(hours => 14).Str, $d.later(day => 1).Str, $d.later(days => 9).Str, $d.later(week => 1).Str, $d.later(weeks => 3).Str, $d.later(month => 1).Str, $d.later(months => 13).Str, $d.later(year => 1).Str, $d.later(years => 2).Str, $d.earlier(hours => 13).Str, $d.earlier(days => 23).Str, $d.earlier(months => 12).Str, $d.later(:1fortnight).Str, ($d.later(:1fortnight) === $d), (try $d.later(:1day, :1hour)) // $!.^name, (try $d.later()) // $!.^name, $d.later((:2hours, :30minutes)).Str, $d.later(hours => 2.7).Str, $d.later(days => 2.7).Str, $d.later(day => -1).Str, DateTime.new("2013-12-23T12:34:36+05:00").later(:1hour).Str, DateTime.new("2013-12-23T22:34:36+05:00").later(:2hours).Str, DateTime.new("2013-12-23T12:34:36+05:00").later(:1second).timezone, DateTime.new("2014-01-31T12:34:36Z").later(month => 1).Str, DateTime.new("2016-02-29T00:00:00Z").later(:1year).Str, DateTime.new("2012-06-30T23:59:59Z").later(second => 1).Str, DateTime.new("2008-12-31T23:59:60Z").later(second => 1).Str, DateTime.new("2009-01-01T00:00:00Z").earlier(second => 1).Str, DateTime.new("2008-12-31T23:59:60Z").later(day => 1).Str, DateTime.new("2008-12-31T23:59:60Z").earlier(day => 1).Str, DateTime.new("1972-12-31T23:59:60Z").later(year => 1).Str, DateTime.new("2008-12-31T23:59:60Z").later(:1minute).Str, DateTime.new("2008-12-31T23:59:60Z").later(:1hour).Str, DateTime.new("2008-12-31T23:59:60.5Z").later(:1day).Str, DateTime.new("1994-05-03T12:00:00Z").later(days => 536106031).Str, DateTime.new("2013-12-23T12:34:36.25Z").later(:1day).Str, DateTime.new("2013-12-23T12:34:36.25Z").later(:1second).second.raku, DateTime.new("2013-12-23T12:34:36Z", :formatter({"F"})).later(:1day).Str, DateTime.new("2013-12-23T12:34:36Z", :formatter({"F"})).later(:1second).Str, DateTime.new("2013-12-23T12:34:36Z").later(:1second).formatter.^name, $d.later(:1day).daycount, DateTime.new(4242).later(:13hours).hh-mm-ss, DateTime.new(4242).earlier(:50hours).hh-mm-ss, DateTime.new("2006-01-01T00:00:00Z").earlier(:1second).hh-mm-ss, $d.later(:1second).second.^name, $d.later(:second(1.5)).second.^name, DateTime.new("2013-12-23T12:34:36+05:00").later(:1second).Str, DateTime.new("2013-12-23T12:34:36+05:00").later(:1month).Str, $d.later(:minute(-1500)).Str, $d.later(:hour(-13)).Str, (try $d.later((:1day, 5))) // $!.^name, $d.later(:second("1")).Str, (try $d.later(:day("1")).Str) // $!.^name, DateTime.new("2016-12-31T23:59:60Z").later(:1day).Str, DateTime.new("2016-12-31T23:59:60Z").later(:1week).Str, DateTime.new("2016-12-31T23:59:60Z").later(:1month).Str, DateTime.new("2015-06-30T23:59:59Z").later(:1second).Str, DateTime.new("2015-06-30T23:59:60Z").earlier(:1second).Str, DateTime.new("2015-06-30T23:59:60Z").later(:1second).Str, $d.later(:second(1e0)).Str, $d.later(:second(1e0)).second.^name).join(" ") }
# rakudo 2026.08: 2013-12-23T12:34:37Z 2013-12-23T12:35:50Z 2013-12-23T12:34:36.700000Z 2013-12-23T12:34:38.707000Z 2013-12-23T12:34:35.300000Z 2013-12-23T12:35:36Z 2013-12-24T13:34:36Z 2013-12-23T13:34:36Z 2013-12-24T02:34:36Z 2013-12-24T12:34:36Z 2014-01-01T12:34:36Z 2013-12-30T12:34:36Z 2014-01-13T12:34:36Z 2014-01-23T12:34:36Z 2015-01-23T12:34:36Z 2014-12-23T12:34:36Z 2015-12-23T12:34:36Z 2013-12-22T23:34:36Z 2013-11-30T12:34:36Z 2012-12-23T12:34:36Z 2013-12-23T12:34:36Z True X::AdHoc X::AdHoc 2013-12-23T15:04:36Z 2013-12-23T14:34:36Z 2013-12-25T12:34:36Z 2013-12-22T12:34:36Z 2013-12-23T13:34:36+05:00 2013-12-24T00:34:36+05:00 18000 2014-02-28T12:34:36Z 2017-02-28T00:00:00Z 2012-06-30T23:59:60Z 2009-01-01T00:00:00Z 2008-12-31T23:59:60Z 2009-01-01T23:59:59Z 2008-12-30T23:59:59Z 1973-12-31T23:59:60Z 2009-01-01T00:00:59Z 2009-01-01T00:59:59Z 2009-01-01T23:59:59.500000Z +1469802-10-18T12:00:00Z 2013-12-24T12:34:36.250000Z 37.25 F F Callable 56650 14:10:42 23:10:42 23:59:60 Rat Rat 2013-12-23T12:34:37+05:00 2014-01-23T12:34:36+05:00 2013-12-22T11:34:36Z 2013-12-22T23:34:36Z X::AdHoc 2013-12-23T12:34:37Z 2013-12-24T12:34:36Z 2017-01-01T23:59:59Z 2017-01-07T23:59:59Z 2017-01-31T23:59:59Z 2015-06-30T23:59:60Z 2015-06-30T23:59:59Z 2015-07-01T00:00:00Z 2013-12-23T12:34:37Z Rat
```
rakupp 4.0.1-84: differs — fractional second moves are truncated (`.later(:second(0.7))` is unchanged) and a second move gives an Int second; leap seconds are not landed on or kept (`:59` +1 is the next day, 1972-12-31T23:59:60 +1 year is `:59`, 2015-06-30T23:59:60 −1 is `:58`); two units are both applied, no unit and a non-Pair list are accepted; `hh-mm-ss` of the leap-second case is `23:59:59`.

### DT-28  truncated-to on a DateTime                                     D:yes R:yes V:spec
`second` replaces the second by its Int (`.second` becomes an Int);
`minute`, `hour`, `day` zero the smaller fields; `week` is Monday 00:00;
`month` and `year` as on a Date; the unit matches by prefix (`seconds`,
`hours`); `fortnight`, the empty string and `Second` throw `X::AdHoc`.
The zone is kept and the truncation is in local time (00:30+02:00 to
`day` is 00:00+02:00, i.e. 22:00Z the day before); the formatter is
kept; `.posix` of the result is the truncated instant; a leap second
survives `second` (`:60` and `:60.5` both give `:60Z`) and `minute` gives
`23:59:00Z`; the result is `===` the equal literal.
```
say do { my $d = DateTime.new("2012-02-29T12:34:56.946314+02:00"); ($d.truncated-to("second").Str, $d.truncated-to("second").second.raku, $d.truncated-to("minute").Str, $d.truncated-to("hour").Str, $d.truncated-to("day").Str, $d.truncated-to("week").Str, $d.truncated-to("month").Str, $d.truncated-to("year").Str, $d.truncated-to("seconds").Str, $d.truncated-to("hours").Str, (try $d.truncated-to("fortnight").Str) // $!.^name, (try $d.truncated-to("").Str) // $!.^name, $d.truncated-to("week").timezone, $d.truncated-to("week").day-of-week, DateTime.new("1969-07-20T08:17:32.987654321Z").truncated-to("second").second, DateTime.new("1999-01-03T10:00:00Z").truncated-to("week").Str, DateTime.new("2000-03-01T10:00:00Z").truncated-to("week").Str, DateTime.new("2005-02-01T15:20:35Z").truncated-to("hour").gist, $d.truncated-to("year").daycount, DateTime.new("2016-12-31T23:59:60Z").truncated-to("second").Str, DateTime.new("2016-12-31T23:59:60.5Z").truncated-to("second").Str, DateTime.new("2016-12-31T23:59:60Z").truncated-to("minute").Str, $d.truncated-to("month").raku, DateTime.new("2012-02-29T12:34:56Z", :formatter({"F"})).truncated-to("day").Str, $d.truncated-to("day").formatter.^name, $d.truncated-to("minute").second.raku, $d.truncated-to("minute").second.^name, DateTime.new("2012-02-29T12:34:56Z").truncated-to("second").second.^name, (try $d.truncated-to("Second").Str) // $!.^name, $d.truncated-to("second").^name, ($d.truncated-to("year") === DateTime.new("2012-01-01T00:00:00+02:00")), $d.truncated-to("week").posix, DateTime.new("2012-02-29T00:30:00+02:00").truncated-to("day").Str, DateTime.new("2012-02-29T00:30:00+02:00").truncated-to("day").utc.Str).join(" ") }
# rakudo 2026.08: 2012-02-29T12:34:56+02:00 56 2012-02-29T12:34:00+02:00 2012-02-29T12:00:00+02:00 2012-02-29T00:00:00+02:00 2012-02-27T00:00:00+02:00 2012-02-01T00:00:00+02:00 2012-01-01T00:00:00+02:00 2012-02-29T12:34:56+02:00 2012-02-29T12:00:00+02:00 X::AdHoc X::AdHoc 7200 1 32 1998-12-28T00:00:00Z 2000-02-28T00:00:00Z 2005-02-01T15:00:00Z 55927 2016-12-31T23:59:60Z 2016-12-31T23:59:60Z 2016-12-31T23:59:00Z DateTime.new(2012,2,1,0,0,0,:timezone(7200)) F Callable 0 Int Int X::AdHoc DateTime True 1330293600 2012-02-29T00:00:00+02:00 2012-02-28T22:00:00Z
```
rakupp 4.0.1-84: differs — the refused units return the DateTime unchanged, and `.truncated-to("week").posix` still counts the dropped seconds (1330293656) although its Str is 00:00:00; every other field agrees.

### DT-29  DateTime ± Duration, DateTime − DateTime, the Numeric fallback D:yes R:yes V:quirk
`DateTime - DateTime` is a Duration, signed, fractional
(`Duration.new(0.25)`), leap seconds counted (2017-01-01T00:00:00Z minus
2016-12-31T23:59:59Z is 2.0, 1997-07-01 minus 1997-06-30 is 86401,
2008-01-01 minus 1973-01-01 is 1104451221). `DateTime ± Duration` and
`Duration + DateTime` give a DateTime in the SAME zone that is `eqv` and
`===` the equal literal, may land on a leap second (23:59:59Z + 1 is
`:60Z`, `:60Z` + 1 is 00:00:00Z), take a negative Duration — but the
formatter is dropped (the quirk; `.formatter` is the Callable type
object afterwards). The Numeric fallback: `DateTime ± Int`, `± Rat`,
`Int + DateTime` give an INSTANT (`Instant.from-posix(1.0)`), `DateTime
* 2` a Num, `DateTime + DateTime` `X::AdHoc`, `DateTime - Instant` and
`Instant - DateTime` a Duration, `DateTime - Date` an Instant, `Date -
DateTime` a Num.
```
say do { my $dt1 = DateTime.new(:2017year, :11month, :15day, :18hour, :36minute, :second(17.25), :timezone(-5*3600)); my $dt2 = DateTime.new(:2015year, :12month, :25day, :3hour, :6minute, :second(7.77), :timezone(14*3600)); my $dur = Duration.new(59826610.48); (($dt1 - $dt2).raku, ($dt1 - $dt2).^name, ($dt2 - $dt1).raku, ($dt1 - $dur).Str, ($dt1 - $dur).timezone, ($dt2 + $dur).Str, ($dur + $dt2).Str, (($dt1 - $dur) eqv $dt2.in-timezone($dt1.timezone)), (($dt1 - $dur) === $dt2), (DateTime.new(:2016year) - DateTime.new(:2015year)).raku, (DateTime.new(:2016year, :3600timezone) - Duration.new(31536001.0)).Str, (DateTime.new(:2015year) + Duration.new(31536001.0)).Str, (Duration.new(42) + DateTime.new(:2015year, :3600timezone)).Str, (DateTime.new(0) + 1).^name, (DateTime.new(0) + 1).raku, (DateTime.new(0) - 1).^name, (1 + DateTime.new(0)).^name, (DateTime.new(0) + 1.5).^name, (try DateTime.new(0) + DateTime.new(1)) // $!.^name, (DateTime.new(0) * 2).^name, (DateTime.new(0) + Duration.new(0.5)).Str, (DateTime.new(0) + Duration.new(0.5)).second.raku, (DateTime.new(0) - Duration.new(0.5)).Str, (DateTime.new(0) + Duration.new(86400 * 366)).Str, (DateTime.new("2016-12-31T23:59:59Z") + Duration.new(1)).Str, (DateTime.new("2016-12-31T23:59:60Z") + Duration.new(1)).Str, (DateTime.new("2017-01-01T00:00:00Z") - DateTime.new("2016-12-31T23:59:59Z")).raku, (DateTime.new("2016-12-31T23:59:60Z") - DateTime.new("2016-12-31T23:59:59Z")).raku, (DateTime.new("2017-01-01T00:00:00Z") - DateTime.new("2016-12-31T23:59:60Z")).raku, (DateTime.new("1997-07-01T00:00:00Z") - DateTime.new("1997-06-30T00:00:00Z")).raku, (DateTime.new("2008-01-01T00:00:00Z") - DateTime.new("1973-01-01T00:00:00Z")).raku, (DateTime.new(0, :formatter({"F"})) + Duration.new(1)).Str, (DateTime.new(0) + Duration.new(1)).formatter.^name, (DateTime.new(0, :timezone(3600)) + Duration.new(1)).Str, (Date.new("2020-01-01") - DateTime.new(0)).^name, (DateTime.new(0) - Date.new("2020-01-01")).^name, (DateTime.new(0) - Instant.from-posix(0)).raku, (DateTime.new(0) + Instant.from-posix(0).Rat).^name, (DateTime.new(0) - DateTime.new(0)).raku, (DateTime.new(0, :timezone(3600)) - DateTime.new(0)).raku, (DateTime.new("2013-12-23T12:34:36.25Z") - DateTime.new("2013-12-23T12:34:36Z")).raku, (DateTime.new(0) + Duration.new(-1)).Str, (Duration.new(1) + DateTime.new(0, :formatter({"F"}))).Str, (DateTime.new(0) - Duration.new(1)).formatter.^name, ((DateTime.new(0) + Duration.new(1)) eqv DateTime.new(1)), ((DateTime.new(0) + Duration.new(1)) === DateTime.new(1)), (Instant.from-posix(0) - DateTime.new(0)).raku, (DateTime.new(0) + 1).tai).join(" ") }
# rakudo 2026.08: Duration.new(59826610.48) Duration Duration.new(-59826610.48) 2015-12-24T08:06:07.770000-05:00 -18000 2017-11-16T13:36:17.250000+14:00 2017-11-16T13:36:17.250000+14:00 True False Duration.new(31536001.0) 2015-01-01T00:00:00+01:00 2016-01-01T00:00:00Z 2015-01-01T00:00:42+01:00 Instant Instant.from-posix(1.0) Instant Instant Instant X::AdHoc Num 1970-01-01T00:00:00.500000Z 0.5 1969-12-31T23:59:59.500000Z 1971-01-02T00:00:00Z 2016-12-31T23:59:60Z 2017-01-01T00:00:00Z Duration.new(2.0) Duration.new(1.0) Duration.new(1.0) Duration.new(86401.0) Duration.new(1104451221.0) 1970-01-01T00:00:01Z Callable 1970-01-01T01:00:01+01:00 Num Instant Duration.new(0.0) Instant Duration.new(0.0) Duration.new(0.0) Duration.new(0.25) 1969-12-31T23:59:59Z 1970-01-01T00:00:01Z Callable True True Duration.new(0.0) 11
```
rakupp 4.0.1-84: differs — `DateTime - DateTime` is a Num with float noise (`59826610.48000002e0`), `DateTime ± Int` is a DateTime (not an Instant), `DateTime + DateTime` is 1, `DateTime - Instant` a DateTime, the `eqv` of `$dt1 - $dur` against the literal is False, leap seconds are not landed on, and `.tai` is missing, so the line died at its last field.

### DT-30  clone on a DateTime                                            D:yes R:yes V:spec
`.clone(:year, :month, :day, :hour, :minute, :second, :timezone,
:formatter)` replaces fields and re-validates (2012-02-29 with
`:year(2015)` throws `X::Temporal::OutOfRange`, `:minute(60)`, `:day(32)`,
`:month(0)` too, `:second(60)` off a leap date `X::OutOfRange`); `:day(*)`
and `:day(*-1)` work; hour goes through `Int()` (`"3"`, 3.9 is 3) and so
does `:timezone` (`"3600"` accepted here); the second is kept as given
(`"3"` stays a Str); `:timezone` RELABELS — same wall clock, a different
instant (`.posix` changes, `==` the original is False), unlike
`in-timezone`; a leap second cloned to another leap date is fine
(2015-06-30); unknown nameds are ignored; `:formatter(Callable)` restores
the default; no arguments give an `===` copy with a fresh `.daycount`.
```
say DateTime.new("2015-12-24T12:23:00Z").clone(hour => 0).Str, " ", DateTime.new("2015-12-24T12:23:00+02:00").clone(:timezone(0)).Str, " ", DateTime.new("2015-12-24T12:23:00+02:00").clone(:timezone(0)).posix, " ", DateTime.new("2015-12-24T12:23:00+02:00").posix, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:second(5.5)).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:day(*)).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:month(2), :day(*)).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:year(2016), :month(2), :day(29)).Str, " ", (try DateTime.new("2012-02-29T12:34:56Z").clone(year => 2015).Str) // $!.^name, " ", (try DateTime.new("2015-12-24T12:23:00Z").clone(:minute(60))) // $!.^name, " ", DateTime.new("2015-12-24T12:23:00Z", :formatter({"F"})).clone(:1hour).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:formatter({"G"})).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone().Str, " ", (DateTime.new("2015-12-24T12:23:00Z").clone() === DateTime.new("2015-12-24T12:23:00Z")), " ", DateTime.new("2015-12-24T12:23:00Z").clone(:foo).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:hour("3")).hour.^name, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:second("3")).second.^name, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:1day).daycount, " ", (try DateTime.new("2016-12-31T23:59:60Z").clone(:2015year).Str) // $!.^name, " ", DateTime.new("2016-12-31T23:59:60Z").clone(:2015year, :6month, :30day).Str, " ", DateTime.new("2015-12-24T12:23:00Z", :formatter({"F"})).clone(:formatter(Callable)).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:timezone("3600")).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:hour(3.9)).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone(:day(*-1)).Str, " ", DateTime.new("2015-12-24T12:23:00Z").clone().formatter.^name, " ", (DateTime.new("2015-12-24T12:23:00Z").clone(:timezone(3600)) == DateTime.new("2015-12-24T12:23:00Z")), " ", (DateTime.new("2015-12-24T12:23:00Z").in-timezone(3600) == DateTime.new("2015-12-24T12:23:00Z")), " ", (try DateTime.new("2015-12-24T12:23:00Z").clone(:second(60)).Str) // $!.^name, " ", (try DateTime.new("2015-12-24T12:23:00Z").clone(:day(32)).Str) // $!.^name, " ", (try DateTime.new("2015-12-24T12:23:00Z").clone(:month(0)).Str) // $!.^name
# rakudo 2026.08: 2015-12-24T00:23:00Z 2015-12-24T12:23:00Z 1450959780 1450952580 2015-12-24T12:23:05.500000Z 2015-12-31T12:23:00Z 2015-02-28T12:23:00Z 2016-02-29T12:23:00Z X::Temporal::OutOfRange X::Temporal::OutOfRange F G 2015-12-24T12:23:00Z True 2015-12-24T12:23:00Z Int Str 57357 X::OutOfRange 2015-06-30T23:59:60Z 2015-12-24T12:23:00Z 2015-12-24T12:23:00+01:00 2015-12-24T03:23:00Z 2015-12-30T12:23:00Z Callable False True X::OutOfRange X::Temporal::OutOfRange X::Temporal::OutOfRange
```
rakupp 4.0.1-84: matches on every field but one — `:day(*-1)` gives 12-31 instead of 12-30.

### DT-31  Leap seconds through posix and Instant                        D:yes R:yes V:quirk
The leap second and the second after it share the POSIX label
(`23:59:60Z.posix` and `00:00:00Z.posix` are both 1483228800, `.posix(:real)`
too), but their Instants differ by one (tai 1483228836 and 1483228837,
`:59Z` 1483228835) and `.Instant.to-posix` tells them apart by the flag
(`(1483228800.0, Bool::True)`, `.5` kept); `DateTime.new(Instant)` brings
the `:60` back, `DateTime.new(1483228800)` (Numeric) never does.
`.whole-second` 60, `.hh-mm-ss` `23:59:60`, `.day-fraction` 86400/86401,
`.raku` `DateTime.new(2016,12,31,23,59,60)`, `.Instant.raku`
`Instant.from-posix(1483228800.0,True)`, `.modified-julian-date`
57754 + 86400/86401 while the next second is 57754.0. The quirk: the
DATE of a leap-second Instant is the next day (`Instant.from-posix(1483228800,
True).Date` and `Date.new(...)` are 2017-01-01; 1483228799 is 2016-12-31),
because `Date.new(Instant)` divides the POSIX time. The 1972 seconds: `1972-06-30T23:59:60Z`
has posix 78796800 and tai 78796810, 1972-07-01T00:00:00Z tai 78796811,
1972-01-01T00:00:00Z tai 63072010 for posix 63072000.
```
say DateTime.new("2016-12-31T23:59:60Z").posix, " ", DateTime.new("2017-01-01T00:00:00Z").posix, " ", DateTime.new("2016-12-31T23:59:60Z").Instant.to-posix.raku, " ", DateTime.new("2017-01-01T00:00:00Z").Instant.to-posix.raku, " ", DateTime.new("2016-12-31T23:59:60.5Z").Instant.to-posix.raku, " ", DateTime.new(Instant.from-posix(1483228800, True)).Str, " ", DateTime.new(Instant.from-posix(1483228800)).Str, " ", DateTime.new(Instant.from-posix(1483228800.5, True)).Str, " ", DateTime.new("2016-12-31T23:59:60Z").whole-second, " ", DateTime.new("2016-12-31T23:59:60Z").hh-mm-ss, " ", DateTime.new("2016-12-31T23:59:60Z").day-fraction.raku, " ", DateTime.new("2016-12-31T23:59:60Z").posix(:real).raku, " ", (DateTime.new("2016-12-31T23:59:60Z").Instant < DateTime.new("2017-01-01T00:00:00Z").Instant), " ", DateTime.new(1483228800).Str, " ", DateTime.new(1483228800.5).Str, " ", Instant.from-posix(1483228800).DateTime.Str, " ", Instant.from-posix(1483228800, True).DateTime.Str, " ", Instant.from-posix(1483228800, True).Date.Str, " ", (Instant.from-posix(1483228800, True).Int - Instant.from-posix(1483228800).Int), " ", DateTime.new("2016-12-31T23:59:60Z").Instant.raku, " ", DateTime.new("2016-12-31T23:59:60Z").raku, " ", DateTime.new("2016-12-31T23:59:60Z").modified-julian-date.raku, " ", DateTime.new("2016-12-31T23:59:60Z").julian-date.raku, " ", DateTime.new("2017-01-01T00:00:00Z").modified-julian-date.raku, " ", DateTime.new("2016-12-31T23:59:60Z").utc.Str, " ", DateTime.new("2016-12-31T23:59:60Z").second.raku, " ", DateTime.new("1972-06-30T23:59:60Z").posix, " ", DateTime.new("1972-01-01T00:00:00Z").posix, " ", Date.new(Instant.from-posix(1483228800, True)).Str, " ", Date.new(Instant.from-posix(1483228799)).Str, " ", (DateTime.new("2016-12-31T23:59:60Z").Instant.to-posix[1] ~~ Bool), " ", DateTime.new("2016-12-31T23:59:60Z").later(:1second).Instant.to-posix.raku, " ", DateTime.new("2016-12-31T23:59:60Z").Instant.tai, " ", DateTime.new("2017-01-01T00:00:00Z").Instant.tai, " ", DateTime.new("2016-12-31T23:59:59Z").Instant.tai, " ", Instant.from-posix(1483228800, False).tai, " ", Instant.from-posix(1483228801, True).tai, " ", DateTime.new("1972-06-30T23:59:60Z").Instant.tai, " ", DateTime.new("1972-07-01T00:00:00Z").Instant.tai, " ", DateTime.new("1972-01-01T00:00:00Z").Instant.tai
# rakudo 2026.08: 1483228800 1483228800 (1483228800.0, Bool::True) (1483228800.0, Bool::False) (1483228800.5, Bool::True) 2016-12-31T23:59:60Z 2017-01-01T00:00:00Z 2016-12-31T23:59:60.500000Z 60 23:59:60 <86400/86401> 1483228800 True 2017-01-01T00:00:00Z 2017-01-01T00:00:00.500000Z 2017-01-01T00:00:00Z 2016-12-31T23:59:60Z 2017-01-01 -1 Instant.from-posix(1483228800.0,True) DateTime.new(2016,12,31,23,59,60) <4990003353/86401> <424704893107/172802> 57754.0 2016-12-31T23:59:60Z 60 78796800 63072000 2017-01-01 2016-12-31 True (1483228800.0, Bool::False) 1483228836 1483228837 1483228835 1483228837 1483228838 78796810 78796811 63072010
```
rakupp 4.0.1-84: differs — no leap seconds: `23:59:60Z.posix` is 1483228799, the `to-posix` flags are False, the Instant round trip comes back as 2017-01-01T00:00:00Z, `.Instant <` between the two is False; `.Date` on an Instant is missing, so the line died there.

### DT-32  Type objects, immutability, MRO                                D:partial R:partial V:spec
`DateTime.gist` is `(DateTime)`, `.raku` `DateTime`, `.Str` `""`;
`DateTime.Date` and `.DateTime` are the type objects; `DateTime ~~
Dateish` True and `~~ Cool` False, while Instant and Duration are Cool
and not Dateish (MROs `Instant, Cool, Any, Mu`; Instant does Real and
Numeric). On a type object `DateTime.year` throws `X::AdHoc`, `.posix`
`X::Multi::NoMatch`, `.utc`, `.hh-mm-ss`, `.Instant` and `Duration.tai`
`X::Parameter::InvalidConcreteness`, `Instant.tai` and `Instant.to-posix`
`X::AdHoc`, `Instant.Bridge` and `Duration.new(Int)`
`X::Numeric::Uninitialized`, `Instant.from-posix(Int)`
`X::Parameter::InvalidConcreteness`, `from-posix("x")`
`X::Str::Numeric`, `from-posix(Inf)`/`(NaN)` `X::AdHoc`;
`DateTime.new(:year(Any))` gives 0000-01-01T00:00:00Z with a warning.
The attributes are read-only (`X::Assignment::RO` for year, timezone,
second, hour, formatter). There is no `DateTime.today`, `Date.now`,
`DateTime.succ`/`.pred`/`.first-date-in-month`, `Date.hour`,
`Date.in-timezone`, `Date.posix` or `Date.Instant` (`X::Method::NotFound`).
```
say DateTime.gist, " ", DateTime.raku, " ", DateTime.Date.^name, " ", DateTime.Date.defined, " ", DateTime.DateTime.^name, " ", DateTime.DateTime.defined, " ", (DateTime ~~ Dateish), " ", (DateTime ~~ Cool), " ", (Instant ~~ Dateish), " ", (Instant ~~ Cool), " ", (Duration ~~ Cool), " ", DateTime.^mro.map(*.^name).raku, " ", Instant.^mro.map(*.^name).raku, " ", Duration.^mro.map(*.^name).raku, " ", (try DateTime.year) // $!.^name, " ", (try DateTime.posix) // $!.^name, " ", (try DateTime.utc) // $!.^name, " ", (try DateTime.new("2000-01-01T00:00:00Z").year = 5) // $!.^name, " ", (try DateTime.new("2000-01-01T00:00:00Z").timezone = 5) // $!.^name, " ", (try DateTime.new("2000-01-01T00:00:00Z").second = 5) // $!.^name, " ", (quietly DateTime.Str).raku, " ", (quietly Instant.Str).raku, " ", (quietly Duration.Str).raku, " ", Instant.gist, " ", Duration.gist, " ", (try Instant.tai) // $!.^name, " ", (try Duration.tai) // $!.^name, " ", (try Instant.to-posix) // $!.^name, " ", (try Duration.new(Int)) // $!.^name, " ", (try Instant.from-posix(Int)) // $!.^name, " ", (try Instant.from-posix("x")) // $!.^name, " ", (try Instant.from-posix(Inf)) // $!.^name, " ", (try Instant.from-posix(NaN)) // $!.^name, " ", (quietly DateTime.new(:year(Any)).Str), " ", Date.new(2020, 1, 2).^name, " ", DateTime.new(:2020year).^name, " ", DateTime.now.WHAT.gist, " ", (Instant.from-posix(0) ~~ Instant), " ", (try DateTime.hh-mm-ss) // $!.^name, " ", (try DateTime.Instant) // $!.^name, " ", (try Instant.raku) // $!.^name, " ", (try Duration.raku) // $!.^name, " ", (try DateTime.new("2000-01-01T00:00:00Z").hour = 5) // $!.^name, " ", (try DateTime.new("2000-01-01T00:00:00Z").formatter = { 1 }) // $!.^name, " ", (try DateTime.today) // $!.^name, " ", (try Date.now) // $!.^name, " ", Date.new("2020-01-02").WHAT.gist, " ", (try DateTime.new("2000-01-01T00:00:00Z").succ) // $!.^name, " ", (try DateTime.new("2000-01-01T00:00:00Z").pred) // $!.^name, " ", (try DateTime.new("2000-01-01T00:00:00Z").first-date-in-month) // $!.^name, " ", (try Date.new("2020-01-02").hour) // $!.^name, " ", (try Date.new("2020-01-02").in-timezone(0)) // $!.^name, " ", (try Date.new("2020-01-02").posix) // $!.^name, " ", (try Date.new("2020-01-02").Instant) // $!.^name, " ", Instant.^roles.map(*.^name).raku, " ", (try Instant.Bridge.^name) // $!.^name
# rakudo 2026.08: (DateTime) DateTime Date False DateTime False True False False True True ("DateTime", "Any", "Mu").Seq ("Instant", "Cool", "Any", "Mu").Seq ("Duration", "Cool", "Any", "Mu").Seq X::AdHoc X::Multi::NoMatch X::Parameter::InvalidConcreteness X::Assignment::RO X::Assignment::RO X::Assignment::RO "" "" "" (Instant) (Duration) X::AdHoc X::Parameter::InvalidConcreteness X::AdHoc X::Numeric::Uninitialized X::Parameter::InvalidConcreteness X::Str::Numeric X::AdHoc X::AdHoc 0000-01-01T00:00:00Z Date DateTime (DateTime) True X::Parameter::InvalidConcreteness X::Parameter::InvalidConcreteness Instant Duration X::Assignment::RO X::Assignment::RO X::Method::NotFound X::Method::NotFound (Date) X::Method::NotFound X::Method::NotFound X::Method::NotFound X::Method::NotFound X::Method::NotFound X::Method::NotFound X::Method::NotFound ("Real", "Numeric").Seq X::Numeric::Uninitialized
```
rakupp 4.0.1-84: differs — `DateTime ~~ Cool` True; Instant and Duration are neither Cool nor Real (MRO `Any, Mu`); the type-object calls are `X::Method::NotFound` or answer values (`Instant.tai` 0, `Instant.to-posix` 10, `Instant.from-posix(Int)` 10); `DateTime.today`, `Date.now`, `DateTime.succ`, `Date.posix` and `Date.Instant` exist; `^roles` is missing, so the line died there.

### DT-33  Str.Date, Str.DateTime, and the local day                     D:partial R:partial V:spec
`Str.Date` is `Date.new(Str)` and `Str.DateTime` is `DateTime.new(Str)`
(a date-only string is midnight UTC), refusing with
`X::Temporal::InvalidFormat`; `Date.Str.Date` round-trips. `Date.today`
is the local date: `Date.today.DateTime` is that date's midnight UTC
(timezone 0) and `DateTime.new(:date(Date.today)).hour` 0; `DateTime.now
~~ Date.today` is True and `Date.today ~~ DateTime.now` False (DT-12);
the UTC date `now.Date`/`Date.new(DateTime.now(:timezone(0)))` stays
within a day of it. Clock-dependent fields print Booleans.
```
say "2020-01-02".Date.raku, " ", "2020-01-02T03:04:05Z".DateTime.Str, " ", (try "x".Date) // $!.^name, " ", (try "x".DateTime) // $!.^name, " ", Date.new("2020-01-02").Str.Date.raku, " ", "2020-01-02".DateTime.Str, " ", (Date.today.Str ~~ /^\d\d\d\d\-\d\d\-\d\d$/).Bool, " ", (Date.today == DateTime.now.Date), " ", Date.today.DateTime.timezone, " ", DateTime.new(:date(Date.today)).hour, " ", (Date.today ~~ DateTime.now), " ", (DateTime.now ~~ Date.today), " ", ((Date.today.daycount - Date.new(DateTime.now(:timezone(0))).daycount).abs <= 1), " ", ((Date.today.daycount - now.Date.daycount).abs <= 1), " ", (Date.today.daycount == now.Date.daycount || $*TZ != 0)
# rakudo 2026.08: Date.new(2020,1,2) 2020-01-02T03:04:05Z X::Temporal::InvalidFormat X::Temporal::InvalidFormat Date.new(2020,1,2) 2020-01-02T00:00:00Z True True 0 0 False True True True True
```
rakupp 4.0.1-84: differs — `"x".Date` and `"x".DateTime` give 0000-01-01 instead of throwing, `DateTime.now ~~ Date.today` is False, and `now.Date` is missing, so the line died there.

## Counts

| | items |
|---|---|
| total | 33 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 4 |
| Rakudo bugs (do not imitate) | 4 — DT-13 a Date range iterates by Str and `.sum` is a Date, DT-19 date-only `2012-13-22` throws `X::AdHoc`, DT-23 `.Str` overflows to `:60` / throws / prints seven decimals and `WHICH` is the formatted string, DT-26 `cmp`/`leg`/`sort` by Str |
| quirks (recorded, step two decides) | 13 — DT-04 Instant `===` False, DT-05 `Duration.new(Inf)` is a Rat, DT-07 a Date inherits a DateTime's formatter, DT-11 the Numeric fallback, DT-12 `cmp` by Str, DT-14 unknown unit `X::TypeCheck::Return`, DT-15 prefix-matched units, DT-20 the second throws a plain `X::OutOfRange`, DT-21 a positional Str second stays a Str, DT-25 Num vs Rat seconds, DT-27 unknown unit is a no-op, DT-29 `± Duration` drops the formatter and `± Int` is an Instant, DT-31 a leap second's Date is the next day |
| rakupp 4.0.1-84 differs | 31 |
| rakupp 4.0.1-84 matches (all but one edge field each) | 2 — DT-18, DT-30 |

Recurring rakupp gaps, for step two: there is no leap-second table, so
the TAI offset is 10 and `from-posix`'s flag, `to-posix`'s flag, the
`:60` second through Instant, `posix`, `later` and `± Duration` are all
lost (DT-01, 02, 21, 26, 27, 29, 31); Instant and Duration are not Cool
or Real, print and compute as Nums, and lack `.tai`, `.to-nanos`,
`.Date`, `.Instant` and the `Instant.new` refusal (DT-01–06); the
Dateish surface misses `days-in-month`/`days-in-year` (both forms),
`yyyy-mm`, `mm-dd`, the separator argument, the instance form of
`new-from-daycount`, `^roles`; the string parsers accept almost any
input and the Temporal exceptions lack `.comment`, plain `X::OutOfRange`
lacks `.what`/`.got`/`.range` (DT-08, 19, 20); `later`, `earlier` and
`truncated-to` never refuse a unit or a count and fractional second moves
truncate (DT-14, 15, 27, 28); `Date .. Date` is a List, not a Range
(DT-13); dates before year 1 are one day off and negative years print
with three digits (DT-10); `~~`/`eqv` between Dates, DateTimes and across
zones are False and a subclass's `WHICH` is an address (DT-12, 26); the
Numeric fallbacks give Nums or DateTimes where Rakudo gives Ints or
Instants, `posix(True)` ignores its flag, the julian dates use local
time, and `truncated-to("week").posix` keeps the seconds (DT-11, 22, 28,
29); `Date.new`/`DateTime.new` with no arguments throw instead of
failing, and type-object calls answer values (DT-07, 21, 32). Where
rakupp is the consistent one — `DateTime.new("2012-13-22")` throwing
`X::Temporal::OutOfRange`, iterating a Date range by value — keep it.

## Method (how this sheet was produced)

As for [Supply.md](Supply.md) and [Range.md](Range.md): 36 probe lines in
four rounds, each run through both engines under `alarm 10` in its own
sandbox directory. Because `say` evaluates every argument before printing
anything, one missing method on rakupp blanked a whole line; a small
transformer rewrote each probe into a `print`-per-field chain for a
rakupp-only pass, so the rakupp lines above name the field a line died
at and what the earlier fields answered. Traps met: a helper `sub X`
parses as the coercion type `X(...)`; `:2.7days` is a malformed radix
number and `:-1day` is not a colonpair (rakupp also refuses `:0day`);
a formatter that returns an Int fails `Str`'s `Str:D` return check;
`Duration.new(Inf).tai` cannot be printed (a zero-denominator Rat), show
it with `.raku`; `Instant.Bridge` on the type object throws an
`X::Numeric::Uninitialized` whose text reads like a warning; `Date.new("2015-12-24").clone(:month(2))`
is a valid date (24 February) — the intended failure needs day 31; the
date-only `DateTime.new("2012-13-22")` throws an `X::AdHoc` that has no
`.what`; `clone(:month("12"))`, `later(days => 2.7)` and
`DateTime.new(0, :timezone("3600"))` throw on Rakudo, so they sit inside
`try`. Clock- and zone-dependent fields are Booleans or type names. D
flags from `doc/Type/{Instant,Duration,Dateish,Date,DateTime,Str}.rakudoc`
and `doc/Language/{temporal,terms,variables}.rakudoc`; R flags from
`S32-temporal/{Date,DateTime,DateTime-Instant-Duration,calendar,local,time,juliandate,greg-jd-frac-seconds,baum-gregorian-data}.t`.
