# Regression: value identity has ONE home — `whichOf`.
#
#   * an OBJECT is identified by its address. That lived only in the `.WHICH`
#     method arm, so `.WHICH` correctly said two instances differed while
#     baggyKeyStr — which keys a quanthash on the RENDERING, `A<obj>` for every
#     instance of A — merged them. `set($x, $y).elems` was 1.
#   * a RANGE is identified by its endpoint FORM, exclusion markers included.
#     Both `===` and `eqv` fell through to a tail that expanded it, so
#     `1..^5 === 1..4` was True — and answering it for a large range built the
#     whole list first.
#   * a parameterised TYPE keeps its parameter: `Array[Int] eqv Array[Str]` was
#     True because the tail compared the bare name.
#   * a CODE object is identical only to itself.
#
# Deliberately NOT changed: the ladders' catch-all tail still compares `toStr()`
# for plain values. (The audit's plan claimed switching it would flip
# `1 === 1.0` from True to False — but that is already False here, and in Rakudo,
# because the Rat arm above the tail catches it. The tail is still worth its own
# batch, just not for that reason.)
# Also unchanged: `%h{$obj}` still keys on the rendering, because Hash keys are
# plain strings. This fixes Sets and Bags, not Hash keys.
#
# Fixing the Range case exposed a bug it had been masking: `Rat.Range` built a
# SATURATED int range and rendered -9223372036854775808..9223372036854775807,
# where the endpoints are ±Inf. It only compared equal to `-Inf..Inf` because the
# old comparison expanded both sides.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# objects are distinct elements
class RgA { has $.n }
my $x = RgA.new(n => 1);
my $y = RgA.new(n => 2);
my $same = $x;
check(($x.WHICH ne $y.WHICH).gist, 'True',  'two instances have different WHICHes');
check(set($x, $y).elems,           '2',     'and are two Set elements');
check(set($x, $same).elems,        '1',     'while the same object is one');
check(bag($x, $x, $y).total,       '3',     'a Bag counts them');
check(($x === $same).gist,         'True',  'identity holds for the same object');
check(($x === $y).gist,            'False', 'and not for a different one');

# ranges compare by form, not by contents
check((1..^5 === 1..4).gist,  'False', 'exclusion markers are part of the identity');
check((1..^5 eqv 1..4).gist,  'False', 'for eqv too');
check((1..5 === 1..5).gist,   'True',  'an identical range is identical');
check((1..5 eqv 1..5).gist,   'True',  'and eqv');
check(('a'..'c' eqv 'a'..'c').gist, 'True', 'a Str range as well');

# parameterised types, and code
check((Array[Int] eqv Array[Str]).gist, 'False', 'the parameter is part of the type');
check((Array[Int] eqv Array[Int]).gist, 'True',  'the same parameter matches');
check((Int eqv Int).gist,               'True',  'an unparameterised type');
sub rgf() { 1 }
sub rgg() { 2 }
check((&rgf eqv &rgg).gist, 'False', 'two subs are not eqv');
check((&rgf eqv &rgf).gist, 'True',  'a sub is eqv to itself');

# the numeric type ranges carry their infinite endpoints
check(Rat.Range.gist,    '-Inf..Inf',   'Rat.Range');
check(Num.Range.gist,    '-Inf..Inf',   'Num.Range');
check(FatRat.Range.gist, '-Inf..Inf',   'FatRat.Range');
check(Int.Range.gist,    '-Inf^..^Inf', 'Int.Range is exclusive at both ends');
check(UInt.Range.gist,   '0..^Inf',     'UInt.Range starts at zero');
check((Rat.Range eqv (-Inf..Inf)).gist, 'True', 'and compares equal to the literal');

# the catch-all tail is untouched — plain values still compare as they did
check((1 === 1).gist,        'True',  'two equal Ints are identical');
check((1 === 2).gist,        'False', 'unequal ones are not');
check(('a' === 'a').gist,    'True',  'and two equal Strs');
check((1 === 1.0).gist,      'False', 'while Int and Rat differ — as they already did');
check((1 eqv 1).gist,        'True',  'eqv agrees');
check((1 eqv 1.0).gist,      'False', 'and is type-aware');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
