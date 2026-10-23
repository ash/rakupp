# BROKE: where the parser guesses from a name's spelling alone whether it is a
# type's, a name beyond ASCII lost (found 2026-10-23 by FatRoast's names-v1;
# each line is the reduction of a Roast file that Rakudo passed renamed and
# rakupp did not):
#
#   class Éx {}; say Éx.new ~~ Éx:D;   # Impossible coercion from 'Pair' — Éx(:D)
#   my class 万 {}                      # Undefined routine 'class' — 万 was 10000
#   sub е { }; say ?&е, 1              # the infix ?& with nothing on its left
#   my $o = new Еx:;                    # the indirect `new` refused
#   has $.b handles Éx                  # no method delegated
#
# FIXED: typeLikeName (src/Parser.cpp) reads a first letter beyond ASCII that
# is not lower-case as a type's, as an upper-case ASCII one is, for the smiley,
# a parameterization, `Foo .new` and the indirect `new`; a CJK ideograph that
# carries a numeric value is a letter, not a numeral (unicodeNumeralValue,
# src/Lexer.cpp); `?&` before a name in any script is the prefix `?` on `&name`;
# `handles` takes a class named in any script. Checked against Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

# type smileys
{ my class Éx { }
  ck(Éx.new ~~ Éx:D, True, 'Éx:D');
  ck(Éx ~~ Éx:U, True, 'Éx:U');
  ck(Éx.new ~~ Éx:U, False, 'an instance is no Éx:U') }
{ my class 不x { }; ck(不x.new ~~ 不x:D, True, 'CJK 不x:D') }
{ my class 갂x { }; ck(갂x ~~ 갂x:U, True, 'Hangul 갂x:U') }
{ sub ч($x) { $x * 2 }; ck((ч 3), 6, 'a lower-case Cyrillic sub still calls') }

# a CJK ideograph with a numeric value is a letter
{ my class 万 is Hash { }; ck(万.^name, '万', 'my class 万 is Hash') }
{ sub 万 { 5 }; ck(万(), 5, 'sub 万') }
{ my $万 = 3; ck($万, 3, '$万') }
ck(Ⅳ + 1, 5, 'a Roman numeral is still a number');
ck(〇, 0, '〇 is still a number');

# ?& before a name in any script
{ sub е(Str :@foo) { }; my $c = \(); ck(?&е.cando($c), True, '?&е.cando(…)') }

# a parameterized role named in Cyrillic
{ my role Ро[::T] { method t { T.^name } }; my class Кл does Ро[Int] { }
  ck(Кл.new.t, 'Int', 'Ро[Int]') }

# the indirect `new`
{ my class Еx { }; my $o = new Еx:; ck($o.^name, 'Еx', 'new Еx:') }

# handles a class named in any script
{ my class Бэк { method hi { 42 } }
  my class Фронт { has $.backend is rw handles Бэк }
  my $f = Фронт.new; $f.backend = Бэк.new;
  ck($f.hi, 42, 'handles Бэк delegates its methods') }

if @fail { note $_ for @fail; say 'FAIL' }
else { say 'PASS' }
