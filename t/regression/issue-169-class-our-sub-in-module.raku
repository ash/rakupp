# Issue #169: an `our sub` in a class body was hoisted to the enclosing
# module's scope, so two classes of one module that each declared
# `our sub f` died loading with "Redeclaration of routine 'f'" (HarfBuzz:
# six classes each declare `our sub create`). A class body's `our` declares
# into the class: `M::A::f` and `M::B::f` are two subs and `M::f` is none.
# Fixed by 8902cac9 (v5.4.0); this holds the module-loading shape the issue
# reported, cold and from the precomp cache.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

my $dir = $*TMPDIR.add("issue169-{$*PID}");
$dir.mkdir;
$dir.add('I169.rakumod').spurt: q:to/END/;
    unit module I169;
    class A { our sub f(--> Str) { "A" }; method g { f() } }
    class B { our sub f(--> Str) { "B" }; method g { f() } }
    class C is export { our sub create(--> Str) { "C" } }
    class D is export { our sub create(--> Str) { "D" } }
    END

my $code = q:to/END/;
    use I169;
    print I169::A::f(), I169::B::f(), I169::A.g, I169::B.g, ' ';
    print C::create(), D::create(), ' ';
    say I169::.keys.grep(*.starts-with('&')).elems;
    END
for <cold cached> -> $run {
    my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close);
    check $out, 'ABAB CD 0', "$run run: each class keeps its own `our sub`{$err ?? " ($err.lines.head())" !! ''}";
}

for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
