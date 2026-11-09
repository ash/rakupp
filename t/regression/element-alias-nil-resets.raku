# A Nil stored through an alias of an untyped element resets it to Any, as
# a store into the element itself does and as Rakudo's Scalar does: `my @p =
# 1, 2; my $e := @p[0]; $e = Nil` left `[Nil, 2]`, and a Hash's element
# `{:a(Nil)}`. An `is default` and a typed element's alias were already
# right.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @p = 1, 2; my $e := @p[0]; $e = Nil;
ck (@p.raku, $e.raku), ('[Any, 2]', 'Any'), "an array's";
my %h = a => 1; my $f := %h<a>; $f = Nil;
ck %h.raku, '{:a(Any)}', "a hash's";
my @q = 1, 2; my $g := @q[4]; $g = Nil;
ck @q.raku, '[1, 2, Any, Any, Any]', 'past the end';
my @d is default(7) = 1, 2; my $h := @d[0]; $h = Nil;
ck @d.raku, '[7, 2]', 'a default, as before';
my Int @t = 1, 2; my $i := @t[0]; $i = Nil;
ck @t.raku, 'Array[Int].new(Int, 2)', 'a typed one, as before';
my @r = 1, 2; @r[0] = Nil;
ck @r.raku, '[Any, 2]', 'the element itself, as before';

say 'PASS';
