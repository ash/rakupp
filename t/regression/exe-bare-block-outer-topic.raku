# --exe: a bare block called with no argument sees the OUTER `$_`, as its
# signature says (`$_ is raw = OUTER::<$_>`). Compiled, its topic defaulted to
# Any, so `$d.watch.tap: { $lock.protect: { %seen{.path.IO.basename}++ } }`
# read `.path` of Any and died "Must specify a non-empty string as a path".
# A `sub { }` still has its own fresh `$_`. Checked both ways by
# t/exe/parity.raku.
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

$_ = 5;
my &b = { $_ };
check b(), 5, 'the mainline topic';
check b(7), 7, 'an argument still wins';

my @got;
for 1, 2 { my &c = { $_ * 10 }; @got.push: c() }
check @got, [10, 20], "a loop's topic";

given 7 { check { $_ + 1 }(), 8, "given's topic" }

my $lock = Lock.new;
my @seen;
for <a b> { $lock.protect: { @seen.push: .uc } }
check @seen, ['A', 'B'], 'a block handed to Lock.protect';

my @out;
<x y>.map: { @out.push: { "<$_>" }() };
check @out, ['<x>', '<y>'], 'nested in a map block';

my &s = sub { $_ };
check s(), Any, 'a sub has its own topic';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
