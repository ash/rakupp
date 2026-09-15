# Dispatch between a candidate that DECLARES a position and one whose capture
# swallows it. Rakudo ranks a satisfied required named first, then the candidate
# that declares more of the positions, and only then falls back to the bands; a
# bare `|` catch-all is the least specific thing there is. Declaration order used
# to settle all of it, so `multi method show(|) {*}` won every call and handed
# back a Whatever (App::Game::Concentration), and `multi sub test-asl($n, |c)`
# recursed into itself 38,000 times instead of reaching the candidate below it
# (Backtrace::Files).
use Test;
plan 6;

class Catch {
    multi method show(|) { 'capture' }
    multi method show($r where 1 <= * <= 4, $c) { 'declared' }
}
is Catch.show(2, 2), 'declared',    'a bare catch-all loses, declared first or not';

class Catch2 {
    multi method show($r, $c) { 'declared' }
    multi method show(|)      { 'capture' }
}
is Catch2.show(2, 2), 'declared',   '…in the other order as well';

multi two-arrays($x, |c)      { 'scalar+capture' }
multi two-arrays(@a, @b, *%_) { 'two-arrays' }
is two-arrays((1,), (2,)), 'two-arrays', 'declaring both positions wins';

multi req-named(Bool:D :$pad!, |c) { 'named-first' }
multi req-named(Str:D $s, |c)      { 'str' }
is req-named("ab", :!pad), 'named-first', 'a satisfied required named outranks that';

class Coerce {
    multi method coerce($a is raw, $b is raw) { 'two-raw' }
    multi method coerce(%dict!, |c)           { 'dict-capture' }
}
is Coerce.coerce({a => 1}, 2), 'two-raw', 'two declared positions beat one and a capture';
is Coerce.coerce(1, 2),        'two-raw', '…whatever the first argument is';
