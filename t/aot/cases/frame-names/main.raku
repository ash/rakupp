# Parameters and outer names through the frame (t/aot/run.raku).
use Aot::Frame;

say positional(1, 2);
say defaults('a');
say defaults('a', 'b', :c<x>, :d<y>);
say slurpy(1, 2, 3, :z, :y(2));
say slurpy('only');
say no-signature(1, 2, 3, :k(1));
say copied('wow');
say typed('ab');
say typed('ab', :times(3));
say counter() for ^3;
say log-lines();
say remember('x');
say remember('y');
say remember('x');
say greet('world');
set-greeting('hi');
say greet('there');
say outer-with-inner('abc');
say uses-own-uc('q');
say apply(-> $n { $n * 3 }, 2);
say apply(&uc, 'z');
