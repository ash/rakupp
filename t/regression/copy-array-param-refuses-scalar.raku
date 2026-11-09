# An `@` parameter that is `is copy` refuses what its sigil refuses, as one
# that is not does and as Rakudo's does: `sub c(@a is copy) { }; c(1)` made
# a one-element Array of the 1, where Rakudo says "Type check failed in
# binding to parameter '@a'; expected Positional but got Int (1)", and Any,
# a Hash and a named `:@a is copy` given a scalar were taken too. What is
# Positional is copied into a fresh Array, as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub refusal(&code) {
    code();
    return 'no refusal';
    CATCH { default { return .message.lines[0] } }
}

sub c(@a is copy) { @a.push(9); @a.^name ~ ' ' ~ @a.elems }
sub n(:@a is copy) { @a.elems }
my $int = 1;
my $any = Any;
my $hash = %(a => 1);
ck refusal({ c($int) }), "Type check failed in binding to parameter '@a'; expected Positional but got Int (1)", 'an Int';
ck refusal({ c($any) }), "Type check failed in binding to parameter '@a'; expected Positional but got Any (Any)", 'Any';
ck refusal({ c($hash) }), "Type check failed in binding to parameter '@a'; expected Positional but got Hash (\{:a(1)}). You have to pass an",
    'a Hash';
ck refusal({ n(:a($int)) }), "Type check failed in binding to parameter '@a'; expected Positional but got Int (1)", 'a named one';
my @x = 1, 2;
ck c(@x), 'Array 3', 'an Array is copied';
ck @x.List, (1, 2), '…and the caller\'s is untouched';
ck c((1, 2)), 'Array 3', 'a List';
ck c(1..2), 'Array 3', 'a Range';
my $l = (5, 6);
ck c($l), 'Array 3', 'a List in a $ variable';

say 'PASS';
