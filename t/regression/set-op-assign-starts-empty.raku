# Regression: a set or bag operator, op-assigned to an undefined target or
# reduced over nothing, starts from its identity as in Rakudo: set() for
# ∪ ∩ ∖ ⊖ and their ASCII forms, bag() for ⊎ and ⊍.
#
# Before the fix the undefined target joined in as a value: `my $s; $s ∪= 0`
# was Set.new(Any, 0), `$s (-)= 1` Set.new(Any) and `$b ⊎= 1` held Any=>1;
# `[∪] ()` and `[⊎] ()` were Any. Roast's S04-phasers/enter-leave.t counts
# the elems of a set built with `∪=` inside ENTER (its test 34).
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Str $what, $got, Str $want) {
    @fail.push("$what: got {$got.raku}, want $want") unless $got.raku eq $want;
}

for <∪ (|) ⊖ (^)> -> $op {
    check "\$s $op= 1",  EVAL("my \$s; \$s $op= 1; \$s"), 'Set.new(1)';
    check "[$op] ()",    EVAL("[$op] ()"),                'set()';
}
for <∩ (&) ∖ (-)> -> $op {
    check "\$s $op= 1",  EVAL("my \$s; \$s $op= 1; \$s"), 'set()';
    check "[$op] ()",    EVAL("[$op] ()"),                'set()';
}
for <⊎ (+)> -> $op {
    check "\$b $op= 1",  EVAL("my \$b; \$b $op= 1; \$b"), '(1=>1).Bag';
    check "[$op] ()",    EVAL("[$op] ()"),                'bag()';
}
for <⊍ (.)> -> $op {
    check "\$b $op= 1",  EVAL("my \$b; \$b $op= 1; \$b"), 'bag()';
    check "[$op] ()",    EVAL("[$op] ()"),                'bag()';
}

my $u; $u ∪= (1, 2);        check 'a list joins an empty set', $u.keys.sort.join(","), '"1,2"';
my $b; $b ⊎= <a a b>;       check 'a list joins an empty bag', $b.pairs.sort.join(","), '"a\t2,b\t1"';
my %h; %h<k> ∪= 3;          check 'a hash slot',               %h<k>, 'Set.new(3)';
my @a; @a[0] (+)= 'x';      check 'an array slot',             @a[0], '("x"=>1).Bag';
my $n; $n ∪= 0 for ^2;      check 'repeated, as enter-leave.t', $n.elems, '1';
# a defined target is untouched by the rule
my $sh = SetHash.new; $sh ∪= 1; check 'a SetHash stays one', $sh, 'SetHash.new(1)';

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
