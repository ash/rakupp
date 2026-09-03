# Issue #63: reported as "clone breaks under recursion" — an object's array
# attribute kept changing under the original after `self.clone`. The clone
# semantics were RIGHT (Rakudo 2026.08 prints the report's output byte for
# byte: `clone` is shallow, so the original and the clone share the one Array
# the `@.data` attribute holds, and `self.data = [$i]` list-assigns INTO it).
#
# What was wrong is why the program got that far. The report's class declares
# `has Str @.data` and stores Ints in it; Rakudo stops at that line with
# "Type check failed for an element of @!data", and rakupp enforced no element
# type at all — not on an attribute, not on a lexical, not through any of the
# ways a value enters a container. `.of` on a typed attribute answered (Mu),
# because the declared type never reached the slot.
#
# Contract: exit 0 + last line PASS. Every case below is oracle-checked
# against Rakudo 2026.08 — run this file under `raku` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
# what a store into a typed container does: 'ok', or the exception's type name
sub outcome(&c) {
    my $r = 'ok';
    try { c(); CATCH { default { $r = .^name } } }
    $r
}
constant BAD = 'X::TypeCheck::Assignment';

# -- the declared type reaches the slot --------------------------------------
{
    class Slot { has Str @.d is rw; has Int %.h is rw }
    my $c = Slot.new;
    check($c.d.of.^name, 'Str', 'has Str @.d makes the attribute an Array[Str]');
    check($c.h.of.^name, 'Int', '…and has Int %.h a Hash[Int]');
    my Str @lex;
    check(@lex.of.^name, 'Str', '…as `my Str @a` already did');
}

# -- every way a value enters a LEXICAL typed array --------------------------
{
    check(outcome({ my Str @a; @a[0] = 'x' }),        'ok',  'a Str into Array[Str]');
    check(outcome({ my Str @a; @a[0] = 1 }),          BAD,   'an Int into Array[Str] is a type error');
    check(outcome({ my Str @a; @a[0,1] = 'x', 2 }),   BAD,   '…through a slice too');
    check(outcome({ my Str @a = 1 }),                 BAD,   '…through list assignment');
    check(outcome({ my Str @a; @a.push(1) }),         BAD,   '…through .push');
    check(outcome({ my Str @a; @a.append(1) }),       BAD,   '…through .append');
    check(outcome({ my Str @a; @a.unshift(1) }),      BAD,   '…through .unshift');
    check(outcome({ my Str @a; @a.prepend(1) }),      BAD,   '…through .prepend');
    check(outcome({ my Str @a = <a b>; @a.splice(1, 1, 2) }),
                              'X::TypeCheck::Splice', '…and .splice says so in its own words');
    my Str @keep = <a b>;
    try { @keep.splice(1, 1, 2) };
    check(@keep.elems, 2, 'a splice that fails to type-check leaves the array alone');
}

# -- the exempt values -------------------------------------------------------
{
    check(outcome({ my Str @a; @a[0] = Str }),  'ok', 'the matching type object is a legal element');
    check(outcome({ my Str @a; @a[0] = Any }),  BAD,  '…a supertype-s is not');
    check(outcome({ my Str @a; @a[0] = Nil }),  'ok', 'a Nil RESETS the slot, it is not a store');
    my Str @n; @n[0] = Nil;
    check(@n[0].^name, 'Str', '…and resets it to the element type object');
    my Bool @r = Nil;
    check((@r.elems, @r[0].^name, ?@r), (1, 'Bool', True),
          '`my Bool @r = Nil` is one default element (issue #37), typed');
    check(outcome({ my Str @a; @a[0] = <42> }), 'ok', 'an allomorph IS a Str');
    check(outcome({ my Int @a; @a[0] = True }), 'ok', 'Bool is an Int subtype');
    check(outcome({ my Int @a; @a[0] = 1.5 }),  BAD,  '…a Rat is not');
    check(outcome({ my Numeric @a; @a[0] = 7 }), 'ok', 'a subtype satisfies a wider element type');
    check(outcome({ my Mu @a; @a[0] = 7 }),     'ok', 'Mu constrains nothing');
    check(outcome({ my Any @a; @a[0] = 7 }),    'ok', '…nor does Any');
}

# -- user types, roles and subsets, not just the core ones -------------------
{
    my role R {}
    my class W does R {}
    my subset Even of Int where * %% 2;
    check(outcome({ my R @a; @a[0] = W.new }), 'ok', 'a role-typed array takes what does the role');
    check(outcome({ my R @a; @a[0] = 1 }),     BAD,  '…and rejects what does not');
    check(outcome({ my Even @a; @a[0] = 2 }),  'ok', 'a subset-typed array runs the `where`');
    check(outcome({ my Even @a; @a[0] = 3 }),  BAD,  '…and rejects a value that fails it');
}

# -- a PARAMETERISED element type constrains twice ---------------------------
# (roast S06-currying/positional.t declares `my Array[Int] @AoAoI`, and a
#  base-name-only check rejected it — the whole file died there)
{
    my Int @i = 1, 2;
    my Str @s = <a b>;
    check(outcome({ my Array[Int] @a = $@i, $@i }), 'ok',
          'Array[Int] @a takes arrays parameterised on Int');
    check(outcome({ my Array[Int] @a = $@s }), BAD,  '…and not an Array[Str]');
    check(outcome({ my Array[Int] @a = $[1, 2] }), BAD, '…nor an unparameterised Array');
    check(outcome({ my Array @a = $@i }), 'ok', 'a bare Array element type takes any of them');
    # the value/key split of a hash's ofType is a TOP-LEVEL comma: a
    # parameterised element type carries its own commas inside its brackets
    check(outcome({ my Hash[Int,Str] @a = $%(:a(1)) }), BAD,
          'Hash[Int,Str] @a rejects a plain Hash (and its comma is not a split point)');
}

# -- and the same for an ATTRIBUTE, which is what the report hit -------------
{
    class Rec { has Str @.d is rw; has Int %.h is rw }
    check(outcome({ Rec.new(d => [1]) }),      BAD, 'a bad element passed to .new');
    check(outcome({ Rec.new(d => ['x']) }),    'ok', '…a good one constructs');
    check(outcome({ my $c = Rec.new; $c.d = [1] }),    BAD, 'a bad element assigned through the accessor');
    check(outcome({ my $c = Rec.new; $c.d.push(1) }),  BAD, '…or pushed through it');
    check(outcome({ my $c = Rec.new; $c.h<k> = 'x' }), BAD, '…or keyed into a typed hash');
    check(outcome({ my $c = Rec.new; $c.h<k> = 3 }),   'ok', '…which still takes its own type');
    class Dflt { has Str @.d = [1] }
    check(outcome({ Dflt.new }), BAD, 'a declared DEFAULT is checked like any other element');
}

# -- the report's own program, from inside a method, mid-recursion -----------
{
    class Foo {
        has Str @.data is rw;
        method bad($i)  { self.data = [$i] }         # Int into Array[Str]
        method good($i) { self.data = ["$i"] }
    }
    check(outcome({ Foo.new.bad(0) }),  BAD,  'the reported line is a type error, as on Rakudo');
    check(outcome({ Foo.new.good(0) }), 'ok', '…and its stringified form is not');
}

# -- clone itself: the shallow-copy semantics the report questioned ----------
# Rakudo agrees with every line of this — `clone` copies the attribute VALUES,
# so a container attribute is SHARED with the clone, and a `.clone(:twiddle)`
# is what gives the clone its own.
{
    class Node { has $.depth is rw; has @.trail is rw }
    my $root = Node.new(depth => -1, trail => []);
    my $c = $root.clone;
    check($c === $root, False, 'clone is a different object');
    check($c.trail === $root.trail, True, '…that SHARES its container attributes');
    $c.depth = 5;
    check($root.depth, -1, '…while a scalar attribute is the clone\'s own');
    $c.trail.push(1);
    check($root.trail.elems, 1, '…and a push through the clone is visible in the original');
    my $t = $root.clone(trail => [ |@($root.trail), 2 ]);
    check(($t.trail.elems, $root.trail.elems), (2, 1),
          'a twiddle gives the clone a container of its own');

    # …which is what makes it safe under recursion: each level clones with its
    # own trail, and unwinding leaves every caller's trail untouched.
    my @seen;
    sub walk($n, $node) {
        return if $n > 3;
        my $kid = $node.clone(depth => $n, trail => [ |@($node.trail), $n ]);
        walk($n + 1, $kid);
        @seen.push($node.trail.join(','));
    }
    walk(0, Node.new(depth => -1, trail => []));
    check(@seen, ['0,1,2', '0,1', '0', ''],
          'each recursion level keeps its own trail on the way out');
}

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
