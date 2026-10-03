# Statements delegated to the interpreter from native bodies (t/aot/run.raku).
use Aot::Delegate;

my $k = Kid.new;
say $k.parent-who;
say qualified($k);
say tail-qualified($k);
try dies-midway($k);
say $!.message;
say peek();
say with-topic([$k, Kid.new]);
say with-match($k, 'abc 42');
say with-match($k, 'none');
say counts($k);
say counts($k);
say set-through(Box.new, 7);
say to-celsius(21);
say to-celsius-assigned(-4);
