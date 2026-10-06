# Regression: a list of a TYPED Array's elements (`@t.reverse`) writes through
# to the array as an untyped one's does (array-views-write-through.raku), and
# the array's type checks the write, as a store into it does; `@a.List` and a
# literal list stay read-only.
#
# Expectations checked against Rakudo 2026.09 via /opt/homebrew/bin/rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

{ my Int @t = 1, 2, 3; my $r = @t.reverse; $r[0] = 5; ck(@t, Array[Int].new(1, 2, 5), 'a typed array') }
{ my Int @t = 1, 2, 3; my $r = @t.reverse;
  ck((try { $r[1] = "s"; 1 }) // $!.^name, 'X::TypeCheck::Assignment', 'a typed array checks the type') }
{ my @c = 1, 2, 3; ck((try { my $l = @c.List; $l[0] = 7; 1 }) // $!.^name, 'X::Assignment::RO', '@a.List stays read-only') }
{ ck((try { my $l = (1, 2, 3).reverse.List; $l[0] = 1; 1 }) // $!.^name, 'X::Assignment::RO', 'a literal list stays read-only') }
{ my int @n = 1, 2; my $q = @n.reverse; $q[0] = 9; ck(@n.List, (1, 2), 'a native array has no containers to write') }

say $fails ?? "FAILED $fails" !! "PASS";
