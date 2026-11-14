# An attribute in a parenthesised `has (…)` list takes its own traits before
# its default, as in Rakudo (rakudo#2078): `has ($.z is rw = 42)` lost both the
# `is rw` and the 42, its tokens skipped one by one as unknown.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

check class { has ($.z is rw = 42); }.new.z, '42', 'a trait, then a default (rakudo#2078)';
my $o = class { has ($.z is rw) }.new;
$o.z = 5;
check $o.z, '5', '`is rw` makes the accessor writable';
check class { has ($.y is rw, $.w = 3) }.new.w, '3', 'the next item after a trait';
check class { has ($.d is default(7)) }.new.d, '7', '`is default(…)`';
class R { has ($.r is required) }
check (try { R.new; 'built' }) // $!.^name, 'X::Attribute::Required', '`is required`';
class C { has ($.a, $.b) is rw }
my $c = C.new;
$c.b = 9;
check $c.b, '9', 'a trait after the list, as before';
check class { has ($.a = 1, $.b = 2) }.new.b, '2', 'defaults alone, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
