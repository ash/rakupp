# A module beside the refused one: its table is intact and attaches.
unit module Aot::TableB;
use Aot::TableA;

sub b-uses-a($x) is export {    #aot: native
    "b(" ~ second-a($x) ~ ")"
}
sub b-alone() is export {       #aot: native
    'b-alone'
}
