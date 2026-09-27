# Regression: a value entering an ATTRIBUTE carried the readonly mark of the
# name it was read through, so the attribute could not be assigned afterwards —
# "Cannot assign to a readonly variable or a value" from a later `$!x = …`.
# Every plain `$` parameter is readonly, and so is a sigilless one bound to a
# value, and so is a constant; the attribute is a container of its own, and
# Rakudo ASSIGNS into it. The routes: the default constructor (both its walk
# and its trailing pass), an attribute's default, `.clone`, and an attributive
# parameter, positional (`method set($!x)`, Cro's MessageWithBody idiom) or
# named (`BUILD(:$!x)`). Found while fixing A2's `C.new(q => $v)` regression
# (ROAST-TRACKS-PLAN track A, 2026-09-27); these routes are older than it.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/rakudo — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub try-set(&code) { my $r = try code(); $r // "died: {$!.message}" }

class A { has $.x; method set($n) { $!x = $n; $!x } }

# --- the default constructor ------------------------------------------------
sub via-new(\v)   { A.new(x => v) }
sub via-colon(\v) { A.new(:x(v)) }
ck(try-set({ via-new(1).set(2) }), 2, 'A.new(x => v) from a sigilless parameter stays assignable');
ck(try-set({ via-colon(1).set(3) }), 3, '…and A.new(:x(v))');
class T { has $.x; submethod TWEAK(:$x) { $!x = $x } }
sub via-tweak(\v) { T.new(x => v) }
ck(try-set({ via-tweak(4).x }), 4, "…so a TWEAK assigning it again succeeds");
class F { has $.x; method mk(\v) { self.bless(x => v) }; method set($n) { $!x = $n; $!x } }
ck(try-set({ F.mk(1).set(5) }), 5, '…and self.bless(x => v)');

# --- an attribute's default -------------------------------------------------
constant K = 6;
class D { has $.x = K; method set($n) { $!x = $n; $!x } }
ck(try-set({ D.new.set(7) }), 7, 'has $.x = CONSTANT stays assignable');

# --- .clone ------------------------------------------------------------------
my $orig = A.new(x => 1);
sub via-clone(\v) { $orig.clone(x => v) }
ck(try-set({ via-clone(8).set(9) }), 9, '.clone(x => v) from a sigilless parameter stays assignable');

# --- attributive parameters --------------------------------------------------
class M { has $.b; method sb($!b) { }; method set($n) { $!b = $n; $!b } }
my $m = M.new; $m.sb(10);
ck(try-set({ $m.set(11) }), 11, 'method sb($!b) leaves the attribute assignable');
class B { has $.q; submethod BUILD(:$!q) { }; method set($n) { $!q = $n; $!q } }
sub via-build(\v) { B.new(:q(v)) }
ck(try-set({ via-build(12).set(13) }), 13, '…and so does BUILD(:$!q) given a sigilless value');

# --- what stays readonly -----------------------------------------------------
my $died = False;
try { A.new(x => 1).x = 5; CATCH { default { $died = True } } }
ck($died, True, 'the public accessor of a non-rw attribute still refuses a write');
sub keep(\v) { A.new(x => v); my $d = False; try { v = 2; CATCH { default { $d = True } } }; $d }
ck(keep(1), True, '…and the sigilless name itself is still readonly');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
