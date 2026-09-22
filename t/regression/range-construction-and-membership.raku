# Regression: eight more places a Range was read through its integer FIELDS
# where only its endpoint OBJECTS knew the answer (Range semantics sheet RG-04,
# RG-08, RG-09, RG-19, RG-26, RG-30, RG-32; S02-types/range.t).
#
#   * `Range.new` did not exist, so every constructed range was
#     X::Method::NotFound.
#   * The element type followed the END: `1..4.9` yielded Nums where the START
#     decides, so it is (1, 2, 3, 4).
#   * `+(1..*)` was 10000 — the bounded prefix an endless range hands out —
#     where numifying one is Inf, and `+(1..NaN)` was a count too.
#   * `int-bounds` had no two-argument form, subtracted for an excluded end even
#     when that end was fractional (`(0..^5.5)` is (0, 5), not (0, 4)), and was
#     X::Method::NotFound on any infinite range.
#   * `2**70 ~~ 1..*` and `(2**64-1) ~~ uint64.Range` were False: the integer
#     fields saturate at the int64 limits, which are also the sentinel for "no
#     end at all", so a bigint compared against 9.2e18 and an unbounded end was
#     read as that same number.
#   * `"raku" ~~ -Inf..Inf` was True, because a string that does not parse
#     numified to 0 instead of not matching.
#   * A native type had no `.Range`.
#   * `eager $range` handed the Range straight back instead of reifying it.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# --- RG-04: Range.new builds what the operators build -----------------------
check Range.new(1, 5).raku,                   '1..5',      'Range.new';
check Range.new(1, 5, :excludes-min).raku,    '1^..5',     'with an excluded min';
check Range.new("a", "z", :excludes-max).raku, '"a"..^"z"', 'and an excluded max';
check Range.new(1, 5, :!excludes-min).raku,   '1..5',      'a negated adverb is no exclusion';
check Range.new(*, 5).raku,                   '-Inf..5',   'a Whatever endpoint';
check Range.new(1, *).infinite,               True,        'is infinite';
check Range.new(5, 1).elems,                  0,           'a reversed range is empty';
check Range.new(1, 3, :excludes-min, :excludes-max).list, (2,), 'both exclusions';
check (try Range.new(1..2, 3)) // $!.^name, 'X::Range::InvalidArg', 'and it refuses what `..` refuses';
check Range.new("1", 5).min.^name, 'Str', 'the endpoint coercions are the same too';

# --- RG-09: the element type follows the START ------------------------------
check (1..4.9).list,   (1, 2, 3, 4),       'an Int start keeps Ints past a Num end';
check (1..^4.1).list,  (1, 2, 3, 4),       'excluded or not';
check (1..3e0).list,   (1, 2, 3),          'and past a Num end that is whole';
check (1..3.0).list,   (1, 2, 3),          'or a Rat one';
check (1.0..3).list,   (1.0, 2.0, 3.0),    'a Rat start keeps Rats';
check (1e0..3e0).list, (1e0, 2e0, 3e0),    'a Num start Nums';
check (1.1..4).list,   (1.1, 2.1, 3.1),    'stepping from the start itself';
check (^5.5).list,     (0, 1, 2, 3, 4, 5), 'and an excluded end only bites on a whole number';

# --- RG-08: numeric context of a range with no count ------------------------
check +(1..*),     Inf, 'an endless range numifies to Inf';
check +(*..5),     Inf, 'from either side';
check +(-∞..-∞),   Inf, 'even when both ends are the same infinity';
check +(∞..∞),     Inf, 'in either direction';
check +(5..-∞),      0, 'but a start after its end is still 0';
check +(1..NaN),   NaN, 'a NaN endpoint numifies to NaN';
check +(NaN..1),   NaN, 'on either side';
check +(1.5..*),   Inf, 'and a fractional start does not change that';
check +(1..5),       5, 'an ordinary range is still its count';
check +(1.5..3.5),   3, 'fractional too';
check +(1^..^10),    8, 'exclusions and all';

# --- RG-26: int-bounds ------------------------------------------------------
check (0..^5.5).int-bounds, (0, 5), 'an excluded FRACTIONAL end keeps its last integer';
check (1..^5.0).int-bounds, (1, 4), 'an excluded whole one does not';
check (0..5.5).int-bounds,  (0, 5), 'an included fractional end';
check ((1..5).int-bounds(my $lo, my $hi)), True, 'the two-argument form answers a Bool';
check ($lo, $hi), (1, 5),                        'and fills its arguments';
check ((1..Inf).int-bounds(my $l2, my $h2)), False, 'False when there are none';
check $l2, Any,                                     'leaving them untouched';
check ((1.5..5).int-bounds(my $l3, my $h3)), False, 'a fractional start has none either';

# --- RG-19/RG-32: membership past the int64 limits --------------------------
check 2**70 ~~ 1..*,              True,  'an endless range holds every number above its start';
check 2**70 ~~ (1..2**80),        True,  'and a bigint end really is that big';
check (2**64-1) ~~ uint64.Range,  True,  'the top of uint64 is inside it';
check 2**64 ~~ uint64.Range,      False, 'and one past it is not';
check Inf ~~ 1..*,                True,  'Inf is above every start';
check -Inf ~~ *..1,               True,  'and -Inf below every end';
check NaN ~~ 1..5,                False, 'NaN is in nothing';
check "raku" ~~ -Inf..Inf,        False, 'a string that does not parse does not match';
check "raku" ~~ 1..*,             False, 'however open the range';
check "42" ~~ 20..50,             True,  'one that parses matches numerically';
check "0x10" ~~ 10..20,           True,  'in any notation';

# --- RG-32: a native type's Range -------------------------------------------
check int8.Range.raku,   '-128..127',   'int8';
check uint8.Range.raku,  '0..255',      'uint8';
check byte.Range.raku,   '0..255',      'byte';
check int16.Range.raku,  '-32768..32767', 'int16';
check uint32.Range.raku, '0..4294967295', 'uint32';
check uint64.Range.raku, '0..18446744073709551615', 'uint64';
check int.Range.raku, '-9223372036854775808..9223372036854775807', 'int is 64 bits';
check atomicint.Range.raku, '-9223372036854775808..9223372036854775807', 'and so is atomicint';
check num.Range.raku,    '-Inf..Inf',   'a native float spans everything';
check int32.Range.int-bounds, (-2147483648, 2147483647), 'and the bounds are exact';
check int8.Range.elems,  256,           'the span counts';
check Bool.Range.raku,   '-Inf^..^Inf', 'Bool.Range is Int.Range';
check Rational.Range.raku, '-Inf..Inf', 'and Rational.Range is Rat.Range';

# --- RG-30: eager reifies ---------------------------------------------------
check (eager (^10+5)/2), (2.5, 3.5, 4.5, 5.5, 6.5), 'eager on a Range gives its elements';
check (eager 1..3),      (1, 2, 3),                 'an Int range too';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
