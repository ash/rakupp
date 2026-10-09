# Regression, 2026-10-10: the small divergences TODO.md listed as one-function
# fixes, each answered as Rakudo 2026.08 answers it (the oracle on this box):
#   - `.WHICH` renders as the ObjAt it is; an enum member is `E|index`; a type
#     object is `Name|U…`
#   - a hole of an `is default(v)` array reads as v through `.kv`, `.pairs`,
#     `.grep`, `for`, `|@a`; an Array's `.kv` itemizes its nested containers
#   - `Sym(42)` with `class Sym is Str` dies; core type objects refuse `.Int`,
#     `.Num`, `.Rat` as Rakudo does (`Int.Int` is Int)
#   - `Int:D.HOW` is a DefiniteHOW
#   - an undeclared `$*name` in a regex is X::Dynamic::NotFound
#   - a Range with a fractional endpoint slices by its members
#   - `my Int $x is default(5)` still refuses a Str
#   - `anon subset`/`anon enum` claim no name
#   - a lazy Seq joins as `...`; `my @a = <lazy seq>` is an Array
#   - `.^can('elems')` finds two candidates on Seq, Range, Map and Hash
#   - `.raku` of a HyperSeq is its configuration; one over an endless source
#     stays a HyperSeq through `.map`
#   - `MY::<$x>` is the block's own lexical only
#   - `for @$list.kv -> $i, $x is rw` refuses a List's values
#   - a superscript power binds before a following postfix
use Test;
plan 45;

# .WHICH
is 5.WHICH.raku, 'ValueObjAt.new("Int|5")', '.raku of a ValueObjAt';
ok [1].WHICH.raku.starts-with('ObjAt.new("Array|'), '.raku of an ObjAt';
is Less.WHICH.Str, 'Order|0', 'an enum member is its index (Order)';
enum E <ea eb>;
is eb.WHICH.Str, 'E|1', '…and a user enum member';
ok Rat.WHICH.Str.starts-with('Rat|U'), 'a type object is Name|U…';
ok Rat.WHICH eq Rat.WHICH && Rat.WHICH ne Int.WHICH, '…stable and distinct per type';

# is default holes, and itemized containers in .kv
my @d is default(7); @d[2] = 1;
is @d.kv.raku, '(0, 7, 1, 7, 2, 1).Seq', '.kv of an is-default array';
is @d.pairs.raku, '(0 => 7, 1 => 7, 2 => 1).Seq', '.pairs of it';
is @d.grep(* > 0).raku, '(7, 7, 1).Seq', '.grep with a WhateverCode';
is @d.grep({ $_ > 0 }).elems, 3, '.grep with a block';
nok @d[0]:exists, '…which leaves the hole a hole';
my @seen; for @d { @seen.push: $_ }
is @seen.join(','), '7,7,1', 'for over it';
is (|@d).raku, 'slip(7, 7, 1)', '|@a';
is [[1, 2], 3].kv.raku, '(0, $[1, 2], 1, 3).Seq', 'an Array\'s .kv itemizes a nested Array';
is (1, [2, 3]).kv.raku, '(0, 1, 1, [2, 3]).Seq', '…a List\'s does not';

# coercion
class Sym is Str { }
throws-like { Sym(42) }, X::AdHoc, message => 'This type cannot unbox to a native string: P6opaque, Int';
is Sym('a').^name, 'Sym', 'a Str still makes a Sym';
throws-like { Str.Int }, X::Parameter::InvalidConcreteness, 'Str.Int';
throws-like { Num.Rat }, X::Parameter::InvalidConcreteness, 'Num.Rat';
throws-like { Rat.Num }, X::Parameter::InvalidConcreteness, 'Rat.Num';
is Int.Int.raku, 'Int', 'Int.Int is the type object';
is Str.Rat, 0.0, 'Str.Rat is 0.0 (as Rakudo)';
sub coerce-int(Int() $x) { $x }
throws-like { coerce-int(Str) }, X::AdHoc, message => "Cannot create an Int from a 'Str' type object";

# HOW, dynamic in regex, slices, typed default
is Int:D.HOW.^name, 'Perl6::Metamodel::DefiniteHOW', 'Int:D.HOW';
throws-like { "abc" ~~ / $*nope-not-declared / }, X::Dynamic::NotFound, 'undeclared $*name in a regex';
my @s = 1..5;
is @s[0..^2.5], (1, 2, 3), '@a[0..^2.5]';
is @s[0.5..2], (1, 2), '@a[0.5..2]';
my Int $td is default(5);
throws-like { $td = "s" }, X::TypeCheck::Assignment, 'is default keeps the declared type';
is $td, 5, '…and the default';

# anon declarations
my $as = anon subset AnonFoo of Int where * > 2;
lives-ok { EVAL 'subset AnonFoo of Str' }, 'anon subset claims no name';
my $ae = anon enum AnonBar <aa ab>;
is $ae.^name, 'Map', 'anon enum is a Map';
nok ::('AnonBar').defined, '…and installs no type name';

# lazy joins and lazy arrays
is (1, { $_ + 1 } ... *).join, '...', 'a lazy Seq joins as ...';
is (1, 2 ... *).join(','), '...', '…whatever it has cached';
my @la = (1, 2 ... *);
is @la.^name, 'Array', 'my @a = <lazy Seq> is an Array';
@la[2];
is @la.join(','), '1,2,3,...', '…and joins the prefix indexing reified';

# introspection
is Seq.^can('elems').elems, 2, 'Seq.^can(elems)';
is Range.^can('elems').elems, 2, 'Range.^can(elems)';
is Hash.^can('elems').elems, 2, 'Hash.^can(elems)';

# hyper
is (1..3).hyper.raku, 'HyperSeq.new(configuration => HyperConfiguration.new(batch => 64, degree => ' ~ (max 1, $*KERNEL.cpu-cores - 1) ~ '))', '.raku of a HyperSeq';
is (1..Inf).hyper.map(* + 1).^name, 'HyperSeq', 'endless .hyper.map';

# MY::
my $outer = 1;
my @my; for 1 { @my.push: MY::<$outer>.raku }
is @my[0], 'Nil', 'MY:: does not see an outer lexical';

# rw over a List
my $list = (1, 2, 3);
throws-like { for @$list.kv -> $i, $x is rw { $x = 5 } }, X::Parameter::RW, 'rw over a List .kv';

# superscripts
sub postfix:<!>($n) { [*] 1..$n }
is 3²!, 362880, '3²! is (3²)!';
is 4².sqrt, 4, '4².sqrt is (4²).sqrt';
