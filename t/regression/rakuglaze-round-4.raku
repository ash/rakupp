# Regression: the fourth Rakuglaze round — 38 snippets that differed from Rakudo.
#
# Each check is one root cause, reduced from the snippet that found it: a
# junction's definedness under `with`/`//`, `$n max= *.elems` currying the whole
# assignment, KEEP after a method's Nil, an EVAL unit's CATCH meeting an error
# that runs its handlers ahead of the unwinding, a `try` block being `use fatal`,
# object-hash values carrying their key into other hashes, Hash.push with a Slip,
# typed and coercing containers, anonymous enums, sub-signatures on slurpies and
# hash arguments, a proto refusing a named it has no place for, and more.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }
sub dies($code, $type, $what) {
    my $got = 'nothing';
    try { $code(); CATCH { default { $got = .^name } } }
    @fail.push("$what: died with $got, wanted $type") unless $got eq $type;
}

# a junction's definedness collapses, under `with`, `//`, `andthen`
ck (do with (1, Any).all { 'w' } else { 'e' }), 'e', 'with all(1, Any) is not taken';
ck ((Any, Any).any // 5), 5, '// on any(Any, Any)';
ck ((1, Any).all andthen 7), Empty, 'andthen on all(1, Any)';

# a `*` on the right of a compound assignment curries the whole assignment
{ my $n = 0; (1, 5, 2).map($n max= *).eager; ck $n, 5, '$n max= * in a map' }
{ my $n = 0; <a bb ccc>.map($n max= *.chars).eager; ck $n, 3, '$n max= *.chars' }

# KEEP runs on a DEFINED result only — in a method as in a sub
{
    my @ev;
    my class C { method m($w) { KEEP @ev.push($w); $w eq 'n' ?? Nil !! 1 } }
    C.m('a'); C.m('n');
    ck @ev, ['a'], 'KEEP skips a method that returns Nil';
}

# an EVAL'd unit's own CATCH handles what dies deeper down
{
    my $out = '';
    try { EVAL q[sub d { die "boom" }; d(); CATCH { when X::AdHoc { $out ~= 'unit' } }]; CATCH { default { $out ~= 'outer' } } }
    ck $out, 'unit', 'the EVAL unit CATCH takes it, not the outer one';
}

# a statement-modifier `my` in an EVAL'd unit is declared when skipped
ck EVAL(q[my $r = Nil unless 'x'.chars; $r // 'ok']), 'ok', 'EVAL `my $r = … unless …`';
ck EVAL(q[my ($c, Int() $d) = 'x', '42'; $d + 1]), 43, 'EVAL `my ($c, Int() $d) = …`';

# `try` puts its block under `use fatal` (and an EVAL inside it is not)
{
    sub f { fail 'x' }
    my $after = False;
    my $r = try { my $v = f(); $after = True; 5 };
    ck ($r, $after), (Any, False), 'a Failure from a call inside try throws';
    my $s = sub { try { fail 'y' }; 'done' }();
    ck $s, 'done', '`fail` in a try inside a sub is caught by the try';
    my $e = try { EVAL q[sub g { fail 'z' }; my $x = g(); 'survived'] };
    ck $e, 'survived', 'an EVAL inside a try is not fatal';
}

# 6.e Str whitespace methods
ck ("  ab \t".leading-whitespace, "  ab \t".trailing-whitespace, " \n".is-whitespace), ("  ", " \t", True), 'leading/trailing-whitespace, is-whitespace';

# `die` with an object that is not an Exception throws X::AdHoc carrying it
{
    my class X::NotAnException { method throw { die self } }
    dies { X::NotAnException.new.throw }, 'X::AdHoc', 'die of a non-Exception X:: class';
}

ck buf8.new(72, 101, 108).subbuf(0, 2).^name, 'Buf[uint8]', 'subbuf keeps the Buf type';
{ my grammar G { token statement { a } }; ck G.^can('statement').so, True, '.^can a grammar token' }

# an object hash's value does not carry its key into another hash
{
    my %o{Mu} = (Int) => 'i';
    my %by;
    %o.map({ %by{.key.^name} = .value });
    ck %by.keys.List, ('Int',), 'object-hash .value assigned elsewhere';
}

# Hash.push: a Slip is stored as it is, and slips into an existing Array
{
    my %r;
    %r.push: (a => slip('1')); %r.push: (b => slip('2')); %r.push: (a => slip('3'));
    ck (%r<a>.raku, %r<b>.raku), ('$["1", "3"]', '$(slip("2",))'), 'Hash.push with Slips';
}

{ my %p = w => 10; for %p.pairs -> \p { p.value = 6 }; ck %p<w>, 6, 'sigilless p.value = …' }
{ my %h = :!x; ck %h.pairs[0].raku, ':x(Bool::False)', 'a Hash Pair spells out its Bool' }
ck Hash[Str,Str]({}).^name, 'Hash[Str,Str]', 'Hash[Str,Str]({}) is that type';
{ sub e($s) { state %q is default({ $^str.uc }); %q<y>($s) }; ck e('abc'), 'ABC', 'state %h is default(…)' }

# lists: a lazy one has no .elems; a Seq does not bind to an @-name
dies { my @l = lazy gather { take 1; take 2 }; @l.elems }, 'X::Cannot::Lazy', '.elems of a lazy Array';
dies { my @l := (1, 2).map(* + 1); @l }, 'X::TypeCheck::Binding', 'binding a Seq to an @-name';

ck so(FatRat.new(1, 2**70).Rat), False, 'FatRat.Rat with a 70-bit denominator fails';

# an anonymous enum is a type its members step through
{
    my enum < A B C >;
    ck (B.pred, C.succ, A.pred.raku, B.^name, B ~~ Int), (A, C, '::A', '', True), 'anonymous enum';
    enum Col <R G>;
    ck R ~~ Int, True, 'an Int enum member is an Int';
}

ck EVAL(q[my $r = do { my enum <P Q>; Q.key }; { my class Q { } }; $r]), 'Q',
   'a `my class` in another block is no post-declaration of the name';

ck ('A'..∞)[25..27], <Z AA AB>, "'A'..∞ slices like 'A'..*";

# `has P() %.h` coerces each element at construction
{
    my class P { has $.n; multi method COERCE(%m) { self.new: |%m } }
    my class N { has P() %.h }
    ck N.new(h => { x => { n => 3 } }).h<x>.n, 3, 'has P() %.h coerces';
}

# a role mixed into a method with `does` answers its methods
{
    my role Acc { method is-acc { True } }
    my $m = method { }; $m does Acc;
    ck $m.?is-acc, True, '$method does Role';
}

# a class body's loop runs at declaration
{
    my class Base { for <get post> -> $n { ::?CLASS.^add_method($n, method ($u) { "$n.uc() $u" }) } }
    ck Base.new.post('/b'), 'POST /b', 'add_method from a for loop in a class body';
}

# an `is rw` CALL-ME is assignable, and a call's `=` is list assignment
{
    my class MV { has @.v; method CALL-ME($k) is rw { my @x := @!v; Proxy.new(FETCH => method () { @x }, STORE => method (*@n) { @x.append: @n }) } }
    my $mv = MV.new;
    $mv('k') = 7, 8;
    ck $mv.v, [7, 8], '$obj(…) = 7, 8';
}

# regexes
{ 'B 15 0 ;' ~~ m:s/ B [ (\d+) ]+ ';' /; ck [@0».Int], [15, 0], '@0 is @($0)' }
ck '{' ~~ /<:!L - [ \# \: ]>/ ?? 'y' !! 'n', 'y', '<:!L - […]>';
ck '#' ~~ /<:!L - [ \# \: ]>/ ?? 'y' !! 'n', 'n', '<:!L - […]> subtracts';
{
    my grammar S { token TOP { <w>+ %% <op> <op>? }; token op { '~' }; token w { \w } }
    ck S.parse('a~b~')<op>.elems, 2, 'a trailing %% separator is not captured twice';
}

# signatures
{
    sub n(&c where .signature ~~ :($, @) | :()) { 1 }
    dies { n(-> $a, $b { }) }, 'X::TypeCheck::Binding::Parameter', ':($, @) does not take -> $a, $b';
}
{
    multi infix:<->(Date:D $d, Str:D $x) { $d.earlier(days => +$x.substr(0, *-1)) }
    ck Date.new(2024, 3, 1) - '1d', Date.new(2024, 2, 29), 'a user Date - Str candidate';
    ck Date.new(2024, 3, 1) - 1, Date.new(2024, 2, 29), '…beside the built-in Date - Int';
}
{
    multi sub t( % ( :ident($)! where 'calc' ) ) { 'calc' }
    multi sub t( % ) { 'other' }
    ck (t({ ident => 'calc' }), t({ ident => 'rgb' })), ('calc', 'other'), 'a where inside a hash sub-signature';
}
{
    sub s(*@ids ($, *@)) { @ids.elems }
    dies { s() }, 'X::AdHoc', 'a slurpy sub-signature needs its one';
    sub o(*%o (Int :x($))) { %o<x> }
    dies { o(x => 'a') }, 'X::TypeCheck::Binding::Parameter', 'a named slurpy sub-signature types';
}
{
    proto sub pr($, :$enc) {*}
    multi sub pr($x, *%) { 'ok' }
    dies { pr(1, :bogus) }, 'X::AdHoc', 'the proto refuses a named it has no place for';
}
{
    my class L { multi method AT-POS(Int $i where * < 2) { "u$i" } }
    ck L.new[1], 'u1', 'the AT-POS multi';
    dies { ~L.new[3] }, 'X::OutOfRange', 'a declining AT-POS multi meets Any.AT-POS';
}
{
    sub ic(\it) { it.VAR.^name ne it.^name }
    my $s = 5;
    ck (ic($s), ic(5)), (True, False), 'a sigilless parameter knows its container';
}
{
    proto sub ij($) {*}
    multi sub ij(Junction $) { True }
    multi sub ij(Mu $) { False }
    ck (ij(1 | 2), ij(3)), (True, False), 'a Junction candidate takes the junction';
}

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
