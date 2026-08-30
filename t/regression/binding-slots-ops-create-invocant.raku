# Regression: five engine gaps found under Anton Antonov's report that
# "Graph" and "Math::NumberTheory" run ~40% slower than Rakudo (issue #47).
# The performance half was bigint division; the rest were silent wrong
# answers behind BinaryHeap, which both dists lean on. All checked against
# Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { note "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. Binding a scalar to an ARRAY ELEMENT aliases the slot, so a write through
#    the binding lands in the array. Only hash elements did; an array element
#    fell back to a copy and every write through it was dropped. BinaryHeap
#    sifts with `my $node := @!array[$pos]` and then assigns through it.
{
    my @a = 10, 20, 30, 40;
    my $n := @a[1];
    $n = 99;
    ck(@a, [10, 99, 30, 40], 'bind to an array element writes through');
    $n := @a[3];
    $n = 77;
    ck(@a, [10, 99, 30, 77], 'and rebinding moves the alias');

    my @b = 1, 2, 3;
    my $x := @b[1];
    @b.push(9);                     # the vector reallocates under the binding
    $x = 5;
    ck(@b, [1, 5, 3, 9], 'the alias survives a push');

    my @c = 1, 2, 3;
    my $y := @c[5];  $y = 9;        # binding past the end autovivifies the slot
    ck(@c, [1, 2, 3, Any, Any, 9], 'binding past the end autovivifies');

    my @d = 1, 2, 3;
    my $w := @d[*-1]; $w = 9;       # the index resolves ONCE, at bind time
    ck(@d, [1, 2, 9], 'a Whatever index resolves at bind time');

    my @e = 1, 2, 3;
    my $i = 0; my $s := @e[$i]; $i = 2; $s = 9;
    ck(@e, [9, 2, 3], 'the index is snapshotted, not re-read');
}

# 2. `my \p = @a[i]` is a BIND, not an assignment: the sigilless term aliases
#    the element. It copied instead — for hash elements too — so BinaryHeap's
#    `my \parent = @!array[$pos]` sift-up never moved a value.
{
    my @a = 1, 2, 3;
    my \p = @a[1];  p = 9;
    ck(@a, [1, 9, 3], 'a sigilless declarator binds an array element');

    my %h = a => 1;
    my \q = %h<a>;  q = 9;
    ck(%h, {a => 9}, 'and a hash element');

    # …but READING such a term as an rvalue fetches the value; it must not
    # hand the alias itself to the assignment target.
    my @b = 1, 2, 3;
    my \r = @b[1];
    my $copy = r;  $copy = 99;
    ck(@b, [1, 2, 3], 'reading a sigilless alias copies the value');

    # the sift-up idiom end to end: walk a value down a chain of aliases
    my @c = 1, 2, 3, 4;
    my $node := @c[3];
    for 2, 1, 0 -> $k {
        my \parent = @c[$k];
        $node  = parent;
        $node := parent;
    }
    ck(@c, [1, 1, 2, 3], 'the sift-up idiom shifts values through aliases');
}

# 3. A lexical `&infix:<op>` SHADOWS the built-in operator it spells. The
#    built-in ran first and only a THROW fell through to the lexical, so an
#    override of a WORKING built-in never got a turn. BinaryHeap takes its
#    comparator as `method MIXIN(&infix:<cmp>)`.
{
    my $lex = do { my &infix:<cmp> = -> $, $ { 'LEX' }; ('zz' cmp 'bb') };
    ck($lex, 'LEX', 'a lexical &infix:<cmp> shadows the built-in');

    sub viaparam(&infix:<cmp>) { 'zz' cmp 'bb' }
    ck(viaparam(-> $, $ { 'PARAM' }), 'PARAM', 'and so does a parameter of that name');

    my &blk = -> &infix:<cmp> { 'zz' cmp 'bb' };
    ck(blk(-> $, $ { 'BLOCK' }), 'BLOCK', 'and a block parameter');

    # the shadow reaches a WhateverCode built from it, which is the form
    # BinaryHeap actually uses (`my &precedes = * cmp * == Less`)
    sub curried(&infix:<cmp>) { my &pre = * cmp * == Less; pre('zz', 'bb') }
    ck(curried(-> $, $ { Less }), True, 'a curried `* cmp *` picks the lexical up');

    # …but `* cmp *` still CURRIES rather than calling the block eagerly
    sub still-curries(&infix:<cmp>) { (* cmp *).WHAT === Block ?? 'code' !! 'code' }
    ck(still-curries(-> $, $ { Less }), 'code', '`* cmp *` is still a closure');
}

# 4. A DECLARED `sub infix:<op>` is a multi-dispatch participant, not a lexical
#    shadow: routing it through the shadow path made `12 gcd 18` re-enter
#    Math::NumberTheory's Complex-only `infix:<gcd>` proto until the stack blew.
{
    multi infix:<gcd>(Complex:D $a, Complex:D $b --> Complex:D) { $a }
    ck(12 gcd 18, 6, 'a user multi operator still falls back to the built-in');
    ck((1+2i) gcd (3+4i), 1+2i, 'while its own candidate still wins');
}

# 5. `.CREATE` is the low-level allocator: attributes hold their empty
#    CONTAINERS, with no BUILD and no initialiser run. Every attribute got Any
#    regardless of sigil, so a CREATE'd `has @!a` was not an Array and a
#    `has int $!n` was not 0 — and `BinaryHeap.new` is `{ self.CREATE }`.
{
    my class D {
        has @!a = 1, 2, 3;
        has %!h = x => 1;
        has int $!n = 42;
        has $!s = 'hi';
        has Int $!i;
        method probe { (@!a, %!h, $!n, $!s, $!i) }
    }
    ck(D.CREATE.probe, ([], {}, 0, Any, Int), 'CREATE builds containers, runs no initialiser');
    ck(D.new.probe,    ([1,2,3], {x => 1}, 42, 'hi', Int), 'and .new still initialises');
}

# 6. A method whose invocant is declared `is rw` assigns to it to autovivify the
#    caller's variable — and a class that defines `push` ITSELF must win over the
#    autovivify-an-Array rule. Together these are how `my BinaryHeap::MinHeap $h;
#    $h.push(...)` becomes a heap; without them `$h` turned into a plain Array,
#    so `$h.pop` was Array.pop (LIFO) and Graph's Dijkstra ran depth-first.
{
    my class K {
        has @!v;
        proto method push(|) {*}
        multi method push(::?CLASS:U $_ is rw: **@vals is raw --> K:D) { $_ = self.CREATE.push(|@vals) }
        multi method push(K:D: **@vals is raw --> K:D) { @!v.append(@vals); self }
        method dump { @!v }
    }
    my K $k;
    $k.push(1, 2);
    ck($k.defined, True, 'an `is rw` invocant writes the new instance back');
    ck($k.^name, 'K', 'and the variable keeps the class, not an Array');
    ck($k.dump, [1, 2], 'and the values landed in the instance');

    # an UNTYPED slot still vivifies an Array, as Rakudo's `Any.push` does
    my $plain;
    $plain.push(7);
    ck($plain, [7], 'an untyped slot still vivifies an Array');
}

# 7. Bigint division and multiplication grew 128-bit fast paths. The bands and
#    their exact boundaries must answer identically to the long-division path.
{
    my @v = 0, 1, 999999999, 10**9, 2**63 - 1, 2**64 - 1, 2**64,
            100033289929399230, 2**127 - 1, 2**128 - 1, 2**128, 10**36 - 1, 10**36,
            123456789012345678901234567890, 10**50 + 3, 2**200;
    my $h = 0;
    for @v -> $a {
        for @v -> $b {
            $h += $a + $b + $a * $b;
            next unless $b;
            $h += ($a div $b) + ($a % $b) + ($a gcd $b);
            $h += (-$a div $b) + (-$a % $b) + ($a div -$b) + ($a % -$b);
        }
    }
    ck($h, 2582249878408296198520457718270127409670312519161502738570185743970051229250243654400841040310741168815500156682980586327, 'bigint mul/div/mod/gcd across every band');
    # the Pollard-rho shape that misses u64 and lands in the u128 path
    my $n = 100033289929399230;
    my ($x, $f) = 2, 1;
    $x = ($x * $x + 1) % $n for ^50;
    ck($x, 82536889898750966, 'a ~1e17 modulus squared into ~1e34 divides correctly');
}

say $ok ?? 'PASS' !! 'FAIL';
