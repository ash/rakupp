# Regression: a placeholder inside a BLOCK in an attribute default.
#
# `checkVirtualCallInDefault` scanned the default's raw tokens flat, so `$^a`
# anywhere in it was "may not be used here because the surrounding block does
# not take a signature" — including inside a block literal, which is exactly
# the block the placeholder was meant to parameterize. FunctionalParsers writes
# `has &.seqLeftSepForm = { ($^a, $^b) }` and would not compile (issue #60).
#
# The scan now tracks brace depth: a block literal claims the placeholder, a
# block that runs where it stands (`do`, `try`, `gather`, a control block) or
# one that already carries a signature (`-> $x { … }`) still does not.

class C {
    has &.pair    = { ($^a, $^b) };
    has &.sum     = sub { $^a + $^b };
    has &.nested  = { my &inner = { $^x * 2 }; inner($^a) };
    has &.pointy  = -> $q { $q + 1 };
    has %.opts    = { verbose => 1 };
}

my $c = C.new;
die "pair"   unless $c.pair.(1, 2) eqv (1, 2);
die "sum"    unless $c.sum.(3, 4) == 7;
die "nested" unless $c.nested.(5) == 10;
die "pointy" unless $c.pointy.(9) == 10;
die "opts"   unless $c.opts<verbose> == 1;

# …and the bans that must survive: a bare placeholder in the default itself,
# a placeholder in a `do` block that has no signature to take it, and a
# virtual call reaching in from inside a block.
for 'class D { has $.a = $^b + 1 }',
    'class D { has $.a = do { $^b } }',
    'class D { has $.y = 1; has &.f = { $.y } }' -> $src {
    my $ok = False;
    try { EVAL $src; CATCH { default { $ok = True } } }
    die "still-legal: $src" unless $ok;
}

say 'PASS';
