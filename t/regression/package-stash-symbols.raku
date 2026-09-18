# Regression: a package's stash and its qualified globals are ONE symbol table.
# `our sub foo` inside `class A` publishes the global `&A::foo` — sigil first —
# but `A::<&foo>` parsed to a slot named `A::&foo`, and `A.WHO` looked for
# globals under a plain `A::` prefix. So the two spellings of one symbol got a
# slot each: `A.WHO` came back EMPTY for every class that declares `our` names,
# `A::<&foo>` answered Any (S02-packages/package-lookup.t), and `A::<$bar> = 99`
# wrote a slot the class's own `$bar` never read.
#
# The fix spells a sigilled `Pkg::<$name>` the way `our` publishes it, and
# splits a qualified name into package and stash key with the sigil travelling
# on the KEY — which is where Rakudo's stash carries it (`A.WHO<&foo>`).
#
# Every expectation below is the Rakudo 2026.08 answer.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub is-it($got, $want, $desc) { @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

class A {
    my $x = 10;                        # a LEXICAL of the same name, independent
    our $bar = 42;
    our sub foo() { 'I am foo' }
    class B { }
    method lexical() { $x }
    method pkg() { $bar }
}

# the stash holds the `our` names under Rakudo's sigilled keys, plus the
# package nested inside this one
is-it A.WHO.keys.sort.List, ('$bar', '&foo', 'B'), 'stash keys';
is-it A::.keys.sort.List,   ('$bar', '&foo', 'B'), 'Typename:: is that same stash';
is-it (A:: === A.WHO),      True,                  'A:: and A.WHO are one stash';
is-it A.WHO<B>.^name,       'A::B',                'a nested package is in the stash';

# reads: both spellings of one slot
is-it A::<$bar>,      42,          'Pkg::<$var> reads the our-variable';
is-it $A::bar,        42,          '…and so does its long name';
is-it A::<&foo>(),    'I am foo',  'Pkg::<&sub> reads the our-sub';
is-it A.WHO<&foo>(),  'I am foo',  '…and so does the stash key';
is-it A::foo(),       'I am foo',  '…and so does the qualified call';

# writes through the stash spelling reach the class's OWN variable — this is the
# half that was two containers: `A::<$bar> = 99` left A.pkg answering 42
A::<$bar> = 99;
is-it A::<$bar>, 99, 'Pkg::<$var> write is visible to itself';
is-it $A::bar,   99, '…to the long name';
is-it A.pkg,     99, '…and to the class, whose $bar it is';

# …and the long name reads a symbol installed through the stash
A.WHO<$zz> = 7;
is-it $A::zz,    7, 'a stash-installed symbol reads by its long name';
is-it A::<$zz>,  7, '…and by the angle spelling';

# the lexical of the same name stays out of all of it
is-it A.lexical,          10,   'a lexical of the same name is independent';
is-it A.WHO<$x>.defined,  False, '…and is not in the stash';

# a sigil-less stash slot is untouched by any of this (EXPORTHOW::<class> = …)
A::<plain> = 'p';
is-it A::<plain>, 'p', 'a sigil-less package slot still works';

die @fail.join('; ') if @fail;
say "PASS";
