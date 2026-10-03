# RAKUPP_AOT_FAULT_CXX (set by main.raku) plants a C++ error in the native
# bodies of `broken` and `Thing.broken-method`. The compiler's errors are
# traced back to those two routines, which keep their interpreted bodies; the
# binary is built again with every other routine native.
unit module Aot::FaultCxx;

sub before($x) is export {          #aot: native
    "before($x)"
}
sub broken($x) is export {          #aot: interpreted (did not compile)
    "broken($x) still answers"
}
sub after($x) is export {           #aot: native
    "after($x) " ~ broken($x)
}
class Thing is export {
    has $.n = 1;
    method fine() {                 #aot: native
        "fine {$!n}"
    }
    method broken-method() {        #aot: interpreted (did not compile)
        "broken-method {$!n}"
    }
}
