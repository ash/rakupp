# A native body whose C++ does not compile costs only that routine
# (t/aot/run.raku).
#aot-compile-env: RAKUPP_AOT_FAULT_CXX=broken,broken-method
use Aot::FaultCxx;

say before(1);
say broken(2);
say after(3);
my $t = Thing.new(n => 7);
say $t.fine;
say $t.broken-method;
