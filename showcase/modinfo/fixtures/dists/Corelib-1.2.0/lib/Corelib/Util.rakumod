unit module Corelib::Util;

sub shout(Str $s --> Str) is export { $s.uc ~ '!' }
