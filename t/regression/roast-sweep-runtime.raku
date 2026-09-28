# Run-time rules, from the 2026-09-28 partial-file sweep (roast
# S06-advanced/lexical-subs.t, S11-modules/import.t, S32-list/map.t,
# S04-phasers/in-loop.t, S03-operators/assign.t, context-forcers.t,
# S09-autovivification, S09-typed-arrays, S10-packages/basic.t,
# S12-construction/BUILD.t, S12-enums/thorough.t, S12-subset/type-subset.t,
# S32-hash/map.t, S32-list/deepmap.t, S32-str/comb.t, S16-io/print.t,
# S14-traits/routines.t, S02-types/quanthash.t, S02-types/pair.t,
# S04-declarations/{constant,our,will}.t, S04-statements/return.t,
# S02-types/whatever.t, S03-metaops/misc.t, S03-operators/inplace.t,
# S04-statements/try.t, S03-binding/attributes.t, S06-routine-modifiers/proxy.t,
# S10-packages/{precompilation,basic}.t, S14-roles/{basic,conflicts}.t,
# S32-list/classify.t, S32-array/create.t, APPENDICES/A01-limits/misc.t,
# APPENDICES/A02-some-day-maybe/misc.t, MISC/misc.t, 6.c A04-experimental,
# S12-attributes/instance.t, S32-hash/map.t, S12-introspection/attributes.t,
# S10-packages/scope.t, S17-supply/syntax.t, S05-grammar/inheritance.t,
# integration/advent2011-day11.t, integration/advent2012-day15.t,
# S14-traits/attributes.t, S14-traits/variables.t, S12-coercion/coercion-methods.t,
# S12-coercion/parameterized.t, integration/error-reporting.t).
# Every line answers the same under Rakudo 2026.08.

use MONKEY-SEE-NO-EVAL;
my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eqv $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got.raku}] WANT [{$want.raku}]";
    }
}
sub error-of(Str $code) { (try { EVAL $code; 'lived' }) // $!.^name }

# a sub with no scope declarator stays out of its package; so does `my constant`
package LexP { sub f { 1 }; our sub g { f() }; my constant yak = 2; constant ox = 3 }
check('a lexical package sub is not qualified', (try LexP::f()) // 'died', 'died');
check('…an our sub is', LexP::g(), 1);
check('my constant is not qualified', (try LexP::yak) // 'died', 'died');
check('…a bare constant is', LexP::ox, 3);

# `import` takes the EXPORT tags asked for
module ImpE { sub e1 is export(:A) { 1 }; sub e2 is export(:B) { 2 }; our sub eo { 3 } }
{
    import ImpE :B;
    check('import by tag brings the tag', e2(), 2);
    check('…and not another', (try EVAL 'e1()') // 'none', 'none');
}
check('import leaves an unexported our sub out', error-of('module ImpF { our sub fo { 1 } }; import ImpF; fo()'),
      'X::Undeclared::Symbols');

# LAST runs when a map block leaves with `last`
my $ranLAST;
my $it := (^20).map({ LAST $ranLAST = True; last if $_ == 10; $_ }).iterator;
Nil until $it.pull-one =:= IterationEnd;
check('LAST on last in map', $ranLAST, True);

# a `last` in a loop body's LEAVE ends the loop; a POST reads the block's value
my $out = '';
my $i = 0;
while $i++ < 6 { $out ~= $i; LEAVE { last } }
check('LEAVE { last }', $out, '1');
my @posts;
sub answer { 42 }
for ^3 { POST @posts.push($_); answer() }
check('POST sees the block value', @posts, [42, 42, 42]);

# assignment results: xor= and a symbolic `$` target answer one item
my $x;
my @p = $x xor= 42, 43;
check('xor= itemizes for a $ target', @p.elems, 1);

# a hyper call reads a Seq once
my \seq = 1, 2, 4 ... 16;
check('seq».abs twice', (try { seq».abs; seq».abs; 'lived' }) // 'died', 'died');
my \seq2 = 1, 2, 4 ... 16;
check('list passes a Seq on unread', (try { (list seq2)».abs; (list seq2)».abs; 'lived' }) // 'died', 'died');

# push autovivifies an undefined $ variable; a typed inner array stays typed
my $pv;
push $pv, 1, 2, 3;
check('push vivifies $a', $pv.raku, '$[1, 2, 3]');
my Array of Int @nested;
@nested[0].push: 3;
check('a typed inner array', @nested[0].WHAT.^name, 'Array[Int]');

# a module that inherits from nothing declared
check('module is NoSuch', (try { EVAL 'module ModNoSuch is NoSuch { }'; False }) // ($! ~~ X::Inheritance::UnknownParent), True);

# a Failure from BUILD is what .new answers
my class FailBuild { submethod BUILD { fail "noway" } }
my $fb = FailBuild.new;
check('fail in BUILD', $fb.defined, False);
check('…is a Failure', $fb.WHAT.^name, 'Failure');

# an enum's type object renders as the type; a value can take a mixin
enum EDay <Sun Mon>;
check('enum type gist', EDay.gist, '(EDay)');
check('enum type raku', EDay.raku, 'EDay');
my enum WithPriv <AA BB>;
AA does role { method !privy { 44 }; method m() { self!privy } }
check('mixin into an enum value', AA.m, 44);

# a `my subset` stays inside its module
check('my subset in a module', error-of('module SubM { my subset F where 5 }; my SubM::F $f = 5'),
      'X::Comp::Group');

# an element of a Hash lives in a Scalar
my %hv = :1a;
check('hash element .VAR', %hv<a>.VAR.^name, 'Scalar');

# deepmap: a typed array keeps its type when it can; Empty drops a hash key
my Str @ds = <a bb>;
check('deepmap to a List', @ds.deepmap(*.chars), (1, 2));
check('deepmap drops Empty values', { a => 1, b => 'x' }.deepmap({ $_ + 1 if $_ ~~ Numeric }), { a => 2 });

# a bare code block in a comb regex runs with $/
check('$/ inside comb', (gather 'abc'.comb(/. { take $/.Str } <!> /)).join(','), 'a,b,c');

# a junction format autothreads through sprintf
check('sprintf threads a junction format', (sprintf ('%d', ('%d',).any).all, 7).Str.contains('7'), True);

# a mixed-in role's @ attribute starts as an empty Array
my role RArr { has @.s is rw }
sub routine-with-attr { }
&routine-with-attr does RArr;
check('mixin @ attribute default', &routine-with-attr.s.^name, 'Array');

# `.values` hands out the hash's containers: a map writes through, and a
# QuantHash drops a key whose weight becomes 0
my %hv2 = a => 1, b => 2;
%hv2.values.map({ $_ = 10 });
check('values.map writes a Hash', %hv2<a b>, (10, 10));
my %sh := SetHash.new(<a b c>);
%sh.values.map({ $_ = 0 });
check('values.map to 0 empties a SetHash', %sh.elems, 0);

# Pair.new with a typed container checks what `.value` takes; a Pair holding
# a container is an object, so its (decontainerized) clone is another one
my $tp = Pair.new('foo', my Int $);
$tp.value = 42;
check('a typed Pair value takes an Int', $tp.value, 42);
check('…and refuses a Str', (try { $tp.value = 'bar'; 'lived' }) // $!.^name, 'X::TypeCheck::Assignment');
my $pv2 = 100;
my $lp := foo => $pv2;
check('a clone of a container Pair is another object', $lp.clone.WHICH === $lp.WHICH, False);

# `constant True` in a block shadows the literal there only
{
    constant True = 42;
    check('a local True', True, 42);
}
check('True outside it', True.^name, 'Bool');

# a no-initialiser `our` names the variable an earlier `our` published
{
    sub set-our() { our $ourv = 3 }
    set-our();
    check('our redeclared sees the value', (our $ourv), 3);
}

# BEGIN and CHECK keep their order across the statement and `will` spellings;
# a `return` in a CHECK has no routine to leave
my $phord;
{
    BEGIN $phord ~= "a";
    my $wb will begin { $phord ~= "b" };
    BEGIN $phord ~= "c";
    CHECK $phord ~= "f";
    my $wc will check { $phord ~= "e" };
    CHECK $phord ~= "d";
    check('BEGIN/CHECK order', $phord, 'abcdef');
}
check('return in a CHECK', error-of('sub { CHECK return; }'), 'X::Comp::BeginTime');

# a user infix over a parenthesized WhateverCode extends the curry it holds
multi sub infix:<quack>($x, $y) { "$x|$y" }
check('a user infix extends a curry', ((* quack *) quack 'a').(2, 3), '2|3|a');

# an OP= on an assignment resolves the inner one once; R-ops write their
# right side; an `@` container list-assigns what the op computes
my $na = 12;
((10 R+= ($na [R-]= 42)) //= 100) += 1000;
check('nested and reversed OP=', $na, 1040);
my @la = 20;
(@la ||= 42) += 10;
check('an @ target list-assigns an OP=', @la, [11]);
class HasA { has @.a is rw }
my $ha = HasA.new;
check('…through an accessor too', ($ha.a += <a b c>), [3]);
check('OP= on an immutable binding', error-of('my $ib := 42; ($ib //= 42) += 10'), 'X::Assignment::RO');
check('an array literal takes ,=', (try { [] ,= 42; 'lived' }) // 'died', 'lived');

# `with $x` binds the topic to $x itself; `with @a` binds the Array
my $wa = 'foo';
with $wa { .=uc; check('with aliases a scalar', $wa, 'FOO') }
my @wb = 'foo';
with @wb { .=uc; check('with binds an array', @wb, ['FOO']) }
(@wb.self) .= lc;
check('(@a.self) .= m stores into @a', @wb, ['foo']);
my ($dc, $db) = 0, 0;
my Int $dx;
do { $db++; ($dx .=new) }.="{$dc++; "new"}"(42);
check('a do block .= runs once', ($dx, $db, $dc), (42, 1, 1));

# `.resume` carries on at the statement after the one that died
my $rs = 0;
try { die "ohh"; $rs = 1; CATCH { default { .resume } } }
check('resume in a try block', $rs, 1);
my $rv = 0;
sub resumes { die "x"; $rv = 7; CATCH { default { .resume } } }
resumes();
check('resume in a sub body', $rv, 7);

# `has $.x does R` mixes the role into the attribute's initial value
my role AttrRole { method bar { 42 } }
class AttrDoes { has $.scalar does AttrRole; has @.array does AttrRole }
check('an attribute does a role', (AttrDoes.new.scalar.bar, AttrDoes.new.array.bar), (42, 42));

# a Proxy SUBCLASS built by the default constructor is the container itself,
# reached with `.VAR`, and keeps its attributes
class PxHist is Proxy { has @.history }
sub px-historize($value is copy) is raw {
    my $proxy := PxHist.new(
        FETCH => -> $ { $value },
        STORE => -> $, \new { $proxy.VAR.history.push($value); $value = new })
}
my $pxa := px-historize(42);
$pxa = 666;
check('a Proxy subclass reads and writes', $pxa, 666);
check('…and keeps its attribute', $pxa.VAR.history, [42]);
$pxa.VAR.history = ();
check('…which list-assigns', $pxa.VAR.history, []);

# a Method gists as its bare name; `is DEPRECATED<x>` is reported as the
# program ends, naming x
class GistM { method foo {} }
check('a method gists as its name', GistM.^methods.map(*.gist).grep('foo').elems, 1);
my $dep = run $*EXECUTABLE, '-e', 'sub f is DEPRECATED<g> { }; f()', :err;
check('DEPRECATED is reported at exit', $dep.err.slurp(:close).contains('Please use g instead'), True);

# a method's parameter type must be declared (qualified in a namespace the
# unit opens too); a role composing a role with the same attribute is refused
# when punned
check('an undeclared method parameter type', error-of('class ZZ { method foo(Blah $a) {} }'),
      'X::Parameter::InvalidType');
check('…a qualified one', error-of('role A::B { method foo(A::C $a) {} }'), 'X::Parameter::InvalidType');
check('a role-in-role attribute conflict', error-of('role Rc { has $.g }; role Sc does Rc { has $.g }; Sc.new'),
      'X::Role::Attribute::Conflicts');

# a Junction the classifier answers is ONE key of the Mu-keyed result,
# looked up as itself
my $jc := <abc axz abcdef axyf cbd xyz>.classify: *.contains: any 'a', 'f';
check('classify keys by the junction', $jc{any(True, True)}.sort, <abcdef axyf>.Seq);
check('…and no bare Bool key', $jc{True}.defined, False);

# a lazy array's clone shares its generator, not its storage; a qualified call
# into a module declared further down reaches it
my @lz = 1, { rand } … *;
my @lc = @lz.clone;
check('a lazy clone sees the same values', @lz[^8] eqv @lc[^8], True);
@lc[2] = 42;
check('…in storage of its own', @lz[2] == 42, False);
check('a forward call into a later module', FwdMod::answer(), 42);
module FwdMod { our sub answer() { 42 } }

# constrained multis from two roles do not conflict; a literal default the
# attribute's type can never hold, a default on a slurpy, and `:sym` on a sub
# name are refused; `x Inf` is a Failure; a coercion return type is answered
# as one, in both spellings
role RmA { multi method f(1) { 'one' } }
role RmB { multi method f(3) { 'three' } }
class RmC does RmA does RmB { }
check('constrained multis from two roles', RmC.new.f(3), 'three');
check('a literal default of the wrong type', error-of('class { has Int $.x = "42" }'),
      'X::TypeCheck::Attribute::Default');
check('…a coercion source of the wrong type', error-of('class { has Int(Rat) $.x = "42" }'),
      'X::TypeCheck::Attribute::Default');
check('a default on a slurpy', error-of('-> *@a = 42 { }'), 'X::Parameter::Default');
check('…on a capture', error-of('-> |c = 42 { }'), 'X::Parameter::Default');
check('`:sym` on a sub name', error-of('sub meow:sym<bar> { }'), 'X::Syntax::Reserved');
my $xinf = 'a' x Inf;
check('x Inf is a Failure', ($xinf ~~ Failure, $xinf.exception ~~ X::NYI), (True, True));
sub rcoerce(Num $x) returns Int(Str) { "$x" }
check('`returns Int(Str)` coerces', rcoerce(42e0).^name, 'Int');
check('…and is answered as the coercion type', &rcoerce.returns eqv Int(Str), True);

# a hash store puts each value in a Scalar container (and a Map does not);
# `$!a` in a method with no class around it is refused
my %hs = a => <a b c>;
my $hn = 0; $hn++ for %hs<a>;
check('a stored List is one item', ($hn, (my @hx = %hs<a>).elems), (1, 1));
class HsArr { has @.a }
check('…and so fills an @ attribute as one element', HsArr.new(a => %hs<a>).a.elems, 1);
my %ha = a => [1, 2, 3];
check('a Map has bare values', HsArr.new(|%ha.Map).a, [1, 2, 3]);
check('`$!a` in a free method', error-of('my $m = method { $!a }'), 'X::Comp::AdHoc');
check('`is copy` of an itemized List is an Array', do { my $k; for %hs.kv -> $, @v is copy { $k = @v.WHAT.^name }; $k }, 'Array');

# a Proxy handed to `.set_value` stays the container (its STORE runs later);
# `$?PACKAGE` in a lexical `my package` is that package
class PxSet { has $!a; method a() is rw { self.^attributes.head.get_value: self } }
my $pxs = PxSet.new;
my $pxstored = False;
my $pxv = 42;
$pxs.^attributes.head.set_value: $pxs, Proxy.new:
    FETCH => -> $ { $pxv }, STORE => -> $, \new { $pxstored = True; $pxv = new };
$pxs.a = 666;
check('set_value keeps the Proxy', ($pxstored, $pxs.a), (True, 666));
package PkgOuter { { my package My::Inner { our sub me { $?PACKAGE } }; our sub inner { My::Inner::me() } } }
check('$?PACKAGE of a my package', PkgOuter::inner().^name, 'PkgOuter::My::Inner');

# a pseudo-stash lookup sees a declaration further down the block (it exists,
# undefined, and shadows the outer one)
my $psx = 5;
{ my $r = EVAL(q[OUTER::.AT-KEY(q<$psx>).defined]); my $psx; check('OUTER:: sees a later declaration', $r, False) }

# a supply's CLOSE works on the running body's lexicals, so the consumer's
# `done` ends an emitting loop at once
my @clx;
my $cls = supply { until my $stop { @clx.push('x'); emit(++$) }; CLOSE { $stop = True } };
react { whenever $cls -> $n { done if $n >= 3 } }
check('CLOSE ends the emitting loop', @clx.elems, 3);

# an Array subclass fixing its element type types the variable it declares
my class StrArr is Array[Str] { }
my @sa is StrArr = <a b c>;
check('`is Array[Str]` keeps the element type', (@sa.of, (try { @sa[0] = 42; 'lived' }) // 'died'), (Str, 'died'));

# an EVAL inside a method checks its `self!name` calls as it compiles
class PrivChk { method good { EVAL 'self!there' }; method bad { EVAL 'return 1; self!nope' }; method !there { 7 } }
check('EVAL finds a real private method', PrivChk.new.good, 7);
check('…and refuses a missing one before running', (try { PrivChk.new.bad; False }) // ($! ~~ X::Method::NotFound), True);

# an INIT term inside a sub runs once, at program start, and each call reads its value
our @init-order;
sub note-init { @init-order.push('init'); 42 }
sub init-term { my $v = INIT note-init(); $v }
@init-order.push('main');
check('an INIT term in a sub runs at program start', (init-term(), init-term(), @init-order.join(',')), (42, 42, 'init,main'));

# a trait named by a TYPE takes the positional form, and what it mixes into the
# attribute's container is there in every instance
role Doc { has $.doc is rw }
multi trait_mod:<is>(Attribute $a, Doc, $arg) { $a.container.VAR does Doc($arg) }
class Documented { has $.dog is Doc('barks'); has @.birds is Doc('tweet'); has %.cows is Doc('moooo') }
my $documented = Documented.new;
check('a type-named attribute trait mixes into the container',
    ($documented.dog.VAR.doc, $documented.birds.doc, $documented.cows.doc), ('barks', 'tweet', 'moooo'));
# …and `.VAR` of a mixin is the mixin itself
role Hi { method hi { 'hi' } }
check('.VAR of a mixin keeps the role', ((5 but Hi).VAR.^name, (5 but Hi).VAR.hi), ('Int+{Hi}', 'hi'));

# a user trait on a VARIABLE gets the Variable, named with its sigil, and what it
# mixes into `.var` is on the variable afterwards
my @var-noted;
multi trait_mod:<is>(Variable:D $v, :$vnoted!) { @var-noted.push($v.VAR.name) }
role VDoc[Str:D $d] { has $.doc is rw = $d }
multi trait_mod:<is>(Variable:D $v, Str:D :$vdoc!) { $v.var.VAR does VDoc[$vdoc] }
my $vn1 is vnoted;
my @vn2 is vnoted;
my $vdog is vdoc('barks');
my %vcows is vdoc('moooo');
check('a variable trait sees the variable', @var-noted.List, ('$vn1', '@vn2'));
check('…and can mix into its container', ($vdog.VAR.doc, %vcows.VAR.doc), ('barks', 'moooo'));
# (a trait word that is really a sigilless type still leaves the declaration
# as the value of the code it ends)
my $vbuf;
for buf8 -> \bt { $vbuf = EVAL('my @a is bt = ^3') }
check('a declaration ending an EVAL is its value', $vbuf, buf8.new(^3));

# the coercion protocol: COERCE, then a `new` of the type's own (told why in
# $*COERCION-TYPE), and whatever answers must be the target type
my class CoBar { has $.value }
my class CoOne {
    has $.value; has Mu $.why;
    multi method new(::?CLASS:U: CoBar:D $b) { self.new: :value($b.value), :why($*COERCION-TYPE) }
    proto method COERCE(Any $) {*}
    multi method COERCE(Numeric:D $n) { self.new: :value($n * 2) }
    multi method COERCE(Bool:D $b) { CoOne.new: :value(!$b) }
}
my class CoTwo is CoOne { }
my CoOne(Any) $co1 = 21;
check('COERCE converts', $co1.value, 42);
$co1 = CoBar.new(:value<v>);
check('…then new, told why', ($co1.value, $co1.why.^name), ('v', 'CoOne(Any)'));
check('…and with neither it is impossible', (try { $co1 = 'x'; 1 }) // $!.^name, 'X::Coerce::Impossible');
my CoTwo(Any) $co2;
check('a COERCE answering the parent fails the child', (try { $co2 = True; 1 }) // $!.^name, 'X::Coerce::Impossible');
# a coercion type — smiley and all — as a role argument
my role CoR[::T] { method take(T $v) { $v } }
my class CoS does CoR[Str:D(Numeric)] { }
check('a role argument can be a coercion type', (CoS.new.take(pi), Str:D(Numeric).^name), (pi.Str, 'Str:D(Numeric)'));

# a signature's type capture is no class to inherit from — but a parametric
# role may name its parameter as the parent it hands on
check('inheriting from a type capture', (try EVAL q[-> ::RTC { class :: is RTC {} }; 1]) // $!.^name,
      'X::Inheritance::Unsupported');
my role RolePar[::T] is T { }
my class RoleParBase { method hi { 'base' } }
my class RoleParUser does RolePar[RoleParBase] { }
check('a role may be `is` its parameter', RoleParUser.new.hi, 'base');
# composing a stubbed CLASS is a composition error, and a broken promise's
# backtrace is a runtime one
check('does a stubbed class', (try EVAL q[class StubC { ... }; class StubD does StubC { }; 1]) // $!.^name,
      'X::Composition::NotComposable');
check('a broken promise backtrace is-runtime', (try { await start die 'x' }) // $!.backtrace.is-runtime, True);

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
