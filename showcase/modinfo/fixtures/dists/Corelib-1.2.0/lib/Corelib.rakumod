unit module Corelib;

#| The one thing this fixture distribution provides.
sub core-greeting(Str $who = 'world' --> Str) is export {
    "hello, $who"
}
