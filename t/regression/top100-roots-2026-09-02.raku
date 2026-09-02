# Regression: the engine gaps behind the top-100 battery's root failures
# (docs/dev/ecosystem/ECOSYSTEM-TOP100.md, 2026-09-02 sitting). Every
# expectation is the Rakudo 2026.08 answer, taken from scratch probes run
# against both engines; each block names the dist that exposed the gap.

use nqp;

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- CBOR::Simple: an enum member beyond int64 keeps its value ----------------
enum Big (Max => 18446744073709551615, Min => -18446744073709551616);
ok(Max.value == 18446744073709551615, 'an enum value past int64 is not clamped');
ok(Min.value == -18446744073709551616, '…in either direction');
ok((-18446744073709551616 <= 5 <= Max.value), 'and it takes part in a chained compare');
# …and Mu is not an Any (the type object conforms only to Mu)
ok(nqp::istype(Mu, Any) == 0, 'nqp::istype(Mu, Any) is 0');
ok(nqp::istype(Any, Any) == 1, 'nqp::istype(Any, Any) is 1');
# …a Buf is not a Str, a utf8 is not a Buf
ok(nqp::istype(buf8.new(1), Str) == 0, 'a Buf is not a Str to nqp::istype');
ok(nqp::istype(buf8.new(1), Blob) == 1, '…but it is a Blob');
ok(nqp::istype("x".encode, Buf) == 0, 'a utf8 is not a Buf');
# …an Instant is Real but not a Num; a Rat is Rational
ok(nqp::istype(Instant.from-posix(5), Num) == 0, 'an Instant is not a Num');
ok(nqp::istype(Instant.from-posix(5), Real) == 1, '…but is Real');
ok(nqp::istype(<1/3>, Rational) == 1, 'a Rat is Rational');
# …a native array is an `array`, and names its element
my $na = array[uint8].new(1, 2);
ok(nqp::istype($na, array) == 1, 'array[uint8].new(…) is an array');
ok($na.^name eq 'array[uint8]', '…and its name carries the element type');
# …a map in diagnostic notation formats each Pair as (key, value)
ok((1 => 2, 3 => 4).fmt('%s: %s', ', ') eq '1: 2, 3: 4', 'List.fmt formats a Pair as key and value');

# --- Color::Names: a constant exported from a `unit class` body --------------
use lib 't/fixtures/modlib';
use Top100UnitClassConst :colors;
ok(COLORS<a> == 1, 'my constant \\X is export(:tag) in a unit class reaches the importer');
ok(PLAIN == 5, '…and the untagged spelling too');

# --- Hash::int: nqp::create on a user class, and nqp over a Pair ------------
class Created { has $!hash; method new() { nqp::p6bindattrinvres(nqp::create(self), self, '$!hash', nqp::hash) } }
ok(Created.new.^name eq 'Created', 'nqp::create(self) makes an instance of the class');
my $pr = 42 => "foo";
ok(nqp::istype($pr, Pair) == 1, 'a Pair is a Pair to nqp::istype');
ok(nqp::getattr($pr, Pair, '$!key') == 42, 'nqp::getattr reads a Pair key');
ok(nqp::getattr($pr, Pair, '$!value') eq 'foo', '…and its value');
ok(nqp::eqaddr(IterationEnd, IterationEnd) == 1, 'nqp::eqaddr on the same type object');
ok(nqp::hllbool(1) === True, 'nqp::hllbool makes a Bool');

# --- IO::Capture::Simple: a closure writing an `is rw` parameter two hops away,
# AFTER the frames returned, needs the container model (big-area #2) — left open
# rather than served by an eager rw-chain walk, which is O(n^2) where a cursor is
# threaded `is rw` through a recursion (JSON::Fast's `int $pos is rw`).

# --- Gnome::N: an attribute typed by a constant that aliases a type ----------
constant GType = uint64;
class Typed { has GType $.g; submethod TWEAK { $!g = 0 } }
ok(Typed.new.g == 0, 'has CONSTANT-ALIAS $.x assigns through the aliased type');

# --- Compress::Zlib: a CStruct field written from inside a method -----------
use NativeCall;
class Z is repr('CStruct') { has int32 $.a; has long $.b; method set { $!a = 5; $!b = 6 } }
my $z = Z.new; $z.set;
ok($z.a == 5 && $z.b == 6, '$!field = v inside a CStruct method writes native memory');

# --- Text::SubParsers: a bare `i` is a word, not the imaginary unit ----------
ok(val("i") ~~ Str && !(val("i") ~~ Numeric), 'val("i") is a Str');
ok(val("1i") ~~ Complex, 'val("1i") is still a Complex');
ok(!defined(try "is".Numeric), '"is".Numeric fails');

# --- CSS::Grammar: a Str-valued enum member is a Str -------------------------
our Str enum CSSObj «:AtRule<at-rule> :Prio<prio>»;
ok(AtRule ~~ Str, 'a Str-valued enum member smartmatches Str');
sub takes-str(Str :$type) { $type }
ok(takes-str(:type(AtRule)) === AtRule, '…and binds a Str parameter');
ok(~AtRule eq 'at-rule', '…and stringifies to its value');
enum IntE (n => 5);
ok(~n eq 'n', 'an Int-valued member still stringifies to its key');

# --- PSGI: a Blob assigned to an ordinary array is one item ------------------
my $body = "Hello".encode;
my @one = $body;
ok(@one.elems == 1, 'my @a = $blob keeps the blob whole');
my uint32 @native = "hi".encode;
ok(@native.elems == 2, '…while a NATIVE array takes its elements');
ok("héllo".encode.Str eq "héllo", 'utf8.Str decodes');

# --- Method::Protected: a method knows its package; a role has its own HOW ---
class Pkg { method m { } }
role Rl { method m { } }
ok(Pkg.^lookup('m').package.^name eq 'Pkg', 'Method.package is the declaring class');
ok(Rl.^lookup('m').package.^name eq 'Rl', '…or role');
ok(!(Rl.HOW.WHAT =:= Metamodel::ClassHOW), "a role's HOW is not a ClassHOW");
ok(Pkg.HOW.WHAT =:= Metamodel::ClassHOW, "a class's is");
my $refused = False;
my multi trait_mod:<is>(Method:D $m, :$only-classes!) {
    die "refused" unless $m.package.HOW.WHAT =:= Metamodel::ClassHOW;
}
try { EVAL q[role Rl2 { method x is only-classes { } }]; }
ok($! && $!.message eq 'refused', "a trait handler's own die reaches the caller");

# --- Font::AFM: `next` as an operand throws instead of going cooperative -----
sub walk(Hash $g) { my @seen; for <a b c> { my Str $s = $g{$_} // next; @seen.push($s) } @seen }
ok(walk({a => 'x', c => 'z'}) eqv ['x', 'z'], 'my Str $s = %h{$_} // next skips the miss');
# …and `$.method` on a type object is still a method call
class Metrics { method data { {b => 42} } method b { $.data<b> } }
ok(Metrics.b == 42, '$.name with a type object as self calls the method');

# --- PDF::Grammar / Intl::LanguageTag: newlines, quote words, closers --------
ok(so "a\r\nb" ~~ /a \n b/, '\\n matches the CRLF grapheme');
ok(so "\r\n" ~~ /^\s$/, '\\s consumes the CRLF grapheme whole');
ok(!("\r\n" ~~ /^<[\r]>$/), '<[\\r]> does not take a CRLF');
ok(so "\r\n" ~~ /^<[\n]>$/, '<[\\n]> does');
my @t = { a => :name<#> },
        { b => "/#6E" };
ok(@t.elems == 2 && @t[1]<b> eq '/#6E', 'a colonpair angle value may hold a hash sign');
class Selfy { has %!t = ms => 1; method AT-KEY(\k) { %!t{k} } method x { self<ms> } }
ok(Selfy.new.x == 1, 'self<ms> is a subscript, not the start of an m:s regex');

# --- Template::Mustache: comments inside code assertions, empty Slips -------
grammar Assert { token TOP { 'a' <?{
        # a comment with a } brace inside
        1
    }> } }
ok(so Assert.parse('a'), 'a # comment inside <?{ }> may hold a brace');
my regex sigils { (< # ^ / \< $ >) \h* }
ok(so '#' ~~ /<sigils>/, 'an escaped bracket inside a < word list > is a member');
ok(!Empty.defined, 'an empty Slip is undefined');
my $w = False;
my $ran = False;
with Nil // ('Warn' if $w) { $ran = True }
ok(!$ran, '…so `with` does not fire on it');

# --- Crane: with( glued to its paren is a call -------------------------------
sub apply(:&with!) { with(3) }
ok(apply(with => { $_ * 2 }) == 6, 'with(EXPR) calls a sub named with');

# --- Text::MiscUtils: the Unicode property trio -----------------------------
ok(nqp::getuniprop_str(0x61, nqp::unipropcode('East_Asian_Width')) eq 'Na', 'East_Asian_Width answers its short alias');
ok(nqp::getuniprop_str(0x1B, nqp::unipropcode('General_Category')) eq 'Cc', 'General_Category through getuniprop_str');
ok(nqp::getuniprop_bool(0x1F600, nqp::unipropcode('Emoji')) == 1, 'a boolean property through getuniprop_bool');

# --- NativeHelpers::Blob: pointer arithmetic in elements ---------------------
my $ca = CArray[uint16].new(10, 20, 30);
my $p = nativecast(Pointer[uint16], $ca);
ok($p.succ.deref == 20, 'Pointer.succ steps one element');
ok($p.succ ~~ Pointer[uint16], '…and keeps its type');
ok($p.add(2).deref == 30, 'Pointer.add steps n elements');
ok(CArray[uint8].new("abc".encode).elems == 3, 'CArray[uint8].new($blob) takes its bytes');

# --- Log::Async: END phasers run before the workers are torn down -----------
# (a program whose END starts a worker and waits on it used to hang at exit;
#  this file ending is the assertion — see runEnds() before drainWorkers())
my $end-ran = False;
END { my $p = start { 1 }; await $p; $end-ran = True; }

say $fails ?? "FAIL ($fails)" !! "PASS";
