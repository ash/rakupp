# Regression: nqp::p6definite, Rakudo's own op rather than NQP's.
#
# It asks the same question as `nqp::isconcrete` — is this a concrete object
# rather than a type object — but hands the answer back as a Raku Bool instead
# of an nqp int, and Rakudo-targeted code relies on that. Compress::Zlib's line
# reader is `gather while nqp::p6definite(my $line = self.get) { ... }`, which
# under an engine that does not know the op dies at the first line read rather
# than at compile time, so `gzslurp` on a valid gzip file failed.
#
# Contract: exit 0 + last line PASS.
use nqp;
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

check nqp::p6definite(42),        True,  'a concrete Int is definite';
check nqp::p6definite('x'),       True,  'a concrete Str is definite';
check nqp::p6definite([]),        True,  'an empty Array is still concrete';
check nqp::p6definite(Int),       False, 'a type object is not';
check nqp::p6definite(Any),       False, 'nor is Any';
check nqp::p6definite(Nil),       False, 'nor is Nil';

# The Bool is the whole point: isconcrete answers an nqp int, p6definite a Raku
# Bool, and code that stores the answer can tell the difference.
check nqp::p6definite(42).^name,  'Bool', 'the answer is a Raku Bool';
check nqp::p6definite(Int).^name, 'Bool', 'for the false case too';

# The loop shape the op exists for: read until the source runs dry.
my @lines = <alpha beta gamma>;
my @seen = gather while nqp::p6definite(my $line = @lines.shift // Nil) {
    take $line;
}
check @seen.join(','), 'alpha,beta,gamma', 'it terminates a read loop';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
