# Natives in multi dispatch (2026-11-06), each answer Rakudo 2026.09's:
#
#   * a native parameter takes a native of its own FAMILY — signed (`int`,
#     `int8`…), unsigned (`uint`, `uint8`…) or `num` — and never one of another:
#     a `my uint` is no `int`, a `my int` no `uint`;
#   * a native argument takes its family's native candidate over every boxed
#     one, whatever `where`, smiley or narrower type the boxed one has;
#   * a BOXED value takes any boxed candidate over a native one, even one
#     whose `where` it passes (rakupp still lets a lone native candidate take
#     one, where Rakudo refuses it: not asserted here);
#   * two natives of one family are equally narrow: `int8` beside `int` makes
#     a native int's call ambiguous.
#
# rakupp scored a native parameter as a nominal type a little narrower than a
# boxed one, so a `where` on the boxed candidate outweighed it, a native
# outranked an untyped `$x` for a boxed 3, and `uint` took a `my int`.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
sub t(&c) { my $r = try c(); $r // "ERR:" ~ ($!.^name.subst(/^X\:\:/, "")) }

multi a(int $x) { "int" }; multi a(Int $x) { "Int" }
multi b(uint $x) { "uint" }; multi b(int $x) { "int" }; multi b(Int $x) { "Int" }
multi e(int $x) { "int" }; multi e(Int $x where * > 0) { "Int+" }; multi e(Int $x) { "Int" }
multi g(int $x where * > 0) { "int+" }; multi g(Int $x) { "Int" }
multi h(int8 $x) { "int8" }; multi h(int $x) { "int" }; multi h(Int $x) { "Int" }
multi m(uint $x) { "uint" }; multi m(Int $x) { "Int" }
multi f1(int $x) { "int" }; multi f1($x) { "any" }
multi f2($x) { "any" }; multi f2(int $x where * > 0) { "int+" }
multi f3(int $x) { "int" }; multi f3(Numeric $x) { "Numeric" }
multi f4(int $x) { "int" }; multi f4(Int:D $x where * > 0) { "Int:D+" }
multi f7(uint $x) { "uint" }; multi f7($x) { "any" }
multi q(num $x) { "num" }; multi q(Num $x where * > 0) { "Num+" }; multi q(Num $x) { "Num" }
multi q2(num $x) { "num" }; multi q2($x) { "any" }
class C { multi method m(int $x) { "int" }; multi method m(Int $x where * > 0) { "Int+" } }

my int $n = 3; my int $z = 0; my uint $u = 3; my int8 $b = 3; my uint8 $ub = 3; my num $d = 1.5e0;
my $p = 3;
check((t({a($n)}), t({b($n)}), t({e($n)}), t({g($n)}), t({m($n)})).join(" "),
      "int int int int+ Int", 'a native int');
check((t({e($z)}), t({g($z)})).join(" "), "int Int", "…whose native candidate's `where` fails");
check((t({a($u)}), t({b($u)}), t({e($u)}), t({g($u)}), t({m($u)}), t({f7($u)})).join(" "),
      "Int uint Int+ Int uint uint", 'a native uint');
check((t({a($b)}), t({b($b)}), t({a($ub)}), t({b($ub)})).join(" "), "int int Int uint", 'int8 and uint8');
check((t({a($p)}), t({b($p)}), t({e($p)}), t({g($p)}), t({m($p)}), t({a(3)}), t({g(3)})).join(" "),
      "Int Int Int+ Int Int Int Int", 'a boxed Int');
check((t({f1(3)}), t({f2(3)}), t({f3(3)}), t({f4(3)}), t({f7(3)})).join(" "),
      "any any Numeric Int:D+ any", '…which a native candidate takes last');
check((t({f1($n)}), t({f2($n)}), t({f3($n)}), t({f4($n)})).join(" "),
      "int int+ int int", '…and a native int first');
check((t({a($n % 3)}), t({e($n + 1)}), t({m(-$n)})).join(" "), "int int Int", 'a native operation');
check((t({q($d)}), t({q(1.5e0)}), t({q(-1.5e0)}), t({q2($d)}), t({q2(1.5e0)})).join(" "),
      "num Num+ Num num any", 'a native num');
check((t({h($n)}), t({h($b)}), t({h(3)})).join(" "), "ERR:Multi::Ambiguous ERR:Multi::Ambiguous Int",
      'int8 beside int');
check((t({C.m($n)}), t({C.m(3)})).join(" "), "int Int+", 'a multi method');

if @fail { note $_ for @fail; say "FAIL" } else { say "PASS" }
exit @fail ?? 1 !! 0;
