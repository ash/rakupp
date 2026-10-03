# `is rw` arguments from native bodies (t/aot/run.raku).
use Aot::Rw;

say locals();
say elements();
say swapped();
say through-method(Counter.new);
say outer-name();
say module-var();
say mixed();
