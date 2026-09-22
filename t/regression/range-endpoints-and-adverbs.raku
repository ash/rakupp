# Regression: what a Range's endpoints are, and what min/max answer about them.
#
# Five separate wrong answers, all from reading a Range's INTEGER FIELDS when
# the endpoints were not integers (Range semantics sheet, RG-03, RG-12, RG-26,
# RG-27; S02-types/range.t and S07-iterators/range-iterator.t):
#
#   * `.min(:k)` and friends were ignored, so `(2..6).min(:k)` was 2 — the
#     endpoint — instead of 0, its index.
#   * `"1"..9` numified the string and iterated integers; a Str on the LEFT
#     coerces nothing, so the elements are the strings "1" through "9".
#   * `1 .. "5"` did the opposite and kept the string; a Real on the left DOES
#     coerce the right.
#   * `(1.5..*)` and `("d"..*)` walked the whole numbers and the codepoints
#     around their start rather than stepping from it.
#   * a range starting at -Inf, Inf or NaN read the saturated int64 limits and
#     counted up from them.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# --- RG-27: min/max answer about the ENDPOINT, keyed by its index -----------
my $r = 2..6;
check $r.min(:k),   0,              'min keys on 0';
check $r.min(:kv),  (0, 2),         'and pairs that key with the endpoint';
check $r.min(:p),   Pair.new(0, 2), 'as a Pair too';
check $r.min(:!k),  2,              'a negated adverb asks for the plain answer';
check $r.max(:k),   4,              'max keys on the index of the LAST ELEMENT';
check $r.max(:kv),  (4, 6),         'paired with the raw endpoint';
check $r.max(:p),   Pair.new(4, 6), 'as a Pair';
check min($r, :k),  0,              'the sub form routes to the same rule';
check max($r, :p),  Pair.new(4, 6), 'in both directions';
# the index and the value need not name the same element: neither 2 nor 6 is
# in `2^..^6`, and `(1.5..3)` has 2.5 at index 1
check (2^..^6).max(:kv), (2, 6),    'an excluded end keeps the raw endpoint';
check (1.5..3).max(:kv), (1, 3),    'and so does a fractional one';
check (2..Inf).max(:kv), (Inf, Inf), 'an endless range keys on Inf';
check (1..0).max(:k),   -1,         'an empty one on -1';
check ("a".."c").max(:kv), (2, "c"), 'a string range answers the same way';
# `:by` is a comparator, not an adverb — it goes to the elements
check (2..6).min(:by(-*)), 6,       'a :by comparator still reaches the list';
check (2..6).max(:by(-*)), 2,       'in both directions';

# --- RG-26: minmax reads the endpoints, not the fields ----------------------
check (3.5..4.5).minmax,  (3.5, 4.5),   'minmax of a Rat range keeps its Rats';
check ("a".."z").minmax,  ("a", "z"),   'a string range its strings';
check (-Inf..Inf).minmax, (-Inf, Inf),  'and an infinite one its infinities';
check (^10).minmax,       (0, 9),       'an Int range is its int-bounds';
check (1^..^5).minmax,    (2, 4),       'exclusions and all';

# --- RG-03: which side coerces which ----------------------------------------
check ("1"..9).list,    ("1","2","3","4","5","6","7","8","9"), 'a Str left end keeps strings';
check ("1"..9).min.^name, 'Str',        'so .min is a Str';
check ("a"..5).elems,   0,              '"a" is after "5" in string order';
check ("abc"..5).elems, 0,              'and so is "abc"';
check (1 .. "5").max.^name, 'Int',      'a Real left end coerces the right';
check +(1 .. "10"),     10,             'through the numeric value, not the text';
check (try (1 .. "b").elems) // $!.^name, 'X::Str::Numeric', 'an unparsable one throws';

# --- RG-12: an endless range with a non-integer start -----------------------
check (1.5..*).head(4).List,  (1.5, 2.5, 3.5, 4.5), 'a fractional start steps from itself';
check (1e0..*).head(3).List,  (1e0, 2e0, 3e0),      'a Num start stays Num';
check (1..*).head(3).List,    (1, 2, 3),            'and an Int start is untouched';
my $it = ("d"..*).iterator;
check ($it.pull-one, $it.pull-one, $it.pull-one), ("d", "e", "f"),
      'an endless string range pulls strings, not codepoints';
my $fi = (-1.5..*).iterator;
check $fi.pull-one, -1.5, 'and an endless fractional one pulls fractions';

# --- RG-12: -Inf, Inf and NaN as the START ----------------------------------
check (Inf..Inf)[^3],     (Nil, Nil, Nil), 'a range starting at Inf is empty';
check (Inf..0).elems,     0,               'however it ends';
check (-Inf..0).head(3).List,  (-Inf, -Inf, -Inf), '-Inf repeats for ever';
check (NaN..NaN).head(3).List, (NaN, NaN, NaN),    'and so does NaN when the end is NaN';
check (NaN..1).head(2).List,   (),                 'but a NaN start with any other end is empty';
check (1..NaN).head(3).List,   (1, 2, 3),          'a FINITE start walks past a NaN end';
# and the same rule survives a thousand steps: this is the drift Roast counts
check (-Inf..0).list[1051], -Inf, 'still -Inf after a thousand steps';

# --- RG-26: there are no integer bounds without integer endpoints -----------
check (0..5.5).int-bounds, (0, 5),  'a fractional END still has them';
check (1.5..3).int-bounds.exception.^name, 'X::AdHoc', 'a fractional START does not';
check (1..Inf).int-bounds.exception.^name, 'X::AdHoc', 'nor does an infinite end';
check ("a".."c").int-bounds.exception.^name, 'X::AdHoc', 'nor a string range';

# --- RG-18: a Range is immutable --------------------------------------------
for <push pop shift unshift append prepend> -> $m {
    my $e = (try { (1..5)."$m"(42) }) // $!;
    check $e.^name, 'X::Immutable', "$m is refused";
    check $e.typename, 'Range', "$m names the type";
    check $e.method, $m, "$m names itself";
}

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
