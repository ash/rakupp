# Regression: `@c».(args)` is a hyper like `@c».method`, not a `.map`: the
# answer keeps the invocant's shape (an Array stays an Array, a List a List, a
# Hash keeps its keys), nested lists are descended into, and an element that
# is not Callable is refused as a `&code` binding. It used to be rewritten to
# `.map({ $_(…) })`, which answered a Seq and died on a nested Array.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @c = {1}, {2};
ck(@c».(), [1, 2], 'an Array of blocks answers an Array');
ck(@c>>.(), [1, 2], 'the ASCII spelling too');
my @d = -> $x { $x * 2 }, -> $x { $x + 1 };
ck(@d».(10), [20, 11], 'the arguments go to every element');
ck(({3}, {4})».(), (3, 4), 'a List answers a List');
my @n = [{1}, {2}], {3};
ck(@n».(), [[1, 2], 3], 'a nested Array is descended into');
my %h = a => {1}, b => {2};
ck(%h».(), {a => 1, b => 2}, 'a Hash keeps its keys');
my @o = 5, {1};
my $err = (try { @o».(); 'lived' }) // $!;
ck($err.^name, 'X::TypeCheck::Binding::Parameter', 'a non-Callable element is refused');
ck($err.message.contains("expected Callable but got Int (5)"), True, '…saying what it got');

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
