# Regression, 2026-10-11: small divergences from TODO.md, each answered as
# Rakudo 2026.09 answers it:
#   - a one-word q-family word list (`qw[a]`, `qqw[a]`, `qqww[a]`, `q:w[a]`)
#     is a one-element List; `<a>`, `«a»` and the `:v` forms stay the word
#   - `qq ｢…｣`, `q "…"`, `qw «…»`: a quote form takes a quote or Unicode
#     delimiter after a blank without an adverb
#   - `"X".lc("a")` and the other case maps and counts are arity errors
#   - `List(@a)` is `@a.List`: the Array itself, its nested Arrays whole
#   - a coercion type `Int(Any)` conforms as Int, and accepts its source type
#   - a Promise, a Lock or a handle is no Associative
#   - an OP= into a typed container's element checks (and coerces) the result
#   - a Seq bound to `Positional $p` is a List; a Capture binds no Array
#     (it does Positional, as Roast asserts and Rakudo does not yet)
#   - a WhateverCode handed to `..` through a variable does not curry
#   - a narrower nominal type wins a multi over a wider one with a `where`
#   - `temp` is restored before the block's LEAVE phasers run
#   - a built-in value does not do a user role that shadows a core one
#   - `f - 5` with `sub f` declared later in the file is `f(-5)`
#   - a Str joined to a Buf with `~` is X::Buf::AsStr; `utf8.new(…)` is a
#     utf8 (it was a Blob[uint8]), which decodes there
#   - a class's own BUILD that ignores an `is required` argument leaves the
#     attribute unprovided
use Test;
plan 43;

is qw[a].^name, 'List', 'qw[a] is a List';
is qqw[a].^name, 'List', 'qqw[a] is a List';
is qqww[a].^name, 'List', 'qqww[a] is a List';
is q:w[a].^name, 'List', 'q:w[a] is a List';
is <a>.^name, 'Str', '<a> is the word';
is «a».^name, 'Str', '«a» is the word';
is qw:v[1].^name, 'IntStr', 'qw:v[1] is the allomorph';
is qw[a b].elems, 2, 'two words are two';

my $x = 5;
is qq ｢a $x｣, 'a 5', 'qq ｢…｣ after a blank';
is q "b", 'b', 'q "…" after a blank';
is qw ｢a b｣.elems, 2, 'qw ｢…｣ after a blank';

for <uc lc tc tclc fc flip chars codes trim> -> $m {
    throws-like { "Xy"."$m"("a") }, X::AdHoc, message => /'Too many positionals'/, "\"Xy\".$m\(\"a\") dies";
}

my @w = [1, 2], [3, 4];
is List(@w).raku, '[[1, 2], [3, 4]]', 'List(@a) is the Array, nested Arrays whole';
is List((1, (2, 3))).raku, '(1, (2, 3))', 'List of a List is that List';
is List(1..3, 4).raku, '(1..3, 4)', 'several arguments are the list as given';

ok Int(Any) ~~ Int, 'Int(Any) ~~ Int';
ok "3" ~~ Int(Str), 'a coercion type accepts its source type';
nok 3.5 ~~ Int(Str), '…and nothing else';

nok Promise.new ~~ Associative, 'a Promise is no Associative';

{
    my Int %h;
    throws-like { %h<a> //= "x" }, X::TypeCheck::Assignment, '//= into a typed hash element checks';
    my Int @a = 1;
    try { @a[0] ~= "x" };
    is @a[0], 1, 'a refused ~= leaves the element alone';
    my Int() @b;
    @b[0] //= "3";
    is @b[0].^name, 'Int', '//= into a coercion-typed element coerces';
}

sub takes-pos(Positional $p) { $p.^name }
is takes-pos((1, 2).Seq), 'List', 'a Seq binds Positional $p as a List';
is takes-pos(\(1, 2)), 'Capture', 'a Capture does Positional (Roast S02-types/sigils-and-types.t; Rakudo todos it)';
sub takes-arr(Array $a) { $a }
throws-like { takes-arr(\(1, 2)) }, X::TypeCheck::Binding::Parameter, '…but it is no Array';

sub upto($n) { 1..$n }
is upto(* + 1).^name, 'Range', 'a WhateverCode from a variable is an endpoint';
my $curried = 1..*-1;
is $curried.^name, 'WhateverCode', 'a written one still curries';

multi m(Numeric $x where * > 0) { "num" }
multi m(Int $x) { "int" }
is m(5), 'int', 'the narrower type wins over a wider one with a where';

my $t = 1;
my $seen;
sub tl { temp $t = 2; LEAVE $seen = $t }
tl();
is $seen, 1, 'temp is restored before LEAVE';

nok EVAL('role Numeric { }; 5.does(Numeric)'), 'a user role shadowing a core one is not done by 5';

is (later - 5), -10, 'f - 5 with f declared later is f(-5)';
sub later($n) { $n * 2 }

throws-like { my $j = "a" ~ Buf.new(1) }, X::Buf::AsStr, 'Str ~ Buf dies';
is utf8.new(0x66, 0x6f).^name, 'utf8', 'utf8.new is a utf8';
is utf8.new(0x66, 0x6f) ~ 'x', 'fox', '…and decodes under ~';

class Req { has $.x is required; submethod BUILD() {} }
throws-like { Req.new(:x(5)) }, X::Attribute::Required, 'a BUILD that ignores the argument provides nothing';
