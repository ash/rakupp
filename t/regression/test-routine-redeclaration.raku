# `use Test; sub ok(…)` compiled, where Rakudo says "Redeclaration of symbol
# '&ok'.": Test's routines are builtins here and `use Test` only set a flag,
# so no import met the local sub. A `use Test` with no import list now takes
# Test's exported names in its scope: a plain `sub`, an `our sub` or a `my &`
# of one is refused at compile time, before or after the `use`, and a proto
# of one is a routine redeclared. `use Test ()`, `need Test`, a multi, a
# method, an inner block's sub and a name Test does not export stay free.
# Every routine declared twice now has Rakudo's one wording, "Did you mean to
# declare a multi-sub?", where a proto said nothing more and a multi beside
# an only sub said "(multi/only mix)"; a proto after a multi of its name is
# refused too.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub refusal($code) {
    try { EVAL $code; return 'lived' }
    # (Rakudo groups a routine's with a second error of its own: the first line is the one)
    $!.^name eq 'X::Redeclaration' | 'X::Comp::Group' ?? $!.message.lines.head !! $!.^name ~ ': ' ~ $!.message
}

ck refusal('use Test; sub ok($x) { }'), "Redeclaration of symbol '&ok'.", 'sub ok';
ck refusal('use Test; our sub ok($x) { }'), "Redeclaration of symbol '&ok'.", 'our sub ok';
ck refusal('use Test; sub done-testing { }'), "Redeclaration of symbol '&done-testing'.", 'sub done-testing';
ck refusal('use Test; my &is = { }'), "Redeclaration of symbol '&is'.", 'my &is';
ck refusal('sub ok($x) { }; use Test;'), "Redeclaration of symbol '&ok'.", 'the sub before the use';
ck refusal('use Test; proto sub plan(|) { * }'),
   "Redeclaration of routine 'plan'. Did you mean to declare a multi-sub?", 'a proto';
try EVAL 'use Test; sub ok($x) { }';
ck ($!.symbol, $!.what), ('&ok', 'symbol'), 'its symbol and what';

ck refusal('use Test (); sub ok($x) { }; 1'), 'lived', 'use Test ()';
ck refusal('need Test; sub ok($x) { }; 1'), 'lived', 'need Test';
ck refusal('use Test; multi sub ok($x) { }; 1'), 'lived', 'a multi';
ck refusal('use Test; class C { method ok { } }; 1'), 'lived', 'a method';
ck refusal('use Test; { sub ok($x) { } }; 1'), 'lived', "an inner block's sub";
ck refusal('{ use Test; }; sub ok($x) { }; 1'), 'lived', "an inner block's use";
ck refusal('use Test; sub nope { }; 1'), 'lived', 'a name Test does not export';

my $twice = "Redeclaration of routine 'f'. Did you mean to declare a multi-sub?";
ck refusal('sub f { }; sub f { }'), $twice, 'two subs';
ck refusal('proto sub f(|) { * }; proto sub f(|) { * }'), $twice, 'two protos';
ck refusal('sub f { }; proto sub f(|) { * }'), $twice, 'a proto after a sub';
ck refusal('proto sub f(|) { * }; sub f { }'), $twice, 'a sub after a proto';
ck refusal('multi sub f { }; sub f { }'), $twice, 'a sub after a multi';
ck refusal('sub f { }; multi sub f { }'), $twice, 'a multi after a sub';
ck refusal('multi sub f { }; proto sub f(|) { * }'), $twice, 'a proto after a multi';
ck refusal('proto sub f(|) { * }; multi sub f { }; 1'), 'lived', 'a multi after a proto';

say 'PASS';
