# The module whose routine table main.raku's RAKUPP_AOT_FAULT_TABLE corrupts:
# its native bodies were compiled (the annotations say so), but at run time
# the table no longer fits the module's AST, so none of them may be attached —
# not even the entries that still fit — and every routine runs interpreted.
unit module Aot::TableA;

sub first-a($x) is export {     #aot: native
    "first-a $x"
}
sub second-a($x) is export {    #aot: native
    "second-a " ~ first-a($x)
}
class A is export {
    method who() {              #aot: native
        'A.who'
    }
}
sub last-a() is export {        #aot: native
    'last-a'
}
