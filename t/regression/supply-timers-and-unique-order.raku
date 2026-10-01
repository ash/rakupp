# Regression: two Supply behaviours that differed from Rakudo.
# 1. Timers due at the same moment raced for the GIL, so
#    `Supply.from-list(7, 8, 9).delayed(0.1)` came out in any order.
# 2. A live Supply's `.unique`/`.squish` compared values by their Str, so 1,
#    "1" and <1> were one value, and an IO::Notification::Change (a value type,
#    WHICH from its path and event) was never equal to another for the same file.
# Contract: exit 0 + last line PASS.
my @fail;

for ^5 {
    my @got;
    my $t = Supply.from-list(7, 8, 9, 10).delayed(0.05).tap({ @got.push($_) });
    sleep 0.25;
    @fail.push("delayed order {@got}") unless @got eqv [7, 8, 9, 10];
}

my $s = Supplier.new;
my @u;
$s.Supply.unique.tap({ @u.push($_) });
$s.emit($_) for 1, "1", 1, 1.0, <1>, "a", "a";
@fail.push("unique gave {@u.raku}") unless @u.elems == 5;

my $c = IO::Notification::Change.new(path => "p", event => FileChanged);
my $d = IO::Notification::Change.new(path => "p", event => FileChanged);
@fail.push("Change is not a value type") unless $c === $d;
my $n = Supplier.new;
my @v;
$n.Supply.unique.tap({ @v.push($_) });
$n.emit($_) for $c, $d, IO::Notification::Change.new(path => "p", event => FileRenamed);
@fail.push("unique Changes {@v.elems}") unless @v.elems == 2;

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
