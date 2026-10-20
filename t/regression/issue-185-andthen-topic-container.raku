# Issue #185: the `$_` an andthen/orelse/notandthen right side sees was a COPY
# of the left side, so `@a[0] andthen $_ = 2` left @a[0] alone, as did
# `$x andthen $_++`, `$x orelse $_ = 5` and `$x andthen s/b/X/`. Only a `.=` on
# the topic was patched back to a head variable. Rakudo binds `$_` to the left
# side's container, as `with` does: a variable, an array or hash element, and
# down a chain the container each step hands on.
#
# And `@a[0] andthen $_ = 2, 3, 4` assigns the whole (2, 3, 4), as in Rakudo:
# its grammar picks item or list assignment by $*LEFTSIGIL, the sigil of the
# expression's FIRST variable (`@a`), '@' once a postfix applied — not by the
# `=`'s own left side. The same rule makes `$y and @b = 1, 2` assign only the
# 1; parentheses, a ternary's middle, a call's arguments and a declaration's
# initializer each start an expression of their own.
#
# Also here: `with @a[$i++] { $_ = 5 }` took the element's slot by evaluating
# the subscript a second time, stepping $i twice and writing @a[1]; and
# `with $p { $_ = 5 }` on a readonly parameter wrote into it instead of dying.
#
# Contract: exit 0 + last line PASS. Every check is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# the issue
{
    my @a; @a[0] andthen $_ = 2, 3, 4;
    check @a, [], 'an undefined element skips the right side';
}
{
    my @a; @a[0] = 1, 2, 3; @a[0] andthen ($_ = 2, 3, 4);
    check @a, [2], 'the element is assigned through $_';
}
{
    my @a; @a[0] = 1, 2, 3; @a[0] andthen $_ = (2, 3, 4);
    check @a, [(2, 3, 4),], 'a parenthesized list goes in whole';
}
{
    my @a; @a[0] = 1, 2, 3; @a[0] andthen $_ = 2, 3, 4;
    check @a, [(2, 3, 4),], '`$_ = 2, 3, 4` after `@a[0]` is list assignment';
}

# $*LEFTSIGIL: the expression's first variable decides `=`, a postfix makes it '@'
# (each `do { }` value is kept, so Rakudo has no sunk constant to warn about)
{ my $y = 1; my @b; my $ = do { $y and @b = 1, 2 }; check @b, [1], 'after a `$` variable: item assignment' }
{ my @a = 1; my $y; my $ = do { @a and $y = 1, 2 }; check $y, (1, 2), 'after an `@` variable: list assignment' }
{ my $x = 1; my $y; my $ = do { $x.so and $y = 1, 2 }; check $y, (1, 2), 'after a postfix: list assignment' }
{ my $x = 1; my $y; my $ = do { $x and $y = 1, 2 }; check $y, 1, '`$` then `$`: item assignment' }
{ sub f { 1 }; my $y; my $ = do { f() and $y = 1, 2 }; check $y, 1, 'a call is no variable' }
{ my $x = 1; my @b; ($x) and @b = 1, 2; check @b, [1, 2], 'parentheses are an expression of their own' }
{ my @a = 1; my $y; my $r = (@a and $y = 1, 2); check $y, (1, 2), '…and so is what they enclose' }
{ my @a = 1; my @l = (@a and my $z = 1, 2); check $z, 1, 'a declaration decides by its own sigil' }
{ my @a = 1; my $y; my $ = do { @a ?? ($y = 1, 2) !! 0 }; check $y, 1, "a ternary's middle is its own" }
{ my @a = 1; my $y; my $ = do { 0 ?? 0 !! @a and $y = 1, 2 }; check $y, (1, 2), "…its last part is not" }
{ my @a = 1; my ($y, $z); my $ = do { @a and $y = 1, $z = 2, 3 }; check ($y, $z), ((1, 2, 3), 2), '`=` starts it afresh' }

# variables and elements, each operator
{ my $x = 1; $x andthen $_ = 5; check $x, 5, 'andthen assigns a variable' }
{ my $x; $x orelse $_ = 5; check $x, 5, 'orelse assigns a variable' }
{ my $x; $x notandthen $_ = 5; check $x, 5, 'notandthen assigns a variable' }
{ my %h = a => 1; %h<a> andthen $_ = 9; check %h, {a => 9}, 'a hash element' }
{ my @a = 1, 2; @a[1] andthen $_ *= 10; check @a, [1, 20], 'a compound assignment to an element' }
{ my @a = 1, 2; @a[*-1] andthen $_ = 9; check @a, [1, 9], 'a WhateverCode subscript' }
{ my %h = a => 1; my $k = 'a'; %h{$k} andthen $_ = 9; check %h, {a => 9}, 'a variable key' }
{ my %h = ab => 1; my $k = 'b'; %h{"a$k"} andthen $_ = 9; check %h, {ab => 9}, 'an interpolated key' }
{ my @a = [1, 2], [3, 4]; @a[1][0] andthen $_ = 9; check @a, [[1, 2], [9, 4]], 'a nested element' }
{ my $x = [1, 2]; $x andthen $_ = 5; check $x, 5, 'a $ variable holding an Array' }
{ $_ = 1; $_ andthen $_ = 5; check $_, 5, 'the outer topic itself' }
{ my @a = 1, 2; for @a { $_ andthen $_ *= 10 }; check @a, [10, 20], 'a for loop topic' }

# what the right side does to the topic
{ my $x = 1; $x andthen $_++; check $x, 2, 'postfix ++' }
{ my $x = 'abc'; $x andthen s/b/X/; check $x, 'aXc', 's///' }
{ my $x = 'a'; $x andthen .=uc; check $x, 'A', '.=' }
{ my $x = 1; my $seen; $x andthen ($_ = 5; $seen = $x); check $seen, 5, 'a variable sees the write at once' }
{ my $x = Failure.new('boom'); $x orelse $_ = 3; check $x, 3, 'orelse replaces a Failure' }
{ my $x = 1; my $r = ($x andthen $_); $r = 99; check $x, 1, 'the answer is a value, not the container' }

# a block on the right
{ my $x = 1; $x andthen { $_ = 9 }; check $x, 9, 'a bare block gets the container' }
{ my $x; $x orelse { $_ = 4 }; check $x, 4, '…on orelse too' }
{ my $x = 1; check ($x andthen -> $y { $y + 1 }), 2, 'a pointy block gets the value' }

# chains: each step's `$_` is the container the step before handed on
{ my $x = 1; $x andthen $_ = 5 andthen $_ = 7; check $x, 7, 'andthen chain' }
{ my $x; $x orelse $_ = 5 andthen $_ = 6; check $x, 6, 'orelse, then an andthen chain on the right' }
{ my $x = 1; $x andthen $_ = Any andthen $_ = 6; check $x, Any, 'a step that leaves it undefined stops the chain' }
{ my $x = 1; my $y = 2; $x andthen $y andthen $_ = 6; check ($x, $y), (1, 6), 'a variable in the middle is handed on' }
{ my $x = 1; $x += 1 andthen $_ = 6; check $x, 6, 'an assignment hands on its target' }
{ my $x = 1 andthen $_ = 5; check $x, 5, 'so does a declaration' }
{ my Int $x; $x orelse .=new andthen .=new: 43; check $x, 43, '.= steps in a chain' }

# no container to write: a literal dies, as `with 1 { $_ = 5 }` does
{ my $died = False; try { 1 andthen $_ = 5; CATCH { default { $died = True } } }; check $died, True, 'a literal topic is immutable' }

# …and so does a readonly parameter, whose value the topic is bound to.
# `with` and `given` used to write into the parameter instead, and andthen
# picked that up from them.
sub ro-andthen($p) { $p andthen $_ = 5 }
sub ro-with($p)    { with $p { $_ = 5 } }
sub ro-given($p)   { given $p { $_ = 5 } }
sub ro-mod($p)     { $_ = 5 with $p }
for &ro-andthen, &ro-with, &ro-given, &ro-mod -> &f {
    my $died = False; try { f(1); CATCH { default { $died = True } } }
    check $died, True, "{&f.name}: a readonly parameter's topic is immutable";
}
{ my $died = False; my $x := 42; try { $x andthen $_ = 5; CATCH { default { $died = True } } }; check ($died, $x), (True, 42), 'a name bound to a value' }
sub rw-with($p is rw)       { with $p { $_ = 5 } }
sub rw-andthen($p is rw)    { $p andthen $_ = 6 }
sub copy-andthen($p is copy) { $p andthen $_ = 5; $p }
{ my $v = 1; rw-with($v); check $v, 5, 'an is rw parameter writes through with' }
{ my $v = 1; rw-andthen($v); check $v, 6, '…and andthen' }
check copy-andthen(1), 5, 'an is copy parameter is its own container';
sub ro-push($p) { given $p { .push: 1 } }
{ my @a; ro-push(@a); check @a, [1], 'a readonly parameter holding an Array still pushes' }

# testing never vivifies, and a subscript is evaluated once
{ my %h; %h<a> andthen $_ = 5; check %h, {}, 'andthen on a missing key' }
{ my %h; my $n = 0; %h<a> orelse $n++; check (%h.elems, $n), (0, 1), 'orelse that assigns nothing' }
{ my %h; %h<a> orelse $_ = 5; check %h, {a => 5}, 'orelse that assigns creates the key' }
{ my @a; @a[3] andthen $_ = 1; check @a.elems, 0, 'andthen on a missing element' }
{
    my @a = 1, 2; my $i = 0; @a[$i++] andthen $_ = 5;
    check ($i, @a[1]), (1, 2), 'andthen: a side-effecting subscript runs once';
}
{
    my @a = 1, 2; my $i = 0; with @a[$i++] { $_ = 5 }
    check ($i, @a[1]), (1, 2), 'with: a side-effecting subscript runs once';
}

if @fail { .note for @fail; die "{+@fail} failure(s)" }
say 'PASS';
