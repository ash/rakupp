# Regression: a LEAVE written after other statements of a routine is armed at
# the routine's start when it reads nothing declared before it, so `--exe`
# compiles the routine natively (examples/life.raku's `print …; LEAVE print …`
# had started bundling), and it still runs when an earlier statement dies. One
# that reads an earlier `my` keeps the routine interpreted, as before.
#
# t/exe/parity.raku compiles this file and compares the binary with the
# interpreter. Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @log;
sub plain   { @log.push('a'); LEAVE @log.push('left'); @log.push('b'); 'r' }
sub dies    { die 'early'; LEAVE @log.push('left after death') }
sub two     { LEAVE @log.push('first'); @log.push('x'); LEAVE @log.push('second'); 1 }
sub reads   { my $v = 'v'; LEAVE @log.push("read $v"); 2 }
plain();
try dies();
two();
reads();
my $ok = @log eqv ['a', 'b', 'left', 'left after death', 'x', 'second', 'first', 'read v'];
say $ok ?? 'PASS' !! "FAIL\n{@log.raku}";
