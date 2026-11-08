# A type object where a needle goes — `.index(Str)`, `.indices(Str)`,
# `.contains(Int)` — is X::Multi::NoMatch, as Rakudo's dispatcher answers:
# no candidate takes (Str:D: Str:U). It died X::TypeCheck::Binding::Parameter
# naming a `$needle` parameter. Roast asserts only `dies-ok` (S32-str/index.t,
# indices.t). The message's capture names the invocant's definedness
# (`m(A:D: Str:D)`, where it said `m(A: Str:D)` for a user's method too) and a
# Bool named argument as the adverb it was (`:i`, `:!i`, not `:i(Bool)`).
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub first-line(&c) { c(); 'lived' }

for <index rindex indices contains starts-with ends-with> -> $m {
    try "abc"."$m"(Str);
    ck $!.^name, 'X::Multi::NoMatch', ".$m\(Str) dies X::Multi::NoMatch";
    ck $!.message.lines[0].substr(0, 40 + $m.chars),
       "Cannot resolve caller $m\(Str:D: Str:U); none of these signatures matches".substr(0, 40 + $m.chars),
       ".$m\(Str)'s message";
}
try 42.index(Str);
ck $!.message.starts-with('Cannot resolve caller index(Int:D: Str:U)'), True, 'a Cool invocant';
try "abc".index(Int);
ck $!.message.starts-with('Cannot resolve caller index(Str:D: Int:U)'), True, 'an Int type object';
try "abc".index(Str, 1);
ck $!.message.starts-with('Cannot resolve caller index(Str:D: Str:U, Int:D)'), True, 'with a position';
try "abc".index(Str, :i);
ck $!.message.starts-with('Cannot resolve caller index(Str:D: Str:U, :i)'), True, 'a True adverb';
try "abc".index(Str, :!i);
ck $!.message.starts-with('Cannot resolve caller index(Str:D: Str:U, :!i)'), True, 'a False adverb';
try index("abc", Str);
ck $!.^name, 'X::Multi::NoMatch', 'the sub form';

# a needle that is a value still works
ck "abc".index("b"), 1, '.index("b")';
ck "abcb".indices("b"), (1, 3), '.indices("b")';
ck 422.indices(2), (1, 2), 'a Cool needle';
ck "abc".contains("c"), True, '.contains';

# a user's multi method names its invocant's definedness too
class A { multi method m(Int) { } }
try A.new.m("x");
ck $!.message.starts-with('Cannot resolve caller m(A:D: Str:D)'), True, 'an instance invocant';
try A.m("x");
ck $!.message.starts-with('Cannot resolve caller m(A:U: Str:D)'), True, 'a type object invocant';
multi f(Int) { }; multi f(Str) { }
try f(1.5, :i);
ck $!.message.starts-with('Cannot resolve caller f(Rat:D, :i)'), True, 'a sub with an adverb';

say 'PASS';
