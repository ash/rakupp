#!raku

use v6;
use Test;
use YAMLish;

is-deeply(load-yaml('[1,2,3]'), [1,2,3], 'Bogus starter test');
is-deeply(load-yaml('[1,2,3,]'), [1,2,3], 'Bogus starter test');
is-deeply(load-yaml("[1,2,3]\n"), [1,2,3], 'Bogus starter test');
is-deeply(load-yaml("[a, b, 33]\n"), ['a','b',33], 'Bogus starter test');
is-deeply(load-yaml("\{a: 42}\n"), {a => 42}, 'Bogus starter test');
is-deeply(load-yaml("\{a: 42,}\n"), {a => 42}, 'Bogus starter test');
is-deeply(load-yaml("\{a: 42, b: 43}\n"), {a => 42, b => 43}, 'Bogus starter test');
is-deeply(load-yaml("\{}\n"), {}, 'Bogus starter test');
is-deeply(load-yaml('foo'), 'foo', 'Bogus starter test');

done-testing();
