# A typed Array's element, written through the topic of `.hyper.map` or
# `.hyper.grep` or through `for @a.kv -> $k, $v is rw`, answers to its type,
# as Rakudo's does: each stored "x" into `my Int @a`, where the serial `.map`
# refused it. A typed Hash's value under `.kv` does too. What fits is
# written as before.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my Int @a = 1, 2, 3;
ck (try { @a.hyper.map({ $_ = "x" }).eager; 1 }) // ($! ~~ X::TypeCheck::Assignment), True, '.hyper.map';
ck (try { @a.hyper.grep({ $_ = "x"; True }).eager; 1 }) // ($! ~~ X::TypeCheck::Assignment), True, '.hyper.grep';
ck @a.List, (1, 2, 3), 'kept';
ck (try { for @a.kv -> $k, $v is rw { $v = "x" }; 1 }) // $!.message,
   'Type check failed for an element of @a; expected Int but got Str ("x")', '.kv';
ck @a.List, (1, 2, 3), 'kept again';
my Int %h = a => 1;
ck (try { for %h.kv -> $k, $v is rw { $v = "x" }; 1 }) // $!.message,
   'Type check failed for an element of %h; expected Int but got Str ("x")', "a hash's .kv";
@a.hyper.map({ $_ *= 10 }).eager;
ck @a.List, (10, 20, 30), 'what fits is written';
for @a.kv -> $k, $v is rw { $v += 1 }
ck @a.List, (11, 21, 31), 'through .kv too';
for %h.kv -> $k, $v is rw { $v = 5 }
ck %h<a>, 5, "and a hash's";

say 'PASS';
