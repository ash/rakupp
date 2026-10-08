# Regression: a `++`/`--` into an element of a Pair, a List or a Range, and the
# container a subscript write vivifies in an empty `$` scalar.
#
#   * The interpreter refused `(a => 1)<a> = 9` but let `(a => 1)<a>++` step
#     the value in place — the Pair became `:a(2)`, and a container-backed one
#     stepped the variable behind it. Same for a List and a Range element.
#     Rakudo's `++` binds its argument `is rw`, so it matches no candidate:
#     X::Multi::NoMatch, where `=` is X::Assignment::RO.
#   * Natively compiled, a write into a List element went through, and one into
#     a Range REPLACED the Range with a fresh Array.
#   * Natively compiled, `my $u; $u<k> = 3` left `$u` holding a bare Hash:
#     `.raku` said `{:k(3)}` where Rakudo says `${:k(3)}`.
#
# Written so the native backend compiles it (no declaration inside a `try`), so
# the --exe gate exercises the native paths. Rakudo passes every check.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}
sub throws(&c, $type, $desc) {
    my $got = (try { c(); 'no throw' }) // $!.^name;
    @fail.push("$desc: got $got, want $type") unless $got eq $type;
}

# --- a step into a Pair ------------------------------------------------------
my $p = (a => 1);
throws { $p<a>++ }, 'X::Multi::NoMatch', 'postfix ++ into a Pair';
throws { --$p<a> }, 'X::Multi::NoMatch', 'prefix -- into a Pair';
my $old;
throws { $old = $p<a>++ }, 'X::Multi::NoMatch', 'postfix ++ into a Pair, its value used';
check $p, (a => 1), 'the Pair is unchanged';
throws { $p<a> += 2 }, 'X::Assignment::RO', '…and an op-assign is still X::Assignment::RO';

my $c = 1;
my $q = (a => $c);
throws { $q<a>++ }, 'X::Multi::NoMatch', 'a container-backed Pair refuses the step too';
check $c, 1, '…and the variable behind it is unchanged';

# --- …into a List and a Range --------------------------------------------------
my $l = (1, 2);
throws { $l[0]++ },    'X::Multi::NoMatch', 'postfix ++ into a List';
throws { $l[0] = 5 },  'X::Assignment::RO', 'assigning into a List';
check $l, (1, 2), 'the List is unchanged';

my $r = 1..3;
throws { $r[0]++ },    'X::Multi::NoMatch', 'postfix ++ into a Range';
throws { $r[0] = 5 },  'X::Assignment::RO', 'assigning into a Range';
check $r, 1..3, 'the Range is unchanged';

# --- mutable places still step ----------------------------------------------
my @a = 1, 2;
@a[0]++;
check @a, [2, 2], 'an Array element steps';
my %h = a => 1;
%h<a>++;
check %h, {a => 2}, 'a Hash element steps';
my $arr = [1, 2];
$arr[1]++;
check $arr, [1, 3], 'an element of an Array in a scalar steps';
my @rev = @a.reverse;
@rev[0] = 7;
check @rev, [7, 2], 'an Array made from a list takes a write';

# --- what a subscript write vivifies is itemized ----------------------------
my $u;
$u<k> = 3;
check $u.raku, '${:k(3)}', 'a vivified Hash in a scalar is itemized';
my $w;
$w[1] = 3;
check $w.raku, '$[Any, 3]', 'a vivified Array in a scalar is itemized';
my $n;
$n<x><y> = 5;
check $n.raku, '${:x(${:y(5)})}', '…at every level';
my $times = 0;
for $u { $times++ }
check $times, 1, 'a vivified Hash in a scalar iterates as one item';

if @fail { .say for @fail; say "{+@fail} check(s) failed"; exit 1 }
say 'PASS';
