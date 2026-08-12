# Regression: four divergences found running `rakupp install` against real
# ecosystem distributions (JSONL, Locale::Dates) — each blocked a module's own
# test suite, none is module-specific.
#
# 1. TYPE-OBJECT CALL: `T(x)` on a class with no CALL-ME runs Rakudo's
#    coercion chain — the argument's own .T method if its class has one, then
#    T.COERCE(x), then T.new(x). Locale::Dates("EN") is the .new leg.
# 2. IMPLICIT %_: a method carries an implicit *%_ — nameds no explicit param
#    claimed land there. Locale::Dates' `multi method new($l = "EN")` forwards
#    attribute inits with `self.bless(|%_)`.
# 3. DIE INSIDE CATCH: an explicit CATCH in a `try` block REPLACES try's
#    implicit swallow — an exception the handler itself throws (the
#    catch-wrap-rethrow idiom, JSONL's strict mode) and an exception no
#    when/default matched both propagate PAST the try.
# 4. JUNCTION SUBSCRIPT: `@rows.all.<age>` autothreads the subscript over the
#    eigenstates via AT-KEY/AT-POS — user classes with their own AT-KEY
#    (JSONL::Line) included. Adverbed subscripts deliberately stay out.
#
# Every check verified against Rakudo (this file runs on both engines).
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# ---- 1. type-object call: .T / COERCE / new -------------------------------
class UB::ViaNew { has $.x; multi method new($x) { self.bless(:$x) } }
check UB::ViaNew(7).x, 7, 'T(x) reaches multi method new';

class UB::Compound::Name { has $.x; multi method new($x) { self.bless(:$x) } }
check UB::Compound::Name(5).x, 5, 'compound-named T(x) too';

class UB::ViaCoerce {
    has $.v;
    method COERCE($s) { self.new(v => $s.uc) }
}
check UB::ViaCoerce('hi').v, 'HI', 'T.COERCE wins when declared';

class UB::Target { has $.tag }
class UB::Source {
    method UB::Target() { UB::Target.new(tag => 'via-method') }
}
check UB::Target(UB::Source.new).tag, 'via-method',
    "the argument's own .T method wins over .new";

my $already = UB::ViaNew.new(x => 1);
check (UB::ViaNew($already) === $already), True, 'T(a T) is identity';

# ---- 2. implicit %_ in methods --------------------------------------------
class UB::Args {
    method grab($p = 0) { %_ }
}
check UB::Args.grab(a => 1, b => 2)<a>, 1, 'unclaimed nameds land in %_';
check UB::Args.grab(a => 1, b => 2).elems, 2, '…all of them';

class UB::Bless {
    has $.x;
    multi method new($l = "EN") { %_ ?? self.bless(|%_) !! "fallback" }
}
check UB::Bless.new(x => 9).x, 9, 'self.bless(|%_) forwards attribute inits';
check UB::Bless.new, "fallback", 'no nameds: %_ is empty and falsy';

class UB::Claimed {
    method grab(:$a) { %_ }
}
check UB::Claimed.grab(a => 1, b => 2).keys.join(','), 'b',
    'a named an explicit param claimed does NOT reach %_';

# ---- 3. die inside CATCH escapes the try ----------------------------------
my $escaped = False;
{
    try { die "original"; CATCH { default { die "wrapped" } } }
    CATCH { default { $escaped = .message eq "wrapped"; } }
}
check $escaped, True, 'a die from a CATCH handler propagates past the try';

my $lived = False;
try { die "original"; CATCH { default { } } }
$lived = True;
check $lived, True, 'a handled exception still survives the try';

my $unmatched = False;
{
    class UB::X is Exception { method message() { "never" } }
    try { die "plain"; CATCH { when UB::X { } } }
    CATCH { default { $unmatched = True; } }
}
check $unmatched, True, 'an exception no when/default matched escapes too';

# ---- 4. junction subscripts autothread ------------------------------------
# (collapse-only checks: a junction ARGUMENT would autothread `check` itself)
my @rows = {age => 40, name => 'a'}, {age => 35, name => 'b'};
check so(@rows.all.<age> > 30), True, 'hash subscript threads over .all and collapses';
check so(@rows.all.<age> > 38), False, '…and all() still needs every eigenstate';
check so(@rows.any.<age> > 38), True, '.any threads the same way';

my @deep = [1, 2], [3, 4];
check so(@deep.all.[0] >= 1) && so(@deep.all.[0] <= 3), True,
    'positional subscript threads too';

class UB::Keyed {
    has %.h;
    method AT-KEY($k) { %!h{$k} }
}
my @objs = UB::Keyed.new(h => {age => 50}), UB::Keyed.new(h => {age => 60});
check so(@objs.all.<age> > 45), True, 'a user AT-KEY threads like a Hash';

# ---------------------------------------------------------------------------
if @fail {
    .say for @fail;
    say "FAIL ({@fail.elems})";
    exit 1;
}
say "PASS";
