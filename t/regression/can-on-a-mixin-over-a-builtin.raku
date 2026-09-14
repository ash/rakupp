# Regression: `.can` on a MIXIN over a built-in value sees the built-in's
# methods. `Date.new(…) does Role` is an object whose class chain ends at Date,
# and `.can('day-of-week')` on it answered an empty list — the role's own
# methods were found, the Date's were not, because nothing in the lookup
# walked past the boundary into the boxed value. Date::Calendar::Strftime is
# used exactly this way and gates `%u` and `%V` on that `.can`, so both came
# out as the literal specifier where Rakudo prints the day and the week.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

role Stamp { method stamp { 'stamped' } }
my $plain = Date.new('2026-09-14');
my $mixed = Date.new('2026-09-14') does Stamp;

ck($mixed.can('stamp').elems > 0,       True,  'the role method answers .can');
ck($mixed.can('day-of-week').elems > 0, True,  'and so does a built-in Date method');
ck($mixed.can('week-number').elems > 0, True,  'another one');
ck($mixed.can('no-such-method').elems,  0,     'a name neither has is still empty');
ck($plain.can('day-of-week').elems > 0, True,  'the plain Date answers as before');

# what .can hands back is callable, and calls the real method
ck($mixed.can('day-of-week')[0]($mixed), 1,    'the returned method runs on the mixin');
ck($mixed.day-of-week,                    1,    'as the direct call does');

# the same over other built-ins
role Tag { method tag { 't' } }
my $str = 'hello' does Tag;
ck($str.can('uc').elems > 0,  True, 'a Str mixin sees .uc');
ck($str.can('tag').elems > 0, True, 'and its own role method');
my $int = 42 does Tag;
ck($int.can('is-prime').elems > 0, True, 'an Int mixin sees .is-prime');

# the shape Date::Calendar::Strftime has: a thunk that gates on .can
sub via-gate($date) {
    my %f = u => -> { if $date.can('day-of-week') { sprintf('%d', $date.day-of-week) } else { Nil } };
    my $fnc = &%f<u>;
    $fnc()
}
ck(via-gate($mixed), '1', 'a .can-gated thunk yields the value on the mixin');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
