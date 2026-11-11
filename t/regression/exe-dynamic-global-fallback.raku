# --exe: a dynamic variable no caller declares is GLOBAL's, twigil dropped,
# before it is an error — for compiled code as for the interpreter. Compiled,
# a read of `$*X` that no `my $*X` declares answered Any where `$GLOBAL::X`
# held a value (and where nothing did, Any instead of the armed Failure), and
# a write minted a new variable instead of reaching the top-level `our $X`
# or throwing X::Dynamic::NotFound. Found by t/exe/parity.raku
# (dynamic-falls-back-to-global.raku). Every line answers the same under
# Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

our %FLAGS = a => 1;
sub flags { %*FLAGS<a> }
check flags(), 1, '%*X reads a top-level our %X';
our $LEVEL = 5;
sub level { $*LEVEL }
check level(), 5, '$*X reads a top-level our $X';

our $COUNT = 1;
sub bump { $*COUNT = 7 }
bump();
check $COUNT, 7, 'a write through $*X reaches the top-level our';

sub missing { $*NO-SUCH-THING }
check missing().^name, 'Failure', 'a name nobody has is a Failure';
sub write-missing { $*NO-SUCH-THING = 1 }
try write-missing();
check $!.^name, 'X::Dynamic::NotFound', '…and writing it throws';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
