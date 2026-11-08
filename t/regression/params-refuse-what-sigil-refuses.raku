# A parameter refuses what its sigil or type refuses, as Rakudo's binder
# does. `sub f(%h is rw)` is refused at compile time ("Can only use 'is rw'
# on a scalar ('$' sigil) parameter"), and so are `@a` and `&c`; a named `&c`
# takes a Callable only, as a positional one did; `Int %h` binds a hash typed
# so, not an untyped Hash or a Map ("You have to pass an explicitly typed
# hash"); `%h is copy` is a plain Hash whatever came (a Map, a Bag's
# {a => 2}); `my %h := (a => 1)` binds the Pair itself; and `given`/`with`
# `-> @a` takes a Positional only. Each was accepted.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub refused($code) { try EVAL $code; $! ~~ X::AdHoc ?? $!.message !! 'accepted' }
my $only = "Can only use 'is rw' on a scalar ('\$' sigil) parameter, not ";

ck refused('sub f(%h is rw) { }'), "For parameter '\%h', '\%' sigil containers don't need 'is rw' to be writable\n$only'\%h'",
   '%h is rw';
ck refused('sub f(@a is rw) { }'), "For parameter '\@a', '\@' sigil containers don't need 'is rw' to be writable\n$only'\@a'",
   '@a is rw';
ck refused('sub f(&c is rw) { }'), "$only'\&c'", '&c is rw';
ck refused('sub f(:%h is rw) { }'), "For parameter '\%h', '\%' sigil containers don't need 'is rw' to be writable\n$only'\%h'",
   'a named one';
ck refused('-> @a is rw { }'), "For parameter '\@a', '\@' sigil containers don't need 'is rw' to be writable\n$only'\@a'",
   'a pointy block';
ck refused('sub f($x is rw) { 1 }; 1'), 'accepted', '$x is rw';

sub r(:&c) { &c.^name }
ck (try r(:c(1))) // $!.message, "Type check failed in binding to parameter '\&c'; expected Callable but got Int (1)",
   'a named & given an Int';
ck (r(:c({ 1 })), r()), ('Block', 'Callable'), 'given a block, or nothing';

sub t(Int %h) { %h.^name }
my $typed = ". You have to pass an explicitly\ntyped hash, not one that just might happen to contain elements of the\ncorrect type.";
ck (try t(%(a => 1))) // $!.message,
   "Type check failed in binding to parameter '\%h'; expected Associative[Int] but got Hash$typed", 'Int %h given a Hash';
ck (try t(Map.new((a => 1)))) // $!.message,
   "Type check failed in binding to parameter '\%h'; expected Associative[Int] but got Map$typed", 'given a Map';
my Str %ts = a => 'x'; my $tsv = %ts;
ck (try t(%$tsv)) // $!.message,
   "Type check failed in binding to parameter '\%h'; expected Associative[Int] but got Hash[Str]", 'given a Hash[Str]';
ck (try t(bag <a>)) // $!.message,
   "Type check failed in binding to parameter '\%h'; expected Associative[Int] but got Bag", 'given a Bag';
my Int %ti = a => 1;
ck t(%ti), 'Hash[Int]', 'given a Hash[Int]';

sub c(%h is copy) { %h.^name }
ck (c(Map.new((a => 1))), c(%ti)), ('Hash', 'Hash'), '%h is copy is a Hash';
sub d(%h is copy) { %h }
ck d(bag <a a>), {a => 2}, 'of a Bag';
sub d2(%h is copy) { %h<z> = 1; %h }
my %orig = a => 1; d2(%orig);
ck %orig, {a => 1}, 'and a copy';

my %p := (a => 1);
ck (%p.^name, %p<a>), ('Pair', 1), '%h := a Pair';
ck (try { my Int %q := (a => 1); 1 }) // $!.message,
   "Type check failed in binding; expected Associative[Int] but got Pair$typed", 'Int %h := a Pair';

my @seen;
given [1, 2] -> @a { @seen.push(@a.^name) }
with (3, 4) -> @a { @seen.push(@a[1]) }
ck @seen, ['Array', 4], 'given and with -> @a';
ck (try { given 5 -> @a { }; 1 }) // $!.message,
   "Type check failed in binding to parameter '\@a'; expected Positional but got Int (5)", 'given 5 -> @a';

say 'PASS';
