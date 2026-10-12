# Regression: rakupp-json-codec('from-json' | 'to-json', &fallback) — the
# engine's JSON codec as a ROUTINE with a fallback built in. A module standing
# in for JSON::Fast exports what it returns, so a call has no Raku-level frame
# in front of the codec; anything the codec does not cover goes to the
# fallback with the very same arguments. Raku++ only.

use Test;

my &maker = try &::('rakupp-json-codec');
unless &maker {
    plan skip-all => 'rakupp-json-codec is a Raku++ builtin';
    exit;
}

plan 12;

my @seen;
my &parse = maker('from-json', sub (|c) { @seen.push(c); 'FALLBACK' });
my &write = maker('to-json',   sub (|c) { @seen.push(c); 'FALLBACK' });

is-deeply parse('{"a":[1,2.5,true,null]}'), {a => [1, 2.5, True, Any]}, 'the codec parses';
is-deeply parse('[1,2]', :immutable), (1, 2), 'and takes :immutable';
is +@seen, 0, 'without asking the fallback';

is parse('[1,]'), 'FALLBACK', 'a document it refuses goes to the fallback';
is @seen[*-1].list[0], '[1,]', 'with the same text';
is parse('[1]', :frobnicate), 'FALLBACK', 'so does an adverb it does not take';
ok @seen[*-1].hash<frobnicate>, 'with the same adverb';

is write({a => [1]}, :!pretty), '{"a":[1]}', 'the codec writes';
is write(Date.new(2026, 10, 12)), 'FALLBACK', 'a type outside the ladder goes to the fallback';
is write(\(1, 2)), 'FALLBACK', 'and a Capture';

dies-ok { maker('from-yaml', sub (|) { }) }, 'an unknown codec is refused';
is &parse.name, 'from-json', 'the routine is named for what it does';
