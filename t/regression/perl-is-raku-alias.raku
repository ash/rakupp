# Regression: `.perl` is `.raku` — the old name for the same method — and it is
# aliased ONCE, at the top of the method dispatcher.
#
# It used to be sixteen `|| m == "perl"` clauses scattered down the ladder, which
# meant every new `.raku` arm had to remember to carry one. The alias happens at
# MName construction; note it must bind a STATIC string, because MName holds a
# reference and a ternary temporary would dangle.
#
# The case that matters beyond the mechanical one: a user class defining
# `method raku` must answer `.perl` too, since the alias resolves before the
# user-method lookup.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# .perl and .raku agree across the type surface. Compared to each OTHER, not to
# pinned text: binding through a `for` loop itemizes, so `[1,2]` reprs as `$[1,2]`
# there, and that is a different question from the alias.
check(42.perl,        42.raku,        'an Int');
check('ab'.perl,      'ab'.raku,      'a Str');
check([1, 2].perl,    [1, 2].raku,    'an Array');
check({a => 1}.perl,  {a => 1}.raku,  'a Hash');
check((1..3).perl,    (1..3).raku,    'a Range');
check(Int.perl,       Int.raku,       'a type object');
check((1/3).perl,     (1/3).raku,     'a Rat');
check((1+2i).perl,    (1+2i).raku,    'a Complex');
check(:a(1).perl,     :a(1).raku,     'a Pair');
# and a couple of exact forms, to catch the alias silently returning something else
check(42.perl,        '42',           'the Int form');
check('ab'.perl,      '"ab"',         'the Str form');
check((1/3).perl,     '<1/3>',        'the Rat form');

# containers and quanthashes
check((1, 2).Seq.perl, (1, 2).Seq.raku, 'a Seq');
check(set(1, 2).perl,  set(1, 2).raku,  'a Set');
check(DateTime.new(:2016year).perl, DateTime.new(:2016year).raku, 'a DateTime');
check(IO::Special.new("<STDIN>").perl, IO::Special.new("<STDIN>").raku, 'an IO::Special');

# a user class: the default renderer, and a user override
class RgP { has $.x = 3 }
check(RgP.new.perl, RgP.new.raku, 'a plain user object');
class RgO { method raku { 'CUSTOM' } }
check(RgO.new.raku, 'CUSTOM', 'a user method raku');
check(RgO.new.perl, 'CUSTOM', 'and .perl resolves to it');

# .raku is untouched by the alias in the other direction
class RgQ { method perl { 'ONLY-PERL' } }
check(RgQ.new.perl, 'ONLY-PERL', 'a user method perl still answers');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
