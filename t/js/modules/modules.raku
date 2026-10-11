# `use`d modules under --target=js (t/js/run.raku compiles this with -I lib):
# each is compiled into the program in a scope of its own, and what it exports
# is bound where the program can see it.
use JsMod::A;
use JsMod::B;
use JsMod::C;
use JsMod::E;
say f(2);
say h(3);
say JsMod::C.new(n => 4).doubled;
say JsMod::C.cached(5) === JsMod::C.cached(5);
say listing().elems;
say JsMod::A::g();
