# Regression: a user class deriving IO::Path is an IO::Path.
#
# `class IO::Path::AutoDecompress is IO::Path { … }` — its instances have to
# answer `.e`, `.slurp`, `.lines`, `.Str` as the built-in does, and a mixin over
# one (`self but Proccer`) keeps that. And a QUALIFIED call past an override —
# `self.IO::Path::slurp(|c)` from a role method mixed in over the object — has
# to reach the built-in, not "No such method 'slurp'". Instances were bare
# objects with nothing behind them.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $me = $?FILE;
my class P is IO::Path { }
my $p = P.new($me);
ck($p.^name, 'P', 'the object keeps its own type');
ck($p.e, True, '.e reaches IO::Path');
ck($p.slurp.chars > 100, True, '.slurp reaches IO::Path');
ck($p.Str, $me, '.Str is the path');
ck($p ~~ IO::Path, True, 'and it is an IO::Path');

my role Proccer {
    method out() { self }
    method slurp(|c) { self.IO::Path::slurp(|c) }
}
my class IOAD is IO::Path {
    method !proc() { self but Proccer }
    method slurp(IOAD:D: :$enc = 'utf8') { self!proc.out.slurp(:$enc) }
}
ck(IOAD.new($me).slurp.chars, $me.IO.slurp.chars, 'the override goes through the mixin to the built-in');
ck(($me.IO but Proccer).out.slurp.chars, $me.IO.slurp.chars, 'the mixin over a plain IO::Path too');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
