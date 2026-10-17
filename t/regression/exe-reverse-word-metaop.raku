# Regression: the reverse metaoperator over a WORD infix in a binary compiled
# with --exe. `'b' Rcmp 'a'` died "Unsupported operator 'Rcmp'": codegen lowered
# `R` only before a symbol (`R-`, `R/`), and the run-time dispatch does not take
# the R spelled into the name — the interpreter strips it in applyBinOp. Every
# R reverses again, so `RRcmp` is `cmp`.
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

ck ('b' Rcmp 'a'), Less, 'Rcmp';
ck ('a' Rleg 'b'), More, 'Rleg';
ck ('a' Rcoll 'b'), More, 'Rcoll';
ck ('a' Runicmp 'b'), More, 'Runicmp';
ck (2 Rdiv 7), 3, 'Rdiv';
ck (3 Rmod 10), 1, 'Rmod';
ck ('b' Rlt 'a'), True, 'Rlt';
ck ('b' RRcmp 'a'), More, 'RRcmp reverses twice';
ck (2 R- 5), 3, 'R- (a symbol, as before)';
ck (<b a> Zcoll <a b>).List, (More, Less), 'Zcoll (as before)';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
