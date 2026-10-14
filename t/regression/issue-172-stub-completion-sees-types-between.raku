# Issue #172: `my class A {...}` builds its completion further down right
# away, so A is usable in between (roast S12-class/stubs.t). The completion's
# body then ran before the types declared BETWEEN the stub and it, so a
# nested class composing or inheriting one died: `my class A {...}; my role
# RH {}; my class A { my class Inner does RH {} }` said "Invalid typename
# 'RH'" (PDF::COS::Tie, `my class CosOfAttr is Attribute does COSAttrHOW`).
# Those types are compile-time in Rakudo too, and are now built first, in
# order; one declared after the completion is still unknown to it.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}
sub run-code($code, *@args) {
    my $p = run($*EXECUTABLE, |@args, '-e', $code, :out, :err);
    my $out = $p.out.slurp(:close).trim;
    my $err = $p.err.slurp(:close);
    $err ?? "$out [stderr: {$err.lines.head}]" !! $out
}

# the issue's module, and the PDF::COS::Tie shape
my $dir = $*TMPDIR.add("issue172-{$*PID}");
$dir.mkdir;
$dir.add('I172.rakumod').spurt: q:to/END/;
    unit module I172;
    my class A {...}
    my role RH { method r { "rh" } }
    my class A { my class Inner does RH {}; method m { Inner.r } }
    my class T {...}
    my role COSAttrHOW { }
    my class T { my class CosOfAttr is Attribute does COSAttrHOW {}; method k { CosOfAttr.^name } }
    sub probe is export { A.m ~ " " ~ T.k }
    END
for <cold cached> -> $run {
    check run-code('use I172; say probe()', '-I', ~$dir), 'rh I172::T::CosOfAttr', "module, $run run";
}

for Q[my class A {...}; my role RH { method r {7} }; my class A { my class Inner does RH {}; method m { Inner.r } }; say A.m]
        => '7',
    Q[class A {...}; role RH { }; class A { class Inner does RH {} }; say A::Inner ~~ RH]
        => 'True',
    Q[my class A {...}; my class P { method p {3} }; my class A { my class Inner is P {}; method m { Inner.p } }; say A.m]
        => '3',
    # the completion is still usable between the stub and it
    Q[my class A {...}; say A.m; my role RH { method r {7} }; my class A does RH { method m { self.r } }]
        => '7',
    Q[class X::B { ... }; say X::B.new.a; class X::B { has $.a = 42 }]
        => '42'
    -> (:key($code), :value($want)) {
    check run-code($code), $want, $code;
}
# …and a role declared AFTER the completion is no more known to it than in Rakudo
check run-code(Q[my class A {...}; my class A { my class Inner does RH {} }; my role RH {}; say "ok"]).contains('ok'),
      False, 'a role after the completion is still an invalid typename';

for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
