# OBJECT-SLOTS-PLAN batch 1 (2026-11-06): a class's AttrLayout — the keys an
# instance stores its declared attributes under, in the order runAttrDefaults
# stores them — agrees with what runAttrDefaults actually stores, for the
# shapes whose keys are not simply the bare names in declaration order:
# shadowing down a chain, sigil twins, a private attribute over a parent's
# public one, roles, role parameters, a second class parent, `does R is A`,
# defaults that read and write other attributes, `is built`, BUILD and TWEAK,
# typed and trait containers, mixins. The programs run in child processes under
# RAKUPP_ATTR_LAYOUT_CHECK, and the check's log is read back.
#
# Two differences are known and pinned here, because batch 3 changes what
# they look like: an object whose table already holds the engine's hidden
# `__type` when its attributes are seeded (a custom HOW: `nonempty,beyond`),
# and a runtime `set_build` closure that stores a LATER attribute before its
# own — nothing seeds an attribute with no default first, so today's order is
# (b, a) where the layout's is (a, b). Rakudo's `.raku` lists attributes in
# declaration order whatever the store order was.
#
# rakupp only: the check is the engine's own.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

my $prog = Q:to/END/;
class A  { has $.x = 1; has @!x = 7, 8; has $!p = 2; has %.h; method twin { @!x } }
class B is A { has $.x = 10; has $.y = 20 }
class C is B { has $.z; has $!p = 3 }
role R  { has $.r = 5 }
role P[$n] { has $.pn = $n }
class D does R { has $.d }
class E is C does R { has $.e = self.z // 0 }
class F  { has $.a; has $.b = $!a * 2; has $.c = do { $!d = 4; 1 }; has $.d }
class G  { has $.g; submethod BUILD(:$g) { $!g = $g * 2 } }
class G2 { has $.t; submethod TWEAK { $!t = 1 } }
class K  { has $!k is built; has Int @.ints; has %.set is Set; has @.def is default(0) }
class M1 { has $.m1 = 1 }
class M2 { has $.m2 = 2 }
class MM is M1 is M2 { has $.mm = 3 }
class Q does P[9] { has $.q }
class V does R is A { has $.v }
class H  { has $.a; has $.b; method setb { $!b = 9; 1 } }
H.^attributes.first(*.name eq '$!a').set_build(-> $self, | { $self.setb });
my @o = A.new, B.new(y => 2), C.new(z => 3), D.new(d => 4), E.new(e => 1),
        F.new(a => 3), G.new(g => 5), G2.new, K.new(k => 1), MM.new(m1 => 5, m2 => 6),
        Q.new, V.new(v => 1), H.new;
my $m = C.new but R;
say @o.elems, " ", $m.r, " ", C.new.twin.elems, " ", MM.new.m2;
END
# a module exporting a metaclass, as Roast's Advent::GrammarProfiler does: each
# `class` the program declares after `use` of it is an instance of the HOW
my $how = Q:to/END/;
my class LayoutHOW is Metamodel::ClassHOW { has $.tag = 1 }
my module EXPORTHOW { }
EXPORTHOW.WHO.<class> = LayoutHOW;
END
my $howUser = Q:to/END/;
use LayoutHOW;
class W { has $.w = 1 }
say W.new.w, " ", W.HOW.^name;
END

my $dir = $*TMPDIR.add("attr-layout-{$*PID}");
mkdir $dir;
my $log  = $dir.add("check.log");
$dir.add("prog.raku").spurt($prog);
$dir.add("LayoutHOW.rakumod").spurt($how);
$dir.add("how-user.raku").spurt($howUser);
sub run-checked($name, @libs = ()) {
    my $p = run($*EXECUTABLE, |@libs.map({ "-I$_" }), ~$dir.add($name), :out, :err,
                :env(%(|%*ENV, RAKUPP_ATTR_LAYOUT_CHECK => ~$log)));
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    check($p.exitcode, 0, "$name runs ({$err.lines.head // ''})");
    $out.trim
}
check(run-checked("prog.raku"), "13 5 2 2", 'its answers');
check(run-checked("how-user.raku", [~$dir]), "1 LayoutHOW", 'a class declared under an exported HOW');

my @lines = $log.e ?? $log.lines.map(*.split("\t")) !! ();
my @summary = @lines.grep(*.[1] eq 'summary');
check(@summary.elems, 2, 'one summary for each process');
my $checked = [+] @summary.map({ .[2].split('=')[1].Int });
check($checked > 15, True, "every construction was checked ($checked)");
my @diffs = @lines.grep(*.[1] ne 'summary').map({ .[1..2].join(" ") }).sort;
check(@diffs.List, ("nonempty,beyond LayoutHOW", "order H"),
      'the two known differences, and no other');

for $dir.dir { .d ?? (.unlink for .dir; .rmdir) !! .unlink }   # (a precomp directory, if made)
$dir.rmdir;

if @fail { note $_ for @fail; say "FAIL" } else { say "PASS" }
exit @fail ?? 1 !! 0;
