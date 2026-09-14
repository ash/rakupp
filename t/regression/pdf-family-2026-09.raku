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
# Runs clean under Rakudo too.

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

exit $fails ?? 1 !! 0;
