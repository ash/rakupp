# `++` and `--` store .succ / .pred (Interpreter::stepValue), which is `+ 1`
# only for a number: a Bool's succ is True, a Str's its magic increment, an
# undefined variable's 1. The kernel's increment took `+ 1` once a value left
# the machine-Int lane, so `$b++` on False counted 1, 2, 3, and so did an
# element `@a[$i]++`. And a FatRat whose parts fit two words rode the Rat
# lane, so `$fr * 3 + 0` came back a Rat. Every answer here is Rakudo
# 2026.09's.
my $b = False; for ^200 { $b++ }; say "bool++ ", $b.raku;
my $c = True; for ^200 { $c-- }; say "bool-- ", $c.raku;
my $s = "a"; for ^200 { $s++ }; say "str++ ", $s;
my $z = "a9"; for ^30 { $z++ }; say "str-carry ", $z;
my $u; for ^200 { $u++ }; say "any++ ", $u;
my $n = 0.5e0; for ^200 { $n++ }; say "num++ ", $n;
my $r = 1/3; for ^200 { $r-- }; say "rat-- ", $r.raku;
my $big = 9223372036854775800; for ^200 { $big++ }; say "past int64 ", $big;
my @bs = False, True; for ^200 { @bs[0]++; @bs[1]-- }; say "elem bool ", @bs.raku;
my @ss = "x"; for ^200 { @ss[0]++ }; say "elem str ", @ss[0];
my @is = 1, 2; for ^200 { @is[1]++ }; say "elem int ", @is.raku;
my $post = False; my $seen; for ^200 { $seen = $post++ }; say "postfix value ", $seen.raku, " ", $post.raku;
my FatRat $fr = FatRat.new(1, 3); my $q; for ^200 { $q = $fr * 3 + 0 }; say "fatrat ", $q.^name, " ", $q.raku;
my $fa = FatRat.new(1, 7); for ^200 { $fa = $fa + 1 }; say "fatrat acc ", $fa.^name, " ", $fa.nude;
