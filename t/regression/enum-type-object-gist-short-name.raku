# A qualified enum's type object gists by its short name, as a class's does:
# `enum B::Idx <X Y>; say B::Idx` is (Idx), where it printed (B::Idx). Its
# `.^name` and `.raku` keep the whole name, and `.^shortname` is Idx.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class A::B { }
enum B::Idx <X Y>;
enum Plain <p q>;
module M { our enum E <P Q>; }

ck A::B.gist, '(B)', 'a qualified class (as before)';
ck B::Idx.gist, '(Idx)', '.gist';
ck "{B::Idx.gist}", '(Idx)', '.gist interpolated';
ck (B::Idx, 1).gist, '((Idx) 1)', 'nested in a list';
ck [B::Idx].gist, '[(Idx)]', 'in an array';
ck Plain.gist, '(Plain)', 'an unqualified enum';
ck M::E.gist, '(E)', 'an enum declared in a module';
ck B::Idx.^name, 'B::Idx', '.^name keeps the whole name';
ck B::Idx.raku, 'B::Idx', '.raku too';
ck B::Idx.^shortname, 'Idx', '.^shortname';
ck X.gist, 'X', 'a member gists as its name';

say 'PASS';
