# Rules from the last partial files of the 2026-09-28 Roast sweep (roast
# APPENDICES/A01-limits/overflow.t, S32-exceptions/misc.t, S32-basics/xxPOS.t,
# 6.c/S04-declarations/my-6c.t, S09-subscript/slice.t, S32-hash/slice.t,
# S03-metaops/hyper.t).
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

# a power whose exponent fits in 32 bits is computed, exactly; a wider one is a
# Failure (not a throw); an Int to a negative power below the smallest double is
# X::Numeric::Underflow, while a Rat's is a plain 0e0
check('an exponent past 900,000 is computed', (2 ** 1_000_000) ~~ Int, True);
my $rlf-over = 3 ** 2**32;
check('a 33-bit exponent is an Overflow Failure',
      ($rlf-over // 'handled', $rlf-over.exception.^name), ('handled', 'X::Numeric::Overflow'));
my $rlf-under = 2 ** -1075;
check('an Int power below the smallest double underflows',
      ($rlf-under.defined, $rlf-under.exception.^name, 2 ** -1074 == 5e-324), (False, 'X::Numeric::Underflow', True));
check('a Rat power below the smallest double is 0e0', 0.5 ** 2000, 0e0);
check('a Rat power past a 64-bit denominator is a Num', (1.0000001 ** 2000, 1.1 ** 700),
      (1.0002000199913315e0, 9.437992127672415e+28));
check('a Rat power stays reduced', ((2/3) ** -5).nude, (243, 32));
# a power gets a budget of work; past it, a Rat power whose answer is a Num
# anyway is computed straight as that Num — the digits rakudo's exact
# division gives (this one spends the budget first: some five seconds)
check('a Rat power past the work budget is computed as its Num', 1.0000001 ** 1_000_000, 1.1051709125497935e0);

# a type-like name before a statement's block that nothing declares is a call
# that swallowed the block: a compile-time group, whether or not the block runs
sub error-of(Str $code) { (try { EVAL $code; 'lived' }) // $!.^name }
check('an undeclared type before a block is a compile-time group',
      (error-of('CATCH { when X::Y {} }'), error-of('if 0 { given 1 { when Nope:D { } } }')),
      ('X::Comp::Group', 'X::Comp::Group'));
enum RLFColor <RLFRed RLFGreen>;
constant RLF-FOO = 42;
check('a declared or core name before a block is not',
      EVAL('given 42 { when X::AdHoc { "x" }; when RLFRed { "e" }; when RLF-FOO { "c" } }'), 'c');

# a sigilless invocant is the caller's container, so an `is raw` AT-POS that
# answers SELF.substr-rw can be assigned through; a Str mixin stays a mixin
role RLFStrIdx does Positional { method AT-POS(\SELF: $i) is raw { SELF.substr-rw($i, 1) } }
my $rlf-s = "asd" but RLFStrIdx;
$rlf-s[1] = "c";
check('an is-raw AT-POS over SELF.substr-rw is assignable', ($rlf-s eq 'acd', $rlf-s.^name), (True, 'Str+{RLFStrIdx}'));
class RLFInv { method m(\SELF:) { SELF = 5 } }
my $rlf-c = RLFInv.new;
$rlf-c.m;
check('a sigilless invocant is the caller\'s container', $rlf-c, 5);
my $rlf-m = "asd" but RLFStrIdx;
$rlf-m.substr-rw(0, 1) = "Z";
my $rlf-n = "asd" but RLFStrIdx;
my $rlf-p := $rlf-n.substr-rw(2, 1);
$rlf-p = "Q";
# OUTER:: is ONE scope, not that scope and outwards, and a `my` further down it
# already counts; a closure's scope is the one it was written in; inside a 6.c/
# 6.d EVAL, SETTING:: is the calling code, elsewhere it has no program variable
my $rlf-x = 0;
{
    check('OUTER:: sees a later my of its scope', EVAL('not OUTER::<$rlf-x>.defined'), True);
    check('an EVAL\'s SETTING:: is its caller', EVAL('not SETTING::<$rlf-x>.defined'), True);
    my $rlf-x; #OK not used
}
{ { check('OUTER:: is one scope', (OUTER::<$rlf-x>, OUTER::OUTER::<$rlf-x>, SETTING::<$rlf-x>), (Nil, 0, Nil)); } }
{
    my $rlf-x = 2;
    sub rlf-call(&f) { f() }
    check('a closure\'s OUTER:: is where it was written', (rlf-call({ $OUTER::rlf-x }), (1,).map({ $OUTER::rlf-x })[0]), (2, 2));
}

# a nested positional slice keeps its shape, adverbs and all, and a missing
# element drops out of its own level; a Range base is the List of its elements,
# and a List (or Range) past its end is Nil where an Array's is Any
check('a nested slice keeps its shape', (("a".."z")[(3, (4, (5,)))], (^10)[1..2, 3]),
      (("d", ("e", ("f",))), ((1, 2), 3)));
check('a nested adverbed slice keeps its shape',
      (("a".."z")[(3, (30, (5,)))]:kv, ("a".."z")[(3, (30, (5,)))]:!p, ("a".."z")[(3, (30, (5,)))]:exists),
      ((3, "d", ((5, "f"),)), (3 => "d", (30 => Nil, (5 => "f",))), (True, (False, (True,)))));
my @rlf-a = <a b c d e f>;
check('a nested :delete slice', (@rlf-a[(3, (30, (5,)))]:delete, @rlf-a.elems), (("d", (Any, ("f",))), 5));
check('a List past its end is Nil', ((1, 2, 3).AT-POS(5), (1, 2, 3)[1, 5]:!v, ("a".."z")[30], @rlf-a[1, 9]:!v),
      (Nil, (2, Nil), Nil, ("b", Any)));

# `my @s := @a[1, 2]` / `:= %h<b c>` binds the ELEMENTS' containers: assigning
# through @s writes the array or hash, one value per container, and answers
# itself; inside a list assignment `(@s, *) = …` it takes as many as it holds
my @rlf-arr = <a b c d>;
my @rlf-sl := @rlf-arr[1, 2];
check('a slice bound to @ writes through', (~(@rlf-sl = <A B C D>), @rlf-arr.join), ('A B', 'aABd'));
my %rlf-h = :a(1), :b(2), :c(3), :d(4);
my @rlf-hs := %rlf-h<b c>;
(@rlf-hs, *) = <X Y Z>;
check('a hash slice bound to @ writes through', (~@rlf-hs, %rlf-h<b c>.join), ('X Y', 'XY'));
@rlf-arr = <a b c d>;
my @rlf-s2 := @rlf-arr[1, 7];
@rlf-s2[0] = 'Q';
check('an element of a bound slice writes through', (@rlf-arr.join, @rlf-arr.elems, @rlf-s2[1]), ('aQcd', 4, Any));

# a NODAL routine in a hyper answers about each node and does not descend —
# the built-in &elems, a List method object, a `sub … is nodal`, and a
# qualified `».Any::elems`; `.+`/`.*` of one routine give one-element Lists
my @rlf-ll := <a b>, <c d e>;
my $rlf-m2 = ().^lookup('elems');
sub rlf-count(\x) is nodal { x.elems }
check('a nodal routine in a hyper stops at each node',
      (@rlf-ll».&elems, @rlf-ll».$rlf-m2, @rlf-ll».&rlf-count, @rlf-ll».Any::elems, @rlf-ll».?&elems),
      ((2, 3), (2, 3), (2, 3), (2, 3), (2, 3)));
check('.+/.* of one nodal routine', (@rlf-ll».+&elems, @rlf-ll».*Any::elems), (((2,), (3,)), ((2,), (3,))));
# List and Any each declare an `elems`, Any's being `self.list.elems`: `.+elems`
# finds both on a List, and Any's after a class's own
class RLFArr is Array { method elems { 42 } }
class RLFOwn { method elems { 7 } }
check('List and Any both declare elems',
      (@rlf-ll».+elems, [1, 2].*elems, RLFArr.new(1, 2).+elems, RLFOwn.new.+elems, "abc".+elems),
      (((2, 2), (3, 3)), (2, 2), (42, 2, 42), (7, 1), (1,)));

check('substr-rw into a Str mixin keeps the mixin',
      ($rlf-m eq 'Zsd', $rlf-m.^name, $rlf-n eq 'asQ', $rlf-n.^name), (True, 'Str+{RLFStrIdx}', True, 'Str+{RLFStrIdx}'));

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
