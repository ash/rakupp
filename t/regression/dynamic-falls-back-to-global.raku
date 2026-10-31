# Regression: a dynamic variable no caller declares is GLOBAL's, twigil
# dropped, before it is an error (issue #77, Red's "$*RED-DB wasn't
# defined").
#
# Red's `red-defaults default => database …` writes
# `%GLOBAL::RED-DEFAULT-DRIVERS`, and get-RED-DB reads
# `%*RED-DEFAULT-DRIVERS<default>`. The dynamic lookup never looked in GLOBAL,
# so every test that set its database that way died "$*RED-DB wasn't
# defined". Only what IS GLOBAL's counts — a `GLOBAL::` write or a top-level
# `our` — not a top-level `my`, which shares rakupp's global scope.

use lib $?FILE.IO.parent.add('lib').Str;
use RakuppGlobalSet;
use RakuppGlobalGet;

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

ck get-default(), 'none', 'nothing set: the probe stays quiet';
set-default 42;
ck get-default(), 42, 'a module\'s %GLOBAL:: write, read as a dynamic by another';

%GLOBAL::FLAGS = a => 1;
ck %*FLAGS<a>, 1, '%*X reads %GLOBAL::X';
$GLOBAL::LEVEL = 5;
sub level { $*LEVEL }
ck level(), 5, '…and $*X reads $GLOBAL::X';
sub shadowed { my $*LEVEL = 9; level() }
ck shadowed(), 9, 'a caller\'s own declaration still wins';

our $COUNT = 1;
sub count { $*COUNT }
ck count(), 1, 'a top-level our is GLOBAL\'s';
our $LATER;
$LATER = 3;
sub later { $*LATER }
ck later(), 3, '…declared without a value too';
sub bump { $*COUNT = 7 }
bump();
ck $COUNT, 7, 'and a write through $*X reaches it';

my $PRIVATE = 2;
sub private { (try $*PRIVATE) // 'not found' }
ck private(), 'not found', 'a top-level my is no dynamic';
module M { our $INNER = 1 }
sub inner { (try $*INNER) // 'not found' }
ck inner(), 'not found', 'nor is a package\'s our';
sub missing { $*NO-SUCH-THING }
ck missing().^name, 'Failure', 'and a name nobody has is the Failure it was';

say 'PASS';
