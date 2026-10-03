# Regression: writing a read-only parameter in a program compiled with --exe.
# A plain `$p` parameter (of a sub, a pointy block, a `for` loop) became a C++
# local the body could overwrite: `sub w($p) { $p = 5; $p }` answered 5 where
# Rakudo and the interpreter die "Cannot assign to a readonly variable". Every
# store into a variable — `=`, `op=`, `++`, `.=`, `s///`, handing it to an
# `is rw` parameter — now refuses a read-only parameter, and the program
# bundles the interpreter (a module routine stays interpreted as a whole: the
# copy a delegated statement would get is no longer read-only).
#
# t/exe/run.raku compiles this file and compares the binary with the interpreter.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

sub assign($p)  { $p = 5; $p }
sub incr($p)    { $p++; $p }
sub append($p)  { $p ~= 'x'; $p }
sub mutate($p)  { $p .= uc; $p }
sub subst($p)   { $p ~~ s/a/b/; $p }
sub bump($x is rw) { $x++ }
sub pass-on($p) { bump($p); $p }

ck (try assign(1)) // 'died', 'died', '$p = …';
ck (try incr(1))   // 'died', 'died', '$p++';
ck (try append('a')) // 'died', 'died', '$p ~= …';
ck (try mutate('a')) // 'died', 'died', '$p .= meth';
ck (try subst('a'))  // 'died', 'died', '$p ~~ s///';
ck (try pass-on(1))  // 'died', 'died', 'a read-only parameter handed to `is rw`';

my $r = (try { for 1, 2 -> $x { $x = 3 }; 'wrote' }) // 'died';
ck $r, 'died', 'a pointy loop variable';
my $blk = -> $x { $x = 1 };
ck (try { $blk(2); 'wrote' }) // 'died', 'died', 'a pointy block parameter';
my $d = (try { for (1, 2), (3, 4) -> ($a, $b) { $a = 9 }; 'wrote' }) // 'died';
ck $d, 'died', 'a destructured loop variable';

# …and what IS writable still is
sub copy($p is copy) { $p = 5; $p }
sub rw($p is rw) { $p = 6 }
my $v = 1; rw($v);
ck copy(1), 5, '`is copy` writes its own copy';
ck $v, 6, '`is rw` writes the caller\'s variable';
my @w; for 1, 2 -> $x is copy { $x *= 10; @w.push: $x }
ck @w.join(','), '10,20', 'an `is copy` loop variable';
sub shadow($p) { my $q = $p; $q = 7; $q }
ck shadow(1), 7, 'a local copy of the parameter';

if @fail { .say for @fail; say 'FAIL' }
else     { say 'PASS' }
