# Regression: language behaviour from the sixth batch of Rakudo t/ gap files
# (2026-10-05).
#
# - A sequence seeded with enum values steps by `.succ`/`.pred`, both ways.
# - `[+]`, `[max]` and `[min]` over an Int range are computed from its ends
#   (`[+] 1..10**12` does not walk a trillion elements).
# - A lexical `infix:<..>` / `infix:<...>` is called for `..` / `...`, while
#   `..^` stays the built-in (the longest built-in spelling wins).
# - `.BIND-POS` reifies a lazy array up to the position and refuses a
#   negative one.
# - A tight adverb on a declared term is its named argument, so adverbed terms
#   nest: `FOO:to(FOO:of(5))`.
# - `@<p>:exists` / `%<h>:exists` drop the adverb and stay the list / hash.
#
# Every expectation below was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

enum E <a b c>;
ck((E::a ... E::c).map(*.key).join(','), 'a,b,c', 'an enum sequence upwards');
ck((E::c ... E::a).map(*.key).join(','), 'c,b,a', 'an enum sequence downwards');
ck((False ... True).List, (False,), 'a Bool sequence');

ck([+](1..10**12), 500000000000500000000000, '[+] over a huge Int range');
ck(([max] 1..^10), 9, '[max] over an exclusive range');
ck(([min] 3^..7), 4, '[min] over an exclusive range');

{
    sub infix:<..>($a, $b) { "range $a $b" }
    ck(1 .. 2, 'range 1 2', 'a lexical infix:<..>');
    ck((1 ..^ 3).raku, '1..^3', '..^ is still the built-in');
}
{
    sub infix:<...>($a, $b) { "seq $a $b" }
    ck((1 ... 2), 'seq 1 2', 'a lexical infix:<...>');
}

my @lazy = 1..*;
@lazy.BIND-POS(5, my $bound = 42);
ck(@lazy[0..6].List, (1, 2, 3, 4, 5, 42, 7), 'BIND-POS into a lazy array');
ck((try { my @b; @b.BIND-POS(-1, 1); 'lived' }) // $!.^name, 'X::OutOfRange', 'BIND-POS refuses a negative position');

proto term:<FOO>(*%) {*}
multi term:<FOO>(Int :$of!) { "of=$of" }
multi term:<FOO>(Str :$to!) { "to=$to" }
ck((FOO:of(5), FOO:to(FOO:of(5))), ('of=5', 'to=of=5'), 'adverbs on a declared term');

"ab" ~~ / $<p>=( (.) (.) ) /;
ck((@<p>:exists).elems, 2, '@<p>:exists stays the list');
"ab" ~~ / $<h>=( $<x>=(.) $<y>=(.) ) /;
ck((%<h>:exists).keys.sort.List, <x y>, '%<h>:exists stays the hash');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
