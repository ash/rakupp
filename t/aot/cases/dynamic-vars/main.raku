# Dynamic variables declared in native module bodies (t/aot/run.raku).
use Aot::Dyn;

say declares();
say declares-and-callee-writes();
say nested(3);
say shadow-and-restore();
my $*DEPTH = 'program';
say reads-callers();
say declares();
say $*DEPTH;
say through-closure();
