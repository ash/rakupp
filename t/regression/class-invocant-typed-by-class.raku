# An explicit `::?CLASS` invocant is typed by the class, as Rakudo types it:
# `method t(::?CLASS:D: $y)` has the signature `(E:D $:: $y, *%_)`, where it
# said `(Mu:D $:: $y, *%_)`, and its invocant's `.type` is E, where it was
# Mu. Dispatch on the smiley, a subclass's and a role's are as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class E {
    method t(::?CLASS:D: $y) { }
    multi method s(::?CLASS:U: $x) { "U" }
    multi method s(::?CLASS:D: Int $y) { "D" }
}
ck E.^find_method("t").signature.gist, '(E:D $:: $y, *%_)', 'the signature';
ck E.^find_method("t").signature.params[0].type.^name, 'E', "the invocant's type";
ck E.^find_method("s").candidates.map(*.signature.gist).List, ('(E:U $:: $x, *%_)', '(E:D $:: Int $y, *%_)'), "a multi's candidates";
class F is E { }
role R { method u(::?CLASS:D: $y) { 7 } }; class G does R { }
ck (E.s(1), E.new.s(2), F.s(1), F.new.s(2), G.new.u(1)), ('U', 'D', 'U', 'D', 7), 'dispatch, as before';

say 'PASS';
