# A subset declared in a package is named for it, `my` or not: `class K {
# my subset S of Int }` has S.^name K::S, where it said S (PDF::Content's
# SubsetFontLike is PDF::Content::FontObj::SubsetFontLike on Rakudo). Its
# `.raku` is the long name too, its gist and `.^shortname` the short one.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class K { my subset S of Int; method n { (S.^name, S.raku, S.gist, S.^shortname) } }
ck K.n, ('K::S', 'K::S', '(S)', 'S'), 'a `my subset` in a class';
my subset T of Int;
ck (T.^name, T.raku, T.gist), ('T', 'T', '(T)'), 'at the top (as before)';
module M { my subset U of Str; our sub n { U.^name } }
ck M::n(), 'M::U', 'in a module';
class K2 { subset V of Int; method n { (V.^name, V.gist) } }
ck K2.n, ('K2::V', '(V)'), 'an `our` one';
ck K2::V.^name, 'K2::V', '…by its long name (as before)';
role R { my subset W of Int; method n { W.^name } }
class X does R { }
ck X.n, 'R::W', 'in a role';
class A::B { my subset Q of Int; method n { Q.^name } }
ck A::B.n, 'A::B::Q', 'in a qualified class';
ck (5 ~~ K2::V), True, 'it still matches';

say 'PASS';
