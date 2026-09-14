# Regression: the PDF batch of 2026-09-14 — the engine faults between rakupp and
# the PDF constellation (PDF::Grammar, PDF, PDF::Content, PDF::Class, PDF::API6),
# found by walking `rakupp install PDF::API6` from the bottom of its dependency
# tree upward.
#
# Parsing
#   * `sub prefix:</>` takes the slash away from the regex literal for the rest
#     of the file (PDF::API6 writes PDF names as `/'Outlines'`)
#   * `set`/`bag`/`mix` open a word list, so `'` and `"` in one stay words
#   * a sigilless TERM shadows a quote keyword — `\m` makes `m` a name, scoped
#     to the block it was declared for
#   * a package declarator used as a TERM (`package.^add_method(…)`)
#   * `!=~=`, the `%%` hash contextualizer, and `with`/`without` as term openers
#   * a smiley is GLUED to its type: `Str :U($x)!` is a named parameter
#   * `my (UInt \a, \b) = …` — a typed sigilless item in a declaration list
#   * a trailing `;` in a hash composer; a second statement makes it a block
#   * `sub a {…}, sub b {…}` — routine declarations strung with commas
#   * `is rw` survives on an ANONYMOUS routine term
#   * `$.name:sym<x>(…)` — a proto-regex candidate's name on the call
#   * `&.name(…)` is `self.name(…)`
#   * `method m handles <a b>` — a routine may delegate
#
# Runtime
#   * `\n` in a character class is the LOGICAL newline, and a class carrying a
#     codepoint escape still honours its `\d`/`\s`/`\w`/`\n` flags
#   * a proto-regex candidate whose body is ONE character is ranked (LTM)
#   * `Match.Rat` is exact — its TEXT read as a Rat
#   * a non-nodal hyper splices a Slip its method returned
#   * a native array is an `array`, and is NOT an Array or a List
#   * a role mixed in at runtime gives its attributes their TYPE OBJECT
#   * `.?meth` swallows a miss reported against an ancestor type
#   * a role composes the role chain of the roles it composes
#   * a plain sub installed with `^add_method` takes the invocant as its first
#     positional and keeps its closure's `self`
#   * a method `where` sees the invocant, so `where $x ~~ $!type` decides dispatch
#   * `.^is_pun` / `.^pun_source`
#
# …and the second sitting, from PDF's tie machinery through its serializer:
#   * an argument a SLURPY swallows is bound less specifically than one a
#     declared parameter takes
#   * `.^mixin` reblesses the object itself
#   * a Proxy assignment is worth what FETCH answers, not what STORE returned
#   * a leading dot after a prefix operator opens the OPERAND's term
#   * a built-in parent still counts when a composed role took the parent slot
#   * a built-in-backed class seeds its attributes' type defaults
#   * Nil RESETS a subset-typed attribute rather than failing its check
#   * a defaulted named's `where` decides dispatch instead of dying at the bind
#   * the ELSE branch of `with`/`without` aliases the topic
#   * a user class built on Attribute/Parameter is backed by a real meta-object
#
# …and the third sitting, from PDF's writer through a reopened document:
#   * a sub-signature made of NAMED params (`% (:$header!, :$body!)`) takes part
#     in multi dispatch
#   * `.new`/`.bless`/`.CREATE` on an INSTANCE construct another of its type
#   * a second `use Mod :&name` widens what an earlier, narrower one imported
#   * `my $v := $p.value` binds the Pair's CONTAINER — through a `do` block too
#   * `class T is Str { has $.value }` is the string it was built with
#   * `$obj<k> = v` is worth what the container HOLDS, not the right-hand side
#   * `$obj<k>++` goes through ASSIGN-KEY
#   * subscripting a Proxy an `is rw` method handed back reaches what it holds
#   * `temp` in a METHOD body is undone on the way out
#
# …and the fourth sitting, from a document's cross-reference table:
#   * a role mixed in at runtime composes the roles THAT role composes
#   * a SUBSET outranks the package-relative short name of a nested class
#   * an overriding `is rw` AT-POS/AT-KEY that `callsame`s is an assignment
#     target — its local must not escape as a pointer
#   * a NAMED argument is not an array index
#   * `seek`/`tell` on a `:bin` handle are BYTE offsets
#
# …and the fifth sitting, from the filters and the encryption dictionary:
#   * `|$obj` on a Hash-derived object slips its entries as NAMED arguments
#   * a type parameter may NAME a constant (`constant W = uint32; Buf[W]`)
#   * `INIT` takes a whole STATEMENT as its operand, as `do` does
#   * a native-typed ARRAY is a container and may be bound
# (two more from that sitting are not pinned here: a class COMPOSING Blob
# marshals its bytes to a native call — which needs a library to call — and
# `eqv` no longer follows a cycle forever, which Rakudo itself does not
# survive, so the comparison has nowhere to run.)
#
# …and the sixth sitting, from a PDF date to the class it has to become:
#   * `::?CLASS` in a PARAMETER is the enclosing class, not Mu
#   * a `where` is evaluated in the scope its SIGNATURE was written in
#   * `nextwith` from a built-in-backed class's `.new` answers that class
#   * a constructor redispatches on the built-in TYPE, however it was invoked
#
# Runs clean under Rakudo too.

use lib $?FILE.IO.parent.add('../fixtures/pdf-util-lib').Str;
use PdfProbe::First;                                   # asks for two of three…
use PdfProbe::Util :&to-ast, :&ast-coerce, :&from-ast; # …and this one for all three

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- set/bag/mix take a word list --------------------------------------
ck (set <T* Td TD Tj TJ Tm ' ">).keys.elems, 8,
   'a set word list holds a quote as a word';
ck ?(set <T* Td ' ">){"'"}, True, '…and that word is the apostrophe itself';
ck (set <m l c v y h re>).keys.sort.join(','), 'c,h,l,m,re,v,y', 'a plain set word list';
ck (bag <x y x>)<x>, 2, 'bag takes one too';

# ---- a sigilless name shadows a quote keyword, scoped ------------------
sub cmyk(\c, \m, \y, \k) { [ c, m, y, k ] }
ck cmyk(1,2,3,4), [1,2,3,4], 'a `\m` parameter is a term, not a match';
ck ('DeviceRGB' ~~ m/^Device('RGB'|'Gray')$/) ?? ~$0 !! 'no', 'RGB',
   '…and `m//` is a match again outside that block';

# ---- a package declarator used as a term -------------------------------
class PkgTarget { method f($b) { "f:$b" } }
{
    my \package = PkgTarget;
    ck package.f(42), 'f:42', 'a term called `package`';
}

# ---- operators ----------------------------------------------------------
ck (1.0 !=~= 2.0), True,  'the negated approximate-equality metaop';
ck (1.0 !=~= 1.0), False, '…and it says no when they are close';
constant %Roman = %( :M(1000), :I(1) );
my $num = 0;
$num += $_ with %%Roman{'I'};
ck $num, 1, 'the %% hash contextualizer after `with`';

# ---- signatures ---------------------------------------------------------
sub crypt-load(Str :O($owner)!, Str :U($user)!, UInt :$Length = 40) { "$owner/$user/$Length" }
ck crypt-load(:O<o>, :U<u>), 'o/u/40', 'a spaced :U is a NAMED parameter, not a smiley';
sub tight(Str:U $t) { 'undefined ok' }
ck tight(Str), 'undefined ok', '…and a glued one is still the smiley';
my (UInt \pa, UInt \pb, \pc) = (1, 2, 3);
ck (pa + pb + pc), 6, 'a typed sigilless item in a `my (…)` list';

# ---- hash composer / routine terms -------------------------------------
my $composed = { :a(1), :b(2); };
ck $composed.WHAT.^name, 'Hash', 'a trailing `;` leaves a hash composer a Hash';
ck $composed.keys.sort.join(','), 'a,b', '…with both of its keys';
sub FETCH-x($) { 1 },
sub STORE-x($, $_) { 2 }
ck (&FETCH-x(0) + &STORE-x(0,0)), 3, 'two routine declarations strung with a comma';
my $x = 1;
my &rwsub = sub () is rw { $x }
rwsub() = 5;
ck $x, 5, '`is rw` survives on an anonymous routine term';

# ---- proto-regex candidate names on the call ---------------------------
class SymAct {
    method numeric:sym<frac>($v) { "frac:$v" }
    method go($v) { $.numeric:sym<frac>($v) }
}
ck SymAct.new.go(7), 'frac:7', '`$.name:sym<x>(…)` names the candidate';

# ---- regex character classes -------------------------------------------
ck ?("\r"     ~~ /<[ \x20 \x0A \x0 \t \f \n ]>/), True, 'class `\n` is the logical newline';
ck ?("\x0B"   ~~ /<[\n]>/),                       True, '…vertical tab too';
ck ?("5"      ~~ /<[ \x41 \d ]>/),                True, 'a class with a codepoint escape keeps its flags';
ck ?(" "      ~~ /<[ \x41 \s ]>/),                True, '…\s as well';
ck ?("a"      ~~ /<[ \x41 \d ]>/),                False, '…and still says no';

# ---- LTM ranks a one-character candidate --------------------------------
grammar Lit {
    proto token lit {*}
    token lit:sym<x>   { x }
    token lit:sym<nest> { '[' <lit> ']' }
}
ck ?Lit.parse('x',   :rule<lit>), True, 'a recursive proto ranks its plain candidate';
ck ?Lit.parse('[x]', :rule<lit>), True, '…and its recursive one';

# ---- Match.Rat is exact -------------------------------------------------
ck ("0.0648041" ~~ /\d+\.\d+/).Rat, 0.0648041, 'Match.Rat reads its TEXT';

# ---- a hyper splices a Slip its method returned -------------------------
class Slipper { has $.v; method ast { ($!v, "x").Slip } }
ck (Slipper.new(v=>1), Slipper.new(v=>2))>>.ast.List, (1, "x", 2, "x"),
   'a non-nodal hyper slips what the method returned';

# ---- native arrays ------------------------------------------------------
my uint64 @nat = 1, 2, 3;
ck ?(@nat ~~ array),  True,  'a native array IS an array';
ck ?(@nat ~~ List),   False, '…and is NOT a List';
ck ?(@nat ~~ Array),  False, '…nor an Array';
ck ?(@nat ~~ Positional), True, '…but it is Positional';
multi sub which(array:D $a) { 'array' }
multi sub which(List:D $a)  { 'list' }
multi sub which(Mu $a)      { 'mu' }
ck which(@nat), 'array', 'and dispatch picks the `array` candidate';

# ---- a runtime mixin gives its attributes their type object ------------
class Plain { has $.anything }
role Typed { has Int $.n is rw; has Str $.s is rw }
my $mixed = Plain.new;
$mixed does Typed;
ck $mixed.n.WHAT.^name, 'Int', 'a mixed-in role attribute defaults to its type object';
ck $mixed.s.WHAT.^name, 'Str', '…for every attribute it brings';

# ---- .? over an ancestor-typed miss ------------------------------------
class HashBacked is Hash { method probe { self.?nope } }
ck HashBacked.new.probe, Nil, '`.?` answers Nil on a Hash-backed class';

# ---- a role composes its own role chain ---------------------------------
role Inner2 { method inner { 'inner' } }
role Outer2 does Inner2 { method outer { 'outer' } }
class OnHash is Hash does Outer2 { }
ck OnHash.new.inner, 'inner', 'a role of a role reaches a Hash-backed class';
ck OnHash.new.outer, 'outer', '…alongside the role that composed it';

# ---- a sub installed as a method ---------------------------------------
class Installed { }
my &as-method = sub (\obj) { "got:{obj.^name}" }
Installed.^add_method('named', &as-method);
ck Installed.new.named, 'got:Installed', 'a sub-as-method takes the invocant first';

# ---- a method `where` sees the invocant --------------------------------
class WhereSelf {
    has $.type = Str;
    multi method pick($v where $v ~~ $!type) { 'typed' }
    multi method pick($v) { 'generic' }
}
ck WhereSelf.new.pick('s'), 'typed',   'a method where reads the invocant';
ck WhereSelf.new.pick(42),  'generic', '…and loses the candidate when it should';

# ---- routine-level handles ---------------------------------------------
class Delegatee { method work($x) { "worked:$x" } }
class Delegator {
    has $!inner;
    method loader is rw handles <work> { $!inner //= Delegatee.new }
}
ck Delegator.new.work(5), 'worked:5', '`method … handles <…>` delegates';

# ---- pun introspection --------------------------------------------------
class NotAPun { }
ck ?NotAPun.^is_pun, False, 'an ordinary class is not a pun';

# ======================================================================
# The second sitting: the faults between PDF's tie machinery and its
# serializer, found by walking t/00-helloworld.t line by line.
# ======================================================================

# ---- a slurpy binds an argument less specifically than a parameter -----
class Coercer {
    multi method co($a, $b)  { "two" }
    multi method co(%h!, |c) { "hash-plus-capture" }
}
ck Coercer.new.co({}, Int), 'two',
   'two declared positionals beat one parameter plus a capture';
# …and a position ONE candidate declares while the other leaves it to a capture
# is not compared at all — Rakudo only weighs the parameters both candidates
# declare. The pair therefore sits in the same band, where declaration order
# settles it, and it must settle it in BOTH directions: scoring the swallowed
# position as merely "wider" handed every such call to the declared parameter
# and reversed base64-sextet.raku's adverb chain.
my @band;
proto sub banded(|) {*}
multi sub banded(Str:D $s, |c)      { @band.push('pos')   }
multi sub banded(Bool:D :$pad!, |c) { @band.push('named') }
banded("ab", :!pad);
ck @band.join(','), 'pos', 'a positional declared first keeps a call a capture could take';
my @band2;
proto sub banded2(|) {*}
multi sub banded2(Bool:D :$pad!, |c) { @band2.push('named') }
multi sub banded2($s, |c)            { @band2.push('pos')   }
banded2("ab", :!pad);
ck @band2.join(','), 'named', '…and a required named declared first keeps its own';

# ---- `.^mixin` mixes IN PLACE -----------------------------------------
role Mixed { method mixed { "mixed" } }
class Host { has $.v = 1 }
my $host = Host.new;
my $alias = $host;
$host.^mixin(Mixed);
ck ?($host ~~ Mixed),  True, '.^mixin reblesses the object itself';
ck ?($alias ~~ Mixed), True, '…so every reference to it sees the role';
ck $host.mixed, 'mixed', '…and the role\'s methods answer';

# ---- a Proxy assignment answers what FETCH gives -----------------------
my $behind;
my $prox := Proxy.new(
    FETCH => sub ($) { "fetched:{$behind // 'unset'}" },
    STORE => sub ($, \v) { $behind = v; 99 },
);
ck ($prox = "abc"), "fetched:abc", 'a Proxy assignment is worth what FETCH answers';

# ---- a leading dot opens the PREFIX OPERATOR's operand ------------------
class Numbered { method num { 7 } }
sub pfx($_) { (? .num, + .num, ~ .num, - .num) }
ck pfx(Numbered.new), (True, 7, "7", -7),
   'a leading-dot operand belongs to the prefix, not its result';
ck (^30 .elems), 30, '…while an operand already complete keeps the postfix';

# ---- a built-in parent behind a composed role ---------------------------
role Tagged { method tagged { "tagged" } }
class StrFirst is Str does Tagged { }
class RoleFirst does Tagged is Str { }
ck ?(StrFirst.new(value => "a")  ~~ Str), True, '`is Str does R` is a Str';
ck ?(RoleFirst.new(value => "a") ~~ Str), True, '…and so is `does R is Str`';

# ---- typed attribute defaults on a built-in-backed class ----------------
role Numbered2 { has Int $.obj-num is rw }
class OnHash2 is Hash does Numbered2 { has Str @.names }
ck OnHash2.new.obj-num.WHAT.^name, 'Int', 'a typed attribute defaults to its type object';
ck OnHash2.new.names.of.^name,     'Str', '…and a typed container is that container';

# ---- Nil RESETS a subset-typed attribute --------------------------------
class Resettable { has UInt $.prev = 3; method clear { $!prev = Nil; $!prev.WHAT.^name } }
ck Resettable.new.clear, 'UInt', 'Nil resets a subset-typed attribute to its type object';

# ---- a defaulted named`s `where` decides DISPATCH ------------------------
class Saver {
    has $.indexed = False;
    multi method save(Str $f, Bool :quick($) where .so && $!indexed = True) { "incremental" }
    multi method save(Str $f, :quick($)) { "full" }
}
ck Saver.new.save('x'),          'full',        'a failing where on a defaulted named loses the candidate';
ck Saver.new(:indexed).save('x'), 'incremental', '…and a satisfied one wins it';

# ---- the ELSE branch of with/without aliases the topic -------------------
my %store;
with %store<k> { } else { $_ = "set-in-else" }
ck %store<k>, "set-in-else", 'the else branch of `with` writes through the container';
my %untouched;
with %untouched<k> { } else { }
ck %untouched.elems, 0, '…and does not autovivify when it writes nothing';
class HashHost is Hash { }
my $hh = HashHost.new;
with $hh<k> { } else { $_ = 5 }
ck $hh<k>, 5, '…through a Hash-backed object too';

# ---- a user class built on the Attribute meta-object ---------------------
role Described { has $.described is rw }
my class MyAttr is Attribute does Described { }
my $myattr = MyAttr.new: :name('@!ID'), :type(Str), :package<?>;
ck $myattr.name, '@!ID', 'a class built on Attribute keeps its name';
ck $myattr.type.^name, 'Str', '…and its type';

# ---- a NAMED sub-signature takes part in multi dispatch ------------------
# PDF::IO::Writer tells its two `stream-cos` arms apart with `% (:$header!,
# :$body!)`, and the one-argument arm calls the two-argument one.
class Writer {
    has %.ast = :cos{ :header{:type<PDF>}, :body[1] };
    multi method cos($fh, % (:$header!, :$body!)) { "two:{$header<type>}" }
    multi method cos($fh) { 'one -> ' ~ $.cos($fh, %!ast<cos>) }
}
ck Writer.new.cos('fh'), 'one -> two:PDF', 'a named sub-signature scores as a candidate';

# ---- `.new` on an INSTANCE constructs another of its type ----------------
# `self.new!open-file: $spec` is how a PDF reopens a document from one of its
# own instances; the invocant is a Hash-backed object, not the type.
class Trailer is Hash { method kind { 'trailer' } }
ck Trailer.new.new.kind,    'trailer', '.new on an instance builds its own type';
ck Trailer.new.bless.kind,  'trailer', '…and so does .bless';
ck Trailer.new.CREATE.kind, 'trailer', '…and .CREATE';

# ---- a later `use` widens what an earlier one imported -------------------
ck PdfProbe::First.go, 'from:1 coerce:2', 'the first importer gets its two names';
ck to-ast(3),     'to:3',     'a later, wider `use` imports the name it asks for';
ck ast-coerce(4), 'coerce:4', '…alongside the ones already imported';
ck from-ast(5),   'from:5',   '…and the rest of them';

# ---- `:=` binds a Pair's value CONTAINER --------------------------------
# PDF's serializer registers an empty dictionary node against cyclic references
# and fills it in afterwards, through a binding taken inside a `do` block.
my Hash $dict1;
my $node1 = :dict($dict1);
my $nv1 := $node1.value;
$nv1 = %( :A(1) );
ck $node1.value<A>, 1, 'a bound Pair value writes through to the pair';

my Hash $dict2;
my $node2;
my $nv2 := do with Nil { 0 } else { $node2 = :dict($dict2); $node2.value };
$nv2 = %( :B(2) );
ck $node2.value<B>, 2, '…and so does one bound through a do/else block';

# ---- a Str subclass IS the string it was built with ----------------------
class TextString is Str { has $.value; has Str $.type is rw = 'literal' }
my $ts = TextString.new: :value<probe>;
ck ($ts eq 'probe'), True, 'a `is Str` class with its own $.value IS that string';
ck $ts.chars,   5,       '…and measures as it';
ck $ts.value,   'probe', '…while keeping the attribute';

# ---- the container protocol on a class that ties what it stores ----------
class Tied is Hash {
    method AT-KEY($k) is rw { callsame }
    method ASSIGN-KEY($k, $v) { self.BIND-KEY($k, 'tied:' ~ $v) }
}
my $tied = Tied.new;
my $stored = ($tied<K> = 'v');
ck $stored,   'tied:v', 'an assignment is worth what the container holds';
ck $tied<K>,  'tied:v', '…which is what a read answers too';

class Counter is Hash {
    method AT-KEY($k) is rw { callsame }
    # binds a CONTAINER, as PDF's `$.lvalue($val)` hands one to its own BIND-KEY
    method ASSIGN-KEY($k, $v) { my $cell = $v; self.BIND-KEY($k, $cell) }
}
my $count = Counter.new;
$count<n> = 0;
$count<n>++;
ck $count<n>, 1, '`$obj<k>++` steps the value the container holds';

# ---- the ELSE branch writes through an object's own ASSIGN-KEY ----------
my $idobj = Counter.new;
with $idobj<ID> { .[1] = 'upd' } else { $_ = [ 'x' xx 2 ] }
ck $idobj<ID>, ['x', 'x'], 'a `with`/`else` topic write reaches ASSIGN-KEY';

# ---- a Proxy an `is rw` method hands back is a CONTAINER ----------------
class Holder {
    has Counter $!store = Counter.new;
    method store is rw {
        sub FETCH($)     { $!store }
        sub STORE($, \o) { $!store = o }
        Proxy.new: :&FETCH, :&STORE;
    }
}
my $holder = Holder.new;
my Hash $reached = $holder.store;
$reached<probe> = 'X';
ck $holder.store<probe>, 'X', 'a subscript reaches through a method-returned Proxy';

# ---- `temp` in a METHOD body is undone on the way out -------------------
class Deref {
    has Bool $.auto is rw = True;
    method quietly { temp $!auto = False; $!auto }
}
my $deref = Deref.new;
ck $deref.quietly, False, 'a method`s `temp` holds inside the body';
ck $deref.auto,    True,  '…and is restored when the method returns';

# ---- a runtime mixin composes the role's OWN role chain ------------------
role CosBase { has Int $.obj-num is rw;  method cos-ok { 'base' } }
role CosBool does CosBase { method content { 'bool:' ~ ?self } }
my Bool $flag = True;
$flag = $flag but CosBool;
ck $flag.does(CosBase),  True,  'a mixin answers the role its role composes';
ck $flag.cos-ok,         'base', '…and carries that role`s methods';
ck $flag.obj-num.raku,   'Int',  '…and its attributes` type objects';
my Int $n = 42;
$n = $n but CosBool;
ck $n.obj-num.raku,      'Int',  '…over a native Int too';

# ---- a SUBSET beats the package-relative short name ----------------------
my subset Boxed of Pair where { .key eq 'boxed' };
class Wrap::Boxed { }
class Wrap::Thing {
    method ast returns Boxed { :boxed[1, 2] }
}
ck Wrap::Thing.ast.key, 'boxed', 'a subset outranks a same-named nested class';

# ---- a custom `is rw` accessor that callsames writes through -------------
class TiedArray is Array {
    method AT-POS($p, :$check) is rw { my $val := callsame; $val }
}
my TiedArray $ta .= new;
$ta[0] = 'Lab';
ck $ta.elems,               1,     'an assignment through an overriding AT-POS lands';
ck $ta[0],                  'Lab', '…and reads back';
ck $ta.AT-POS(0, :check),   'Lab', 'a named argument is not an index';

class TiedHash is Hash {
    method AT-KEY($k, :$check) is rw { my $val := callsame; $val }
}
my TiedHash $th .= new;
$th<k> = 5;
ck $th<k>,                  5, 'the same for an overriding AT-KEY';
ck $th.AT-KEY('k', :check), 5, '…called with the adverb its callsame forwards';

# ---- seek/tell on a BINARY handle are byte offsets -----------------------
my $tmp = $*TMPDIR.add("rakupp-seek-probe-{$*PID}.bin");
$tmp.spurt: 'abcdefghij';
{
    my $fh = $tmp.open(:bin);
    $fh.seek(0, SeekFromEnd);
    ck $fh.tell, 10, '`tell` after a seek to the end is the file`s byte count';
    $fh.seek(3, SeekFromBeginning);
    ck $fh.tell, 3, '…and a seek to a byte offset lands on that byte';
    ck $fh.read(4).decode('latin-1'), 'defg', '…which is where the next read starts';
    $fh.seek(-2, SeekFromEnd);
    ck $fh.read(2).decode('latin-1'), 'ij', 'SeekFromEnd counts back from the end';
    $fh.close;
}
$tmp.unlink;

# ---- `|$obj` on a Hash-derived object slips its entries as nameds --------
class Entries is Hash { }
role EncryptEntries { }
class Taker { has $.V; has $.R; submethod TWEAK(|c) { } }
my $enc = Entries.new;
$enc<V> = 2; $enc<R> = 3;
$enc = $enc but EncryptEntries;
ck Taker.new(|$enc).V, 2, 'a Hash-derived object slips as named arguments';
ck Taker.new(|$enc).R, 3, '…every entry of it';

# ---- a type parameter may NAME a constant -------------------------------
sub wide-buf {
    constant WORD = uint32;
    Buf[WORD].allocate(4);
}
ck wide-buf().bytes,     16,       'Buf[CONSTANT] sizes by the type the constant names';
ck wide-buf().of.^name,  'uint32', '…and reports it as its element type';

# ---- `INIT` takes a whole STATEMENT as its operand -----------------------
sub init-given  { state $x = INIT given 21   { $_ * 2 }; $x }
sub init-with   { state $x = INIT with  5    { $_ * 3 }; $x }
sub init-if     { state $x = INIT if True    { 9 };      $x }
ck init-given, 42, '`INIT given` runs the statement and yields its value';
ck init-with,  15, '…and `INIT with`';
ck init-if,     9, '…and `INIT if`';

# ---- a native-typed ARRAY may be bound ----------------------------------
sub native-bytes { my uint8 @a = 1, 2, 3; @a }
my uint8 @bound := native-bytes();
ck @bound.elems,    3,       'a native-typed array is a container and binds';
ck @bound.of.^name, 'uint8', '…keeping its element type';

# ---- `::?CLASS` in a parameter is the enclosing CLASS --------------------
role CoerceFallback { multi method COERCE($v is raw) { 'fallback' } }
class Stamp does CoerceFallback {
    my constant StampRx = rx/^ 'D:' \d+ /;
    multi method COERCE(::?CLASS:D $_) { 'self' }
    multi method COERCE(Str:D $s where StampRx, |c) { 'parsed' }
}
ck Stamp.COERCE(Stamp.new), 'self',     '`::?CLASS:D` takes an instance of its own class';
ck Stamp.COERCE(42),        'fallback', '…and nothing else — the role`s catch-all gets it';

# ---- a `where` reads the scope its SIGNATURE was written in --------------
ck Stamp.COERCE('D:1998'), 'parsed',
   'a `where` naming a class-body constant decides the dispatch';

class Scoped {
    my $limit = 10;
    multi method pick(Int:D $n where * > $limit) { 'big' }
    multi method pick($n) { 'small' }
}
ck Scoped.pick(11), 'big',   '…and one naming a class-body variable';
ck Scoped.pick(3),  'small', '…and still loses when it does not hold';

# ---- a built-in-backed constructor keeps its SUBCLASS identity ----------
class Stamped is DateTime {
    multi method new(Str:D $y where /^ \d**4 $/) { nextwith("{$y}-01-01T00:00:00Z") }
}
ck Stamped.new('1998').^name, 'Stamped', '`nextwith` from a subclass`s .new answers that subclass';
ck Stamped.new('1998').year,  1998,      '…built from what the built-in made';
my Stamped $stamp .= new('1998');
$stamp .= new('1999');
ck $stamp.year, 1999, 'a constructor called on an INSTANCE redispatches on the type';

# ---- `sub prefix:</>` owns the slash ------------------------------------
# LAST in the file on purpose: from the declaration on, a bare `/` is that
# operator and no longer opens a regex — which is what Rakudo does too.
ck ("abcd" ~~ /bc/).Str, "bc", 'a regex literal before the declaration';
sub prefix:</>($name) { "N:$name" }
ck (/'Outlines'),        'N:Outlines', 'prefix-slash on a quoted name';
ck (/<Metadata>),        'N:Metadata', 'prefix-slash on a word list';
ck %( S => /('D'), St => 3 )<S>, 'N:D', 'prefix-slash inside a hash composer';
ck ("abcd" ~~ m/bc/).Str, "bc",  'the m// form still matches';
ck 10 / 4,               2.5,    'and division is still division';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
