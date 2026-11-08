# X::Cannot::Map's message ends with its suggestion, as Rakudo's does:
# `(1,2).map(5)` said "Cannot map a List using '5'" alone, where Rakudo goes
# on "\nDid a * (Whatever) get absorbed by a list?"; a List and a Hash given
# to `.map` get their own. `.suggestion` was already the line.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); $!.message }

ck msg({ (1, 2).map(5) }), "Cannot map a List using '5'\nDid a * (Whatever) get absorbed by a list?", 'an Int';
ck msg({ (1, 2).map("x") }), "Cannot map a List using '\"x\"'\nDid a * (Whatever) get absorbed by a list?", 'a Str';
ck msg({ my @a = 1, 2; @a.map(Int) }), "Cannot map a Array using 'Int'\nDid a * (Whatever) get absorbed by a list?", 'a type';
ck msg({ (1, 2).map((1, 2)) }),
   "Cannot map a List using a List\nDid a * (Whatever) get absorbed by a comma, range, series, or list repetition?\n"
   ~ "Consider using a block if any of these are necessary for your mapping code.", 'a List';
ck msg({ (1, 2).map({ a => 1 }) }), "Cannot map a List using a Hash\nDid you mean to add a stub (\{ ... }) or did you mean to .classify?",
   'a Hash';
try (1, 2).map(5);
ck $!.suggestion, 'Did a * (Whatever) get absorbed by a list?', '.suggestion';

say 'PASS';
