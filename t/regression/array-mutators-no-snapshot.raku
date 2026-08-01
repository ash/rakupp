# Regression: Array mutators that write through the handle (push/append/
# unshift/prepend/pop/shift/splice) must not pay methodCallTail's O(n)
# whole-array snapshot (`ValueList items = toList(inv)`) — that copy made every
# accumulate loop QUADRATIC: cognates' build-db did 48k pushes onto one array
# ≈ 1.15 billion Value copies, ~130 of its 197 seconds. The snapshot now skips
# the through-the-handle arms (Array only; Hash.push and lazy arrays keep it).
# Also pins the cooperative `when` (given/when match no longer throws
# BreakGivenEx through the macOS unwinder when frames line up) — the semantics
# the fast path must preserve, plus a wall-clock bound on the hot shape.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. accumulate loop: 60k pushes. Quadratic = minutes; linear ≈ 0.1 s.
#
# The wall-clock bounds here and in §4 are QUADRATIC DETECTORS, not performance
# assertions — the regression they exist for turned ~0.1 s into ~130 s. They are
# therefore set far above the linear cost, because a shared CI runner is several
# times slower than a developer machine and a bound that merely doubles the
# local time is a flake generator: §4's was 10 s against a 4.4 s local baseline,
# and it failed three CI runs in a row at 10.4, 10.6 and 10.8 s while the code it
# guards was provably unchanged (an older snapshot measured identically).
# If you tighten these, measure both machines first.
my $t0 = now;
my @acc;
@acc.push($_) for ^60_000;
my $push-s = now - $t0;
@fail.push("push result: {@acc.elems}/{@acc[*-1]}") unless @acc.elems == 60_000 && @acc[*-1] == 59_999;
@fail.push("push too slow: {$push-s.round(0.1)} s") if $push-s > 30;   # linear ≈ 0.1 s

# 2. mutator semantics survive the snapshot skip
my @m = 1, 2;
@m.push(3, 4);            @fail.push("push: @m[]")    unless @m eqv [1,2,3,4];
@m.append([5, 6]);        @fail.push("append: @m[]")  unless @m eqv [1,2,3,4,5,6];
@m.unshift(0);            @fail.push("unshift: @m[]") unless @m eqv [0,1,2,3,4,5,6];
@m.prepend([-1]);         @fail.push("prepend: @m[]") unless @m eqv [-1,0,1,2,3,4,5,6];
@fail.push('pop')   unless @m.pop == 6;
@fail.push('shift') unless @m.shift == -1;
my @cut = @m.splice(1, 2);
@fail.push("splice: @cut[] / @m[]") unless @cut eqv [1,2] && @m eqv [0,3,4,5];
my uint8 @nat; @nat.push(300);
@fail.push("native wrap: @nat[0]") unless @nat[0] == 44;   # 300 mod 256
# (NOT asserted: `my @L := (1,2,3); @L.push(4)` — Rakudo dies "Cannot resize an
# immutable List", rakupp allows it. A pre-existing divergence, older than the
# snapshot skip; left open, recorded here so nobody blames the fast path.)

# 3. cooperative when/given: every semantic the fast path must keep
my $v1 = do given 5  { when 5 { 'five' }; when Int { 'int' } };
@fail.push("when order: $v1") unless $v1 eq 'five';
my $v2 = do given 'x' { when 'y' { 1 }; when 'x' { 2 }; default { 3 } };
@fail.push("when value: $v2") unless $v2 == 2;
my @seen;
for 1, 2, 3 { when 2 { next }; @seen.push($_) }
@fail.push("when+next: @seen[]") unless @seen eqv [1, 3];
my $reached = 'no';
given 7 { when Int { succeed }; $reached = 'yes' }
@fail.push('succeed must exit the given') if $reached eq 'yes';
$_ = 4;
my $v3 = do given $_ { when 4 { 'matched' } };
@fail.push("topical given: $v3") unless $v3 eq 'matched';
# proceed falls through to the NEXT when
my $v4 = do given 10 { when Int { proceed }; when 10 { 'second' }; default { 'def' } };
@fail.push("proceed: $v4") unless $v4 eq 'second';
# when behind a closure boundary still works (exception path)
my $v5 = do given 8 { (1..1).map({ when 8 { } }); 'after-map' };
@fail.push("closure when: $v5") unless $v5 eq 'after-map';

# 4. hot shape wall-clock: given/when dispatch per iteration (the bind loop
#    shape). 100k iterations threw 100k BreakGivenEx before — seconds of
#    unwinder time on macOS; cooperative it is milliseconds.
$t0 = now;
my $n = 0;
for ^100_000 {
    given $_ % 3 {
        when 0  { $n++ }
        when 1  { $n += 2 }
        default { $n += 3 }
    }
}
my $when-s = now - $t0;
# ^100_000 by residue: 33334 zeros ×1 + 33333 ones ×2 + 33333 twos ×3
@fail.push("when loop result: $n") unless $n == 199_999;
@fail.push("when loop too slow: {$when-s.round(0.1)} s") if $when-s > 60;  # linear ≈ 4.4 s

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
