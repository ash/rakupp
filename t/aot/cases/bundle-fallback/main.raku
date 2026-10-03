# The program falls back to bundling; its module keeps native bodies
# (t/aot/run.raku).
#aot-expect: bundled
use Aot::Bundled;

say fib(40);
say words-by-length('a bb cc d eee');
my $s = Stack.new.push(1).push(2).push(3);
say $s.pop;
say ~$s;
my $t = $s but Tagged;    # a `but` mixin: not compiled natively, so the program bundles
say $t.tag;
