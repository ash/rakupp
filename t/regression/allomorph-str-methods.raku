# Regression: an allomorph is a Str, and a Cool matches on its Str (issue #212).
#
# `<42>.parse-base(16)` died with "No such method 'parse-base' for invocant
# of type 'IntStr'": the method answered a Str or a Match only. And a
# literal `.match` needle reached a plain Str invocant only, so
# `<42>.match("4")` and `42.match("4")` died the same way — which is what a
# MAIN argument that looks like a number is. Cool.match is
# `self.Str.match`, and a needle that is no Regex is matched as its Str.
# Every answer below is Rakudo 2026.09's.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck <42>.parse-base(16), 66, 'IntStr.parse-base';
ck <1.5>.parse-base(16), 1.3125, 'RatStr.parse-base parses its Str face';
ck <42>.Str.parse-base(16), 66, 'the workaround still works';
ck (try <0x10>.parse-base(16)).defined, False, 'the Str face is what is parsed: "0x10" is no base-16 number';
ck (try 42.parse-base(16)).defined, False, 'an Int is no Str: no parse-base';

ck <42>.match("4").Str, '4', 'IntStr.match with a Str needle';
ck <42>.match("x"), Nil, 'IntStr.match that fails is Nil';
ck 42.match("4").Str, '4', 'Int.match';
ck 4.5.match("4").Str, '4', 'Rat.match';
ck 1e0.match("1").Str, '1', 'Num.match';
ck (1+2i).match("1").Str, '1', 'Complex.match';
ck True.match("T").Str, 'T', 'Bool.match';
ck "42".match(4).Str, '4', 'an Int needle is matched as its Str';
ck "42".match(<4>).Str, '4', 'an IntStr needle too';
ck 42.match("4", :g).map(*.Str).List, ('4',), 'adverbs still apply';
ck <42>.match(/4/).Str, '4', 'a Regex needle is unchanged';
ck "abc".match("b").from, 1, 'a Str on a Str is unchanged';

# what a MAIN argument is
sub MAIN-like($x) { $x.match("4").Str }
ck MAIN-like(<42>), '4', 'a numeric-looking argument matches';

say 'PASS';
