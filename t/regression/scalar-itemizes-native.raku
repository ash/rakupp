# Regression: a `$` container ITEMIZES the list it holds, in natively compiled
# code as in the interpreter. `--exe` stored the Array, Hash or List bare, so
# `my $a = [1, 2]; for $a { … }` ran twice where Rakudo runs once, and `.raku`
# printed `[1, 2]` where Rakudo prints `$[1, 2]`.
#
# Written so the native backend compiles it (no declaration inside a `try`, no
# list literal holding a variable that is written through), so the --exe gate
# exercises the native assignment. Rakudo passes every check.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $a = [1, 2];
my $h = {a => 1};
my $l = (1, 2);
my $r = 1..3;
my %m = x => 1;
my $mh = %m;
my @arr = 3, 4;
my $ma = @arr;

check $a.raku,  '$[1, 2]',  'an Array in a scalar';
check $h.raku,  '${:a(1)}', 'a Hash in a scalar';
check $l.raku,  '$(1, 2)',  'a List in a scalar';
check $mh.raku, '${:x(1)}', 'a hash variable copied into a scalar';
check $ma.raku, '$[3, 4]',  'an array variable copied into a scalar';

my $times = 0;
for $a { $times++ }
check $times, 1, 'a for over an itemized Array runs once';
$times = 0;
for @$a { $times++ }
check $times, 2, '…and over its elements twice';
my @f = $a, 3;
check @f.elems, 2, 'an itemized Array is one element of a list';
my @g = $h, 3;
check @g.elems, 2, '…as is a Hash';
my @k = $l, 3;
check @k.elems, 2, '…and a List';
my @rr = $r, 4;
check @rr.elems, 2, '…and a Range';
check ($a, 3).flat.elems, 2, '.flat leaves an item alone';

my $b;
$b = [5, 6];
check $b.raku, '$[5, 6]', 'a plain assignment itemizes too';
my $n = 41;
$n = $n + 1;
check $n, 42, 'a scalar value is stored as before';
my $s = 'a';
$s = $s ~ 'b';
check $s, 'ab', '…a string too';

if @fail { .say for @fail; say "{+@fail} check(s) failed"; exit 1 }
say 'PASS';
