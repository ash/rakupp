# Regression: a coercion type as a CONTAINER's element type.
#
# `my Int() $x = "42"` worked; `my Int() @a` and `my Int() %h` did not, because
# the coercion was applied to the right-hand side WHOLE instead of to each
# element. `my Int() @b = "3", "4"` answered [2] — the list numified to its own
# element count — and `my Str() %s = a => 42` left a bare Str type object in
# the slot.
#
# Separately, a Map is not a Hash. Both are one representation here, so the
# nominal check called a Map a Hash and `Hash()` never ran: Text::Table::Simple
# opens with `my Hash() %options = %defaults` over a Map of Maps, and every
# value stayed an immutable Map.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

# An ARRAY coerces each element, not the list.
my Int() @a = "3", "4";
check @a.elems, 2,      'an Int() array keeps both elements';
check @a.List,  (3, 4), 'and coerces each one';
my Str() @b = 1, 2;
check @b.List, ("1", "2"), 'a Str() array stringifies each element';

# A HASH coerces each value, and leaves the keys alone.
my Str() %s = a => 42, b => 7;
check %s<a>, "42",        'an Str() hash value is the coerced string';
check %s<b>, "7",         'for every key';
check %s.keys.sort.List, ("a", "b"), 'and the keys are untouched';
my Int() %i = k => "17";
check %i<k>, 17, 'an Int() hash value numifies';

# Hash() over a Map: the values come out mutable Hashes.
my %defaults is Map =
    rows    => Map.new('column_separator', '|'),
    headers => Map.new('corner_marker', 'O'),
;
my Hash() %options = %defaults;
check %options<headers>.WHAT.^name, 'Hash', 'Hash() turns a Map value into a Hash';
check %options<rows>.WHAT.^name,    'Hash', 'every value, not just the first';
check %options<headers><corner_marker>, 'O', 'carrying its contents across';
%options<headers><corner_marker> = '+';
check %options<headers><corner_marker>, '+', 'and the result is mutable';
check %defaults<headers><corner_marker>, 'O', 'without writing through to the Map';

# A scalar Hash() does the same.
my Hash() $h = Map.new('y', 2);
check $h.WHAT.^name, 'Hash', 'a scalar Hash() coerces a Map too';
check $h<y>, 2, 'keeping the pair';

# The scalar coercions that already worked must keep working.
my Str() $x = 99;
check $x, "99", 'a scalar Str() still coerces';
my Int() $n = "8";
check $n, 8, 'and a scalar Int()';

# A plain typed container still CHECKS rather than coerces.
my Int @plain = 1, 2;
check @plain.List, (1, 2), 'an untyped-coercion Int array is unchanged';
check (try { my Int @bad = "x"; 'no throw' } // 'threw'), 'threw',
      'and still rejects a Str element';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
