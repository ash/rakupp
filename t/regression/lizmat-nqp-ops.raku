# Regression: the nqp-op and definedness batch found by putting lizmat's 262
# distributions through `rakupp test`. Eleven of her dists could not run a line
# because `nqp::div_i` did not exist, and the blocker they sit behind
# (Array::Sorted::Util) gates ten more.
#
#   * the whole bignum `_I` family and the integer/list/system leaves beside it
#     were missing: div_i, div_I, mul_I, sub_I, mod_I, the six `is*_I`
#     comparisons, cmp_I, neg_I, abs_I, pow_I, gcd_I, lcm_I, the five bitwise
#     `_I` forms, isbig_I, tostr_I, fromstr_I, box_i, box_n, sqrt_n, isfalse,
#     pop, print, say, time, readlink, repeat_while and repeat_until
#   * `nqp::mod_i` was FLOORED and MoarVM truncates it — mod_i(-7,2) is -1
#   * `nqp::box_i($n, SomeType)` ignored its TYPE argument, so a class whose
#     whole purpose is to answer `.defined` came back as a plain Int
#   * `with` / `without` / `//` / `orelse` ignored a user `method defined`
#   * every nqp LIST op did nothing at all to an IterationBuffer, and a
#     positional subscript on one read Any
#
# MoarVM is NOT self-consistent about division, and that is the point of the
# first block: div_i FLOORS while mod_i TRUNCATES, and mod_I floors again.
# Every expectation here was taken from Rakudo 2026.08, and this file runs
# clean under Rakudo too — which is the only thing that makes it worth having.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- division and remainder: the two disagree on purpose ----------------
ck nqp::div_i(7, 2),   3,  'div_i rounds a positive quotient down';
ck nqp::div_i(-7, 2), -4,  'div_i FLOORS a negative dividend (truncating gives -3)';
ck nqp::div_i(7, -2), -4,  'div_i floors a negative divisor too';
ck nqp::div_i(-7, -2), 3,  'div_i of two negatives is positive';
ck nqp::mod_i(-7, 2), -1,  'mod_i TRUNCATES — the sign follows the dividend, unlike div_i';
ck nqp::mod_i(7, -2),  1,  'mod_i ignores the divisor sign';
ck nqp::mod_i(-1, 7), -1,  'mod_i of a small negative stays negative';
ck (try { nqp::div_i(1, 0) } // 'threw'), 'threw', 'div_i by zero throws';
ck (try { nqp::mod_i(1, 0) } // 'threw'), 'threw', 'mod_i by zero throws too, with its own wording';
ck nqp::mod_I(nqp::decont(1), nqp::decont(0), Int), 1,
   'but the BIGNUM mod by zero answers the dividend — libtommath, not Raku`s `%`';

# ---- the bignum `_I` family --------------------------------------------
# Operands wider than a native int on purpose: a truncating implementation
# would answer something plausible for small ones.
my $big := nqp::decont(10 ** 25);
my $sev := nqp::decont(7);
ck nqp::mul_I($big, $sev, Int), 70000000000000000000000000, 'mul_I keeps full width';
ck nqp::sub_I($big, $sev, Int),  9999999999999999999999993, 'sub_I keeps full width';
ck nqp::div_I(nqp::decont(-7), nqp::decont(2), Int), -4, 'div_I floors like div_i';
ck nqp::mod_I(nqp::decont(-7), nqp::decont(2), Int),  1, 'mod_I FLOORS, unlike mod_i';
ck nqp::iseq_I($big, $big), 1, 'iseq_I';
ck nqp::isne_I($big, $sev), 1, 'isne_I';
ck nqp::islt_I($sev, $big), 1, 'islt_I';
ck nqp::isle_I($sev, $big), 1, 'isle_I';
ck nqp::isge_I($sev, $big), 0, 'isge_I';
ck nqp::isgt_I($sev, $big), 0, 'isgt_I';
ck nqp::cmp_I($sev, $big), -1, 'cmp_I answers -1/0/1';
ck nqp::neg_I($sev, Int), -7, 'neg_I';
ck nqp::abs_I(nqp::decont(-7), Int), 7, 'abs_I';
ck nqp::pow_I(nqp::decont(2), nqp::decont(10), Num, Int), 1024, 'pow_I';
ck nqp::gcd_I(nqp::decont(12), nqp::decont(18), Int), 6, 'gcd_I';
ck nqp::lcm_I(nqp::decont(4), nqp::decont(6), Int), 12, 'lcm_I';
ck nqp::bitand_I(nqp::decont(12), nqp::decont(10), Int), 8, 'bitand_I';
ck nqp::bitor_I(nqp::decont(12), nqp::decont(10), Int), 14, 'bitor_I';
ck nqp::bitxor_I(nqp::decont(12), nqp::decont(10), Int), 6, 'bitxor_I';
ck nqp::bitshiftl_I(nqp::decont(1), 10, Int), 1024, 'bitshiftl_I';
ck nqp::bitshiftr_I(nqp::decont(1024), 3, Int), 128, 'bitshiftr_I';
ck nqp::isbig_I($big), 1, 'isbig_I says a wide value is wide';
ck nqp::isbig_I($sev), 0, 'isbig_I says a native-width value is not';
ck nqp::tostr_I($big), '10000000000000000000000000', 'tostr_I';
ck nqp::fromstr_I('123456789012345678901234567890', Int),
   123456789012345678901234567890, 'fromstr_I reads back a value no native int holds';

# ---- the small leaves ---------------------------------------------------
ck nqp::isfalse(0), 1, 'isfalse of 0';
ck nqp::isfalse(1), 0, 'isfalse of 1';
ck nqp::sqrt_n(16e0), 4e0, 'sqrt_n';
my $l := nqp::list(1, 2, 3);
ck nqp::pop($l), 3, 'pop answers the last element';
ck nqp::elems($l), 2, '…and the list is one shorter';
ck (nqp::time() > 1_000_000_000_000_000_000), True, 'time answers NANOSECONDS, not seconds';

# repeat_until runs its body BEFORE the first test — that is the whole
# difference from nqp::until, so the guard is true from the start here.
my $ran = 0;
nqp::repeat_until(1, ($ran = $ran + 1));
ck $ran, 1, 'repeat_until runs the body once even when the test is true at entry';
my $count = 0;
nqp::repeat_until(nqp::isge_i($count, 3), ($count = $count + 1));
ck $count, 3, 'repeat_until then keeps going until the test holds';

# nqp::print / nqp::say write to the VM's OWN stdout and do NOT consult $*OUT
# — a block that rebinds $*OUT captures `say` and does not capture `nqp::say`.
# Routing them through $*OUT would be more useful and less true, so the two
# halves are asserted together.
my $sink = class { has @.lines; method print($s) { @!lines.push($s); True } }.new;
{
    my $*OUT = $sink;
    nqp::say("via-nqp-say");
    nqp::print("via-nqp-print");
    say "via-raku-say";
}
ck $sink.lines, ["via-raku-say\n"],
   'nqp::say and nqp::print bypass a rebound $*OUT, while `say` does not';

# ---- box_i honours its TYPE argument ------------------------------------
# `class NotFound is Int { method defined(--> False) { } }` is exactly
# Array::Sorted::Util's not-found signal: the value is the position, and the
# UNDEFINEDNESS is the answer. A plain Int here is the wrong answer twice over.
my class NotFound is Int { method defined(--> False) { } }
my $nf := nqp::box_i(3, NotFound);
ck $nf.^name, 'NotFound', 'box_i builds the type it was handed';
ck $nf.defined, False, '…and that type answers its own .defined';
# `.Int` on an Int-derived class is IDENTITY, so it answers a NotFound and not
# an Int — unbox to ask for the value itself.
ck nqp::unbox_i($nf), 3, '…while still carrying the native value';
ck ($nf + 1), 4, '…and numifying as it';
ck nqp::box_i($nf, Int), 3, 'boxing it back into a plain Int reads the value out';
ck nqp::box_i(42, Int).^name, 'Int', 'a core type argument still gives a plain Int';
ck nqp::box_n(1.5e0, Num), 1.5e0, 'box_n';

# ---- with / without / // / orelse ask `.defined` ------------------------
# Rakudo routes all four through the METHOD. `so` and `.DEFINITE` stay on the
# representation, and that half matters as much: routing them too would break
# every object that merely reports itself absent.
my $w = 'skipped'; with $nf { $w = 'ran' }
ck $w, 'skipped', '`with` honours a user-declared .defined';
my $o = 'skipped'; without $nf { $o = 'ran' }
ck $o, 'ran', '`without` honours it too';
ck ($nf // 'fallback'), 'fallback', '`//` honours it';
ck ($nf orelse 'fallback'), 'fallback', '`orelse` honours it';
ck (so $nf), True, '`so` does NOT — truthiness is not definedness';
ck $nf.DEFINITE, True, '`.DEFINITE` does NOT — the object is concrete';

# an ordinary object with no override is unaffected
my class Plain { }
my $p = Plain.new;
my $pw = 'skipped'; with $p { $pw = 'ran' }
ck $pw, 'ran', 'an object declaring no .defined is still defined';
ck (Int // 'fallback'), 'fallback', 'a type object is still undefined';
ck (0 // 'fallback'), 0, '…and a defined falsy value is still defined';

# ---- IterationBuffer: subscripts and the nqp list ops -------------------
my $buf := IterationBuffer.new;
$buf.push($_) for <a c d>;
ck $buf[1], 'c', 'a positional subscript on an IterationBuffer reads its element';
ck $buf[*-1], 'd', '…including from the end';
ck $buf[1, 2].List, ('c', 'd'), '…and a slice answers a list';
nqp::splice($buf, nqp::list('b'), 1, 0);
ck $buf.List, ('a', 'b', 'c', 'd'), 'nqp::splice inserts INTO the buffer the caller holds';
ck nqp::elems($buf), 4, 'nqp::elems counts the buffer';
ck nqp::atpos($buf, 2), 'c', 'nqp::atpos reads through it';
nqp::push($buf, 'z');
ck $buf.List, ('a', 'b', 'c', 'd', 'z'), 'nqp::push appends to it';
ck nqp::pop($buf), 'z', 'nqp::pop takes from it';
nqp::splice($buf, nqp::list(), 0, 1);
ck $buf.List, ('b', 'c', 'd'), 'nqp::splice deletes from it';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
