# `.first`'s `$_` is the Array's element, as `.map`'s and `.grep`'s are and as
# Rakudo's is: `@a.first({ $_ = 9; True })` changed nothing and answered 1,
# where Rakudo writes 9 into the element and answers it. A typed array's
# element answers to its type. A List's and a matcher that is no block are
# as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my @a = 1, 2, 3;
ck @a.first({ $_ = 9; True }), 9, 'answers what it wrote';
ck @a.List, (9, 2, 3), 'and wrote it';
my @b = 1, 2, 3; @b.first({ $_ *= 10; $_ > 15 });
ck @b.List, (10, 20, 3), 'each element it tried';
my @c = 1, 2, 3; @c.first({ $_ = 7; True }, :end);
ck @c.List, (1, 2, 7), ':end';
my Int @t = 1, 2, 3;
ck (try { @t.first({ $_ = "x"; True }); 1 }) // $!.^name, 'X::TypeCheck::Assignment', 'a typed one refuses';
ck @t.List, (1, 2, 3), 'and is kept';
my int @n = 1, 2, 3; multi nat(int) { 'int' }; multi nat(Int) { 'Int' };
ck (nat(@n.first({ $_ > 1 })), nat(@n.first({ $_ = 5; True }))), ('int', 'int'), "a native's answer stays native";
ck @n.List, (5, 2, 3), 'and is written';
ck ((1, 2, 3).first({ $_ > 1 }), (1, 2, 3).first(* > 1, :k), @c.first(2)), (2, 1, 2), 'as before';

say 'PASS';
