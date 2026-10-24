# Regression: a store into an element bound to a container reaches that
# container, however the bind and the store are spelled.
#
# Two paths lost it. A bind to a DECLARATION (`@b[1] := my $c`, and the scalar
# `$x := my $c`) looked `$c` up before the declaration made it, and the
# declaration then made a fresh `$c`: `@b[1] = 9` left `$c` as (Any). And a
# SLICE or LIST assignment (`@b[0, 1] = …`, `(@b[0], @b[1]) = …`) wrote the
# element's slot outright, so the bound container was replaced instead of
# stored into. Found 2026-10-24 reading the element-store path; fixed by
# bindToDeclared and storeThrough (src/InterpreterCalls.cpp).
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- binding to a declaration ----
{
    my @b = 1, 2, 3;
    @b[1] := my $c;
    @b[1] = 9;
    ck $c, 9, '`@b[1] := my $c`: a store to the element lands in $c';
    $c = 11;
    ck @b[1], 11, '…and a store to $c shows in the element';
}
{
    my @b = 1, 2, 3;
    @b[1] := my $c = 4;
    ck @b[1], 4, '`@b[1] := my $c = 4` binds the initialised container';
    @b[1] = 9;
    ck $c, 9, '…and stores through it';
}
{
    my @b = 1, 2, 3;
    @b[1] := my $c;
    @b[1]++;
    ck $c, 1, '`@b[1]++` on an element bound to `my $c` increments $c';
}
{
    my @b = 1, 2, 3;
    @b[1] := my Int $c;
    @b[1] = 9;
    ck $c, 9, 'a typed declaration binds the same way';
}
{
    my @b = 1, 2, 3;
    @b[1] := (my $c);
    @b[1] = 9;
    ck $c, 9, 'a parenthesised declaration binds the same way';
}
{
    my @b = 1, 2, 3;
    @b[1] := state $c;
    @b[1] = 9;
    ck $c, 9, 'a `state` declaration binds the same way';
}
{
    my %h = a => 1;
    %h<a> := my $c;
    %h<a> = 7;
    ck $c, 7, '`%h<a> := my $c`: a store to the value lands in $c';
}
{
    my $x;
    $x := my $c;
    $x = 9;
    ck $c, 9, '`$x := my $c`: a store to $x lands in $c';
    my $y := my $d;
    $y = 3;
    ck $d, 3, '`my $y := my $d` aliases the two';
}
{
    sub f { my @b = 1, 2, 3; @b[1] := my $c; @b[1] = 8; $c }
    ck f(), 8, 'the same inside a routine';
    ck f(), 8, '…called again, with a fresh $c';
}

# ---- slice and list assignment through a bound element ----
{
    my @b = 1, 2, 3;
    my $c = 5;
    @b[1] := $c;
    @b[1, 2] = 7, 8;
    ck $c, 7, 'a slice assignment stores into a bound element\'s container';
    ck @b.List, (1, 7, 8), '…and the array reads it';
    $c = 0;
    ck @b[1], 0, '…which is still bound afterwards';
}
{
    my @e = 1, 2, 3;
    my $f = 0;
    @e[1] := $f;
    @e[^2] = 3, 4;
    ck $f, 4, 'a Range slice does the same';
}
{
    my %h = a => 1, b => 2;
    my $d = 0;
    %h<a> := $d;
    %h<a b> = 10, 20;
    ck $d, 10, 'a hash slice stores into a bound value\'s container';
}
{
    my @b = 1, 2, 3;
    my $c = 5;
    @b[1] := $c;
    ($c, @b[1]) = 1, 2;
    ck $c, 2, 'a list assignment stores into a bound element\'s container';
}
{
    my $stored;
    my $p := Proxy.new(FETCH => { 1 }, STORE => -> $, $v { $stored = $v });
    ($p, my $q) = 5, 6;
    ck $stored, 5, 'a list assignment to a Proxy runs its STORE';
}

# ---- the element's container ----
{
    my @b = 1, 2, 3;
    my $c = 5;
    @b[1] := $c;
    ck @b[1].VAR.^name, 'Scalar', '.VAR of a bound element is a Scalar';
    my %h;
    %h<a> := $c;
    ck %h<a>.VAR.^name, 'Scalar', '…and of a bound hash value';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
