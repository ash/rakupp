# A native array's element is the native it holds (2026-11-06), as Rakudo's
# AT-POS on an `int` / `num` / `uint8` array answers one: `@a[$i]` and the
# topic `.map`, `.grep` and `.first` hand a block are natives, and `.grep`'s
# answer keeps them. A copy into a container boxes — an Array, a scalar, a
# hash, a `push`, the `[…]` composer — and so do `.pop` and `.shift`;
# arithmetic on an element is boxed too (an element is no native operand). A
# List literal keeps one.
#
# With those natives in place a native multi parameter takes a native of its
# family only, as Rakudo's multi dispatch does: a lone `multi z(int $x)` is
# no match for a boxed 3 (a plain sub's binder still coerces it). Every
# answer here is Rakudo 2026.09's.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
sub t(&c) { my $r = try c(); $r // "ERR:" ~ ($!.^name.subst(/^X\:\:/, "")) }

multi a(int $x) { "int" }; multi a(uint $x) { "uint" }; multi a(num $x) { "num" }
multi a(Int $x) { "Int" }; multi a(Num $x) { "Num" }; multi a($x) { "any" }

my int @a = 1, 2, 3; my num @n = 1e0, 2e0; my uint8 @u = 1, 2; my int8 @b = 1, 2; my @plain = 1, 2;
check((a(@a[0]), a(@a[*-1]), a(@n[0]), a(@u[0]), a(@b[0]), a(@plain[0])).join(" "),
      "int int num uint int Int", 'an element read');
my int @e;
check((a(@e[5]), a(@a[10])).join(" "), "int int", "…past the end, its default");
check((a(@a[0] + 1), a(-@a[0]), a(+@a[0]), a(@n[0] * 2e0)).join(" "), "Int Int Int Num",
      'arithmetic on an element is boxed');
check(@a.map({ a($_) }).join(","), "int,int,int", '.map topic');
check(@n.map({ a($_) }).join(","), "num,num", '…over a num array');
check(@u.map({ a($_) }).join(","), "uint,uint", '…over a uint8 array');
check(@a.map(-> $x { a($x) }).join(","), "Int,Int,Int", '…but a declared parameter boxes it');
check(@a.map(&a).join(","), "int,int,int", '.map of a routine');
check(@a.grep({ a($_) eq 'int' }).elems, 3, '.grep topic');
check(@a.grep(* > 1).map({ a($_) }).join(","), "int,int", ".grep's answer keeps its natives");
check(a(@a.first({ $_ > 1 })), "int", '.first');
check((do for @a { a($_) }).join(","), "int,int,int", 'a for topic, as before');

{
    my int @c = 1, 2;
    my @copy = @c; my $s = @c[0]; my %h; %h<k> = @c[0]; my @p; @p.push(@c[0]);
    check((a(@copy[0]), a($s), a(%h<k>), a(@p[0]), a([@c[0]][0]), a((@c[0],)[0])).join(" "),
          "Int Int Int Int Int int", 'a copy boxes; a List literal keeps it');
    check((a(@c.pop), a(@c.shift)).join(" "), "Int Int", '.pop and .shift box');
}
{
    my int @w = 1, 2; @w.map({ $_ }); @w.grep({ True });
    my @after = @w;
    check(a(@after[0]), "Int", 'a .map or .grep leaves no tags in the array');
}

multi z(int $x) { "z-int" }
multi w(num $x) { "w-num" }
class C { multi method m(int $x) { "int" }; multi method m(Str $x) { "Str" } }
sub only(int $x) { "only-int" }
my int $n = 3; my $v = 3;
check((t({z(3)}), t({z($v)}), t({z(@a.elems)})).join(" "),
      "ERR:Multi::NoMatch ERR:Multi::NoMatch ERR:Multi::NoMatch", 'a lone native candidate refuses a boxed value');
check((t({z($n)}), t({z($n + 1)}), t({z(@a[0])}), t({@a.map({ z($_) }).head})).join(" "),
      "z-int z-int z-int z-int", '…and takes a native');
check((t({w(1.5e0)}), t({w(@n[0])})).join(" "), "ERR:Multi::NoMatch w-num", '…for num too');
check((t({C.m($n)}), t({C.m(3)})).join(" "), "int ERR:Multi::NoMatch", '…for a multi method');
check((t({only(3)}), t({only($n)})).join(" "), "only-int only-int", 'a plain sub still coerces');

if @fail { note $_ for @fail; say "FAIL" } else { say "PASS" }
exit @fail ?? 1 !! 0;
