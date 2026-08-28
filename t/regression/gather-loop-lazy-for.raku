# `for` over an unfinished `gather` iterates it LIVE, one element at a time,
# and a gather whose first take lands after the probe budget still counts as
# probed.
#
# Two faults, both reached by `use Digest::SHA3; sha3_256('hello world')`,
# which did not finish in five minutes while md5/sha1/sha256 were instant.
#
# 1. A `gather { loop { take … } }` is a block that has not said it is done. It
#    is not marked `infinite` (nothing declared it so), and drainIfFiniteLazy
#    read "not infinite" as "finite": it ran the generator to the
#    million-element cap to find out where it ended, before the loop body saw
#    its first element. SHA-3's squeeze phase is
#
#        gather loop { take $state.subbuf(0, $rateInBytes); $state .= &KeccakF1600 }
#
#    — one Keccak permutation per take, ~12 ms — and its consumer takes ONE
#    element and stops. Such a gather now iterates live, as an endless source
#    does; one that reaches its end still says so, and whole-list consumers and
#    LAST phasers keep working.
#
# 2. The probe's TIME budget stops a block at the first take past it, so that a
#    generator whose takes get steadily more expensive cannot make merely
#    declaring it slow. "The first take PAST the budget" has to mean the second
#    take onwards — the budget can be spent before the block takes anything at
#    all, here by the list expression, which builds and probes the inner gather.
#    The "have we collected anything yet" guard was read AFTER this take's own
#    push, so it was true on the very first take: such a gather was declared
#    lazy having probed nothing. A lazy gather is RE-RUN when something pulls
#    on it, and re-running a block whose list expression is `samewith …` cannot
#    work — the dispatcher it needs is gone — so with (1) fixed, SHA-3 failed
#    with "Cannot resolve caller sha3_256()" instead of hanging.
#
# The sources below are produced by SUB CALLS rather than bound to `$`
# variables: `for $bound-seq { … }` is a separate known divergence (rakupp
# treats a `$`-sigil source as one item even when it was bound with `:=`,
# where Rakudo iterates it), and this file must not depend on it either way.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

# --- the shape that hung: an endless gather, a consumer that wants two --------

my $produced = 0;
sub endless() { gather loop { take $produced++ } }

my @seen;
for endless() {
    @seen.push($_);
    last if @seen == 2;
}
check @seen.List, (0, 1), 'for over an endless gather sees its elements in order';
# The generator may run a small probe beyond what was consumed; a drain to the
# million-element cap is what this pins against.
check ($produced < 1000), True, "an endless gather is not drained ($produced takes for 2 elements)";

# --- an expensive generator: the Keccak shape, without the crypto ------------

my $work = 0;
sub costly() { $work++; $work * 10 }
sub squeeze() { gather loop { take costly() } }

my $first = 0;
for squeeze() { $first = $_; last }
check $first, 10, 'the first element of an expensive gather is the first take';
check ($work < 1000), True, "an expensive gather is not drained either ($work takes)";

# --- a FINITE gather still behaves like a finite list ------------------------

sub finite() { gather for ^200 { take $_ * 2 } }
check finite().elems, 200, 'a finite gather past the probe still knows its length';
check finite()[199], 398, 'a finite gather past the probe indexes to its end';

my @all;
for finite() { @all.push($_) }
check @all.elems, 200, 'for over a finite gather visits every element';
check (@all[0], @all[*-1]), (0, 398), 'for over a finite gather visits them in order';

# --- LAST still fires on the real last element of a finite gather ------------

sub hundred() { gather for ^100 { take $_ } }
my $last-seen = -1;
my $iterations = 0;
for hundred() {
    $iterations++;
    LAST { $last-seen = $_ }
}
check $iterations, 100, 'a finite gather past the probe runs the body once per element';
check $last-seen, 99, 'LAST fires on the last element of a finite gather';
# (A `last` statement and a LAST phaser in one block are not tested together:
# Rakudo 2026.08 fails to compile that shape — "QAST::Block … has not
# appeared" — and this file has to run under both engines.)

# --- a gather whose SETUP is slower than the probe budget --------------------

sub slow-setup() {           # ~40 ms, comfortably past the 20 ms probe budget
    my $t = now;
    my $x = 0;
    $x++ while now - $t < 0.04;
    (1, 2, 3)
}
sub after-slow-setup() { gather for slow-setup() { take $_ * 7 } }
check after-slow-setup().List, (7, 14, 21),
      'a gather whose first take is already past the probe budget still probes';

# …and the consequence that made SHA-3 fail rather than hang: a block declared
# lazy is re-run, and a block that dispatches with `samewith` cannot be.
proto stream($) {*}
multi stream(Int $n) { gather for stream("go") { take $_ * 2; last } }
multi stream(Str $s) { gather loop { take 21 } }
check stream(1).List, (42,), 'a gather block calling samewith is not re-run outside its dispatcher';

# --- the two-layer shape Keccak actually has --------------------------------

my $inner-takes = 0;
sub blocks() { gather loop { take ++$inner-takes } }
sub outer() { gather for blocks() { take $_ * 100; last } }
check outer().List, (100,), 'a gather consuming an endless gather takes what it needs';
check ($inner-takes < 1000), True, "…and the inner one stays lazy ($inner-takes takes)";

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
