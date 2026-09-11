# Regression: a file-scope package stub completed inside a nested block.
#
# `our class Rak { ... }` at file scope, and `class Rak { … }` (the real body)
# inside the `rak` sub further down: a package declaration is `our`-scoped
# wherever it is written, so the nested body completes the stub. rakupp checked
# stubs per statement list and only saw the file scope's own statements:
# "The following packages were stubbed but not defined: Rak". The mirror case
# (stub in a method, body at file scope) already worked.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

our class Rak { ... }
sub mk() {
    class Rak { has $.x = 7 }
    Rak.new
}
ck(mk().x, 7, 'the body inside the sub builds the class');
ck(Rak.new.x, 7, 'and the file-scope name is the same class');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
