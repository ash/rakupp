# Regression: language behaviour from the third batch of Rakudo t/ gap files
# (2026-10-05).
#
# - `GLOBAL::<$x>:exists`, `GLOBAL::<Sym> = …`, `$p::` (the package a value
#   names), `Pkg::{'&f'}`, and `our &f is export = …` in EXPORT::DEFAULT.
# - `our &infix:<qq> = &c` declares the operator; after a term, a declared
#   infix word is the operator, not a quote.
# - `.minpairs`/`.maxpairs` take a key or a comparator; `.minmax(&key)` reads
#   Ranges by their ends, keeping exclusions.
# - A type object's own ACCEPTS runs for `~~`; a refused element assignment does
#   not grow the array.
# - `%_` in a grammar rule holds the unclaimed named arguments; a sub with a
#   signature refuses placeholders, a regex code block refuses `%_`/`@_`.
# - An attribute's `is Array` names its type; `qqw{…}` nests inner braces;
#   `tr/a-c/` is refused; `-> *@x` takes one value per iteration; `.raku`
#   escapes a grapheme led by a Prepend or Extend codepoint.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
sub dies-with($code, $type) { (try { EVAL $code; 'lived' }) // $!.^name eq $type }

our $g = 1;
ck((GLOBAL::<$g>:exists, GLOBAL::<$nope>:exists), (True, False), 'GLOBAL::<$x>:exists');
GLOBAL::<GapsProbe> = 43;
ck(GLOBAL::<GapsProbe>, 43, 'a GLOBAL symbol springs into being');
my $p = Int;
ck($p::.WHO.^name, 'Stash', '$p:: is the package the value names');

module GapsM {
    sub c(@a, @b) { |@a, |@b }
    our &infix:<qq> is export = &c;
}
ck(GapsM::EXPORT::DEFAULT::{'&infix:<qq>'}.^name, 'Sub', 'an exported operator variable is in EXPORT::DEFAULT');
{
    sub cat(@a, @b) { |@a, |@b }
    our &infix:<qq> = &cat;
    ck((1, 2) qq (3, 4), (1, 2, 3, 4), 'an operator declared through a variable');
}
ck(qq (3, 4), '3, 4', '... while qq in term position stays a quote');

ck((^4).Array.minpairs(*.chars), (0 => 0, 1 => 1, 2 => 2, 3 => 3), '.minpairs with a key keeps the ties');
my $calls = 0;
ck((3, 1, 4, 1).minpairs(-> $a, $b { $calls++; $a <=> $b }), (1 => 1, 3 => 1), '.minpairs with a comparator');
ck($calls, 3, '... called once per value after the first');
ck((1^..5, 7).minmax(*.self), 1^..7, '.minmax with a key keeps Range exclusions');

my $ran = 0;
my class Picky { method ACCEPTS($) { ++$ran; True } }
ck((42 ~~ Picky, $ran), (True, 1), "a type object's own ACCEPTS");
my Int @typed = 1, 2;
try { @typed[5] = 'no' }
ck(@typed.elems, 2, 'a refused element assignment does not grow the array');

my $seen;
grammar Slurpy {
    token TOP  { <part(:adverb)> }
    token part { <?{ $seen = %_; 1 }> \w+ }
}
Slurpy.parse('abc');
ck($seen, {:adverb}.Hash, '%_ in a rule holds the named arguments');
ck(dies-with('sub taking(:$d) { %_ }', 'X::Signature::Placeholder'), True, 'a signature refuses %_');
ck(dies-with('sub outside { "a" ~~ / <?{ %_ }> \w / }', 'X::Placeholder::Block'), True,
   'a regex code block refuses %_');

my class Based { has Int @.a is Array }
ck(Based.^attributes[0].type.^name, 'Array[Int]', 'is Array names the attribute type');
ck(qqw{a {1+1} c}, ('a', '{1+1}', 'c'), 'brace-delimited qqw nests inner braces');
ck(dies-with('my $s = "a"; $s ~~ tr/a-c/x-z/', 'X::Obsolete'), True, 'tr refuses a hyphen range');

my @one;
for (1, 2, 3) -> *@x { @one.push(@x) }
ck(@one, [[1], [2], [3]], 'a slurpy for body takes one value a time');
ck(("\x[600]a".raku, "\x[200D]".raku, "a\x[300]".raku), ('"\x[600,61]"', '"\x[200D]"', "\"a\x[300]\""),
   '.raku escapes a grapheme led by a Prepend or Extend codepoint');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
