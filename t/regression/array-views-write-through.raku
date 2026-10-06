# Regression: a list of an Array's own ELEMENTS — `@a.reverse`, `.rotate`,
# `.sort`, `.Seq`, a slice `@a[0,1]` — holds the array's containers in Rakudo,
# so a write through it (`$r[0] = 9`, `$r[1]++`, `$r[2] ~= 'x'`, and through
# `.List` of it) lands in the array. Raku++ lists hold values: those writes were
# refused (X::Assignment::RO) or, worse, silently lost. Such a list now carries
# where its elements came from (ElemView in Value.h) and a write is mirrored
# into the array — interpreted (lvalue + the assignment) and compiled
# (rtViewSync). `@a.List` and a literal list stay read-only, as in Rakudo, and
# a typed array checks the type — both in array-views-typed.raku, which --exe
# does not compile natively (a typed `@` variable; and compiled code does not
# refuse a write into an immutable List at all, a gap of its own). A native
# array has no containers to write. Found by rakuglaze (App::RaCoCo's `@parts.reverse.List`).
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{ my @p = <lib App Mod>; my $l = @p.reverse.List; $l[0] = "x";
  ck((@p.join(' '), $l.join(' ')), ('lib App x', 'x App lib'), '.reverse.List takes a write, and so does the array') }
{ my @a = 1, 2, 3; my $r = @a.reverse; $r[0] = 9; ck(@a, [1, 2, 9], '.reverse') }
{ my @e = 1, 2, 3; my $s = @e.Seq.List; $s[0] = 5; ck(@e, [5, 2, 3], '.Seq.List') }
{ my @f = 1, 2, 3; my $rr = @f.reverse.reverse; $rr[0] = 8; ck(@f, [8, 2, 3], '.reverse.reverse') }
{ my @h = 1, 2, 3; my $sl = @h[0, 1]; $sl[0] = 42; ck(@h, [42, 2, 3], 'a slice') }
{ my @h = 1, 2, 3; my $sl = @h[0..1]; $sl[1] ~= "x"; ck(@h, [1, "2x", 3], 'a range slice and ~=') }
{ my @i = 1, 2, 3; my $ro = @i.rotate; $ro[0] = 42; ck(@i, [1, 42, 3], '.rotate') }
{ my @j = 3, 1, 2; my $so = @j.sort; $so[0] = 42; ck(@j, [3, 42, 2], '.sort') }
{ my @k = 1, 2, 3; my $q = @k.reverse; $q[1]++; $q[2] += 10; ck(@k, [11, 3, 3], '++ and +=') }
{ my @m; @m[2] = 1; my $r = @m.reverse; $r[0] = 5; ck(@m[2], 5, 'over holes') }
{ my @b = 1, 2, 3; my @rb = @b.reverse; @rb[0] = 9; ck(@b, [1, 2, 3], 'assigned to an array: a copy') }
{ my @n = 1, 2, 3; ck(@n.reverse.map(* * 2).List, (6, 4, 2), 'reads are unchanged') }

say $fails ?? "FAILED $fails" !! "PASS";
