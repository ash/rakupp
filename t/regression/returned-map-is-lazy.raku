# Regression: a map or grep that is a routine's or closure's value is lazy
# (issue #60).
#
# Rakudo's map and grep are lazy, so a Seq handed back from a routine runs
# its block for what the caller reads. Here only a `.first`/`.head` written
# directly on the map read it lazily (b6ac7079); returned from a routine, it
# ran every element where it was written. FunctionalParsers' `apply` is
# `-> @x { &p(@x).map({ ($_[0], &f($_[1])) }) }`, and its chain-left reads
# `.head` of a list whose third element makes `&f` die, so the whole test file
# died. Every answer below is Rakudo 2026.09's.

my @log;
sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub f { (1, 2, 3).map({ @log.push("m$_"); $_ * 10 }) }
@log = (); ck f().head, 10, 'a sub\'s map read by .head';
ck @log, ['m1'], '…runs its block once';

my &g = -> { (1, 2, 3).map({ @log.push("p$_"); $_ }) };
@log = (); ck g().head, 1, 'a pointy block\'s map'; ck @log, ['p1'], '…once';

my &h = sub { (1, 2, 3).grep({ @log.push("g$_"); $_ > 1 }) };
@log = (); ck h().head, 2, 'an anonymous sub\'s grep'; ck @log, ['g1', 'g2'], '…up to its answer';

sub r { return (1, 2, 3).map({ @log.push("r$_"); $_ }) }
@log = (); ck r().head, 1, '`return` of a map'; ck @log, ['r1'], '…once';

class C { has @.x = 1, 2, 3; method d { @!x.map({ @log.push("c$_"); $_ * 2 }) } }
@log = (); ck C.new.d.head, 2, 'a method\'s map'; ck @log, ['c1'], '…once';

# a Seq kept for later runs where it is read
@log = ();
my $s = f(); @log.push('made'); ck $s.elems, 3, 'kept, then counted';
ck @log, ['made', 'm1', 'm2', 'm3'], '…runs when read';

# …and sunk, it still runs, as a statement or under a modifier
@log = (); f(); ck @log, ['m1', 'm2', 'm3'], 'a sunk call runs it';
@log = (); f() if True; ck @log, ['m1', 'm2', 'm3'], '…and one under `if`';

# eager readers get every element
my @r = f(); ck @r, [10, 20, 30], 'list assignment';
my @a; @a.append(f()); ck @a, [10, 20, 30], '.append';
my @p = 0; @p.prepend(f()); ck @p, [10, 20, 30, 0], '.prepend';
ck f().raku, '(10, 20, 30).Seq', '.raku';

# it is still a map over a FINITE list: not lazy, as Rakudo's is not
ck f().is-lazy, False, '.is-lazy over a finite list';
sub endless { (1..*).map(* * 2) }
ck endless().is-lazy, True, '…and True over an endless one';
ck endless().head(3).List, (2, 4, 6), '…read as far as asked';

# the block still sees the source's elements themselves
sub bump(@x) { @x.map({ $_++ }) }
my @b = 1, 2; bump(@b).eager; ck @b, [2, 3], 'a write to $_ reaches the element';
sub dbl(@x) { @x.map(* * 2) }
my @src = 1, 2, 3; my $late = dbl(@src); @src[0] = 100;
ck $late.List, (200, 4, 6), 'the source is read when the Seq is';

# FunctionalParsers' chain-left: the third parse makes the block die,
# and `.head` never reaches it
sub apply(&f, &p) { -> @x { &p(@x).map({ ($_[0], &f($_[1])) }) } }
my &parses = -> @x { ((), (2, ((&infix:<->, 1),))), ((), (2, ())) };
my &chain = apply({ reduce({ $^b[0]($^a, $^b[1]) }, $_.head, |$_[1]) }, &parses);
ck &chain(()).head.tail, 1, 'chain-left stops at its first parse';

say 'PASS';
