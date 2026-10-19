# Regression: `--exe` native code died on every `⚛` operation. The parser
# lowers `$n⚛++` to a call named `atomic-fetch-inc` (and `cas` is one by
# name), which the interpreter takes as a special form on the CONTAINER;
# native codegen emitted an ordinary routine call, and the runtime has no
# routine by that name: "Undefined routine 'atomic-fetch-inc'". In a DESTROY
# the error was swallowed, so `$freed⚛++` there counted nothing. Both now go
# through one helper (Interpreter::atomicOn). This program checks itself; the
# `--exe` section of t/run.raku builds it and holds the binary to the
# interpreter's output.
#
# Contract: exit 0 + last line PASS. Runs under Rakudo unchanged.
my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my atomicint $n = 0;
$n⚛++ for ^10;
ck($n, 10, '$n⚛++ in a loop');
sub bump() { $n⚛++; atomic-fetch-add($n, 5) }
ck(bump(), 11, 'atomic-fetch-add in a sub answers the old value');
ck($n, 16, 'and adds');
ck(cas($n, 16, 100), 16, 'cas answers what it saw');
ck($n, 100, 'and swaps when it matches');
ck(cas($n, 5, 1), 100, 'a cas that does not match');
ck($n, 100, 'leaves the value');
ck(cas($n, { $_ + 1 }), 101, 'the code form answers the new value');
ck(++⚛$n, 102, '++⚛');
ck($n⚛--, 102, '⚛--');
ck(⚛$n, 101, 'an atomic fetch');
$n ⚛= 7;
ck($n, 7, '⚛=');
ck(($n ⚛+= 3), 10, '⚛+= answers the new value');
class C { method go { $n⚛++; $n } }
ck(C.new.go, 11, 'in a method');
my atomicint $freed = 0;
class D { submethod DESTROY { $freed⚛++ } }
D.new for ^1000;
$*VM.request-garbage-collection;
ck($freed > 0, True, 'in a DESTROY');
my @w = (^4).map: { start { for ^1000 { $n⚛++ } } };
await @w;
ck($n, 4011, 'from four threads at once');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
