# Regression: Rakupp::Internals::JSON.to-json and its Rakudo::Internals::JSON
# alias wrote with a compact-only writer of their own that dropped every
# adverb. zef calls `Zef::to-json(:pretty, {...})` through the Rakudo name for
# its test reports and got one long line; :sorted-keys, which Rakudo's
# CompUnit::Repository::Installation passes for its dist records, did
# nothing, so key order was insertion order. The class now writes with the
# engine's one writer, the JSON::Fast-exact codec `use JSON::Native` runs,
# under Rakudo's surface for this method: compact unless :pretty, with
# :spacing, :level and :sorted-keys honoured.
#
# The expected texts are Rakudo 2026.09's own output, and the file passes
# there too. Hash order is unspecified in Rakudo, so a hash of more than one
# key is written with :sorted-keys.

use Test;

my \J = ::('Rakudo::Internals::JSON');

my %h = b => 1, a => [1, 2.5, 3e0, -0.125, 10**30, "x\ty\n\"q\\", Any, True, False],
        c => {}, d => [], e => { z => 1, y => [], x => { w => [[], {}] } },
        f => "é \x[1F600] \x[1]", g => 1/4, h => 1e-20, i => 2/1;

my $compact = Q|{"a":[1,2.5,3e0,-0.125,1000000000000000000000000000000,"x\ty\n\"q\\",null,true,false],|
            ~ Q|"b":1,"c":{},"d":[],"e":{"x":{"w":[[],{}]},"y":[],"z":1},|
            ~ Q|"f":"é \uD83D\uDE00 \u0001","g":0.25,"h":1e-20,"i":2.0}|;

my $pretty = Q:to/END/.chomp;
{
  "a": [
    1,
    2.5,
    3e0,
    -0.125,
    1000000000000000000000000000000,
    "x\ty\n\"q\\",
    null,
    true,
    false
  ],
  "b": 1,
  "c": {
  },
  "d": [
  ],
  "e": {
    "x": {
      "w": [
        [
        ],
        {
        }
      ]
    },
    "y": [
    ],
    "z": 1
  },
  "f": "é \uD83D\uDE00 \u0001",
  "g": 0.25,
  "h": 1e-20,
  "i": 2.0
}
END

plan 17;

is J.to-json(%h, :sorted-keys), $compact, 'compact by default, keys sorted';
is J.to-json(%h, :!pretty, :sorted-keys), $compact, ':!pretty is the default';
is J.to-json(%h, :pretty, :sorted-keys), $pretty, ':pretty indents by two';
is J.to-json(:pretty, :sorted-keys, %h), $pretty, 'adverbs ahead of the value, as zef writes them';
is J.to-json(%h, :pretty, :sorted-keys, :spacing(4)), $pretty.subst(/^^ (' '+)/, { ' ' x 2 * $0.chars }, :g),
    ':spacing(4) doubles the indent';
is J.to-json({ k => [1, {}] }, :pretty, :spacing(0)), "\{\n\"k\": [\n1,\n\{\n}\n]\n}", ':spacing(0)';
is J.to-json([1, { a => 2 }], :pretty, :level(1)), "[\n    1,\n    \{\n      \"a\": 2\n    }\n  ]",
    ':level starts the indent deeper';
is J.to-json([1, [2, [3, []]], {}]), '[1,[2,[3,[]]],{}]', 'nested arrays, compact';
is J.to-json([], :pretty) ~ J.to-json({}, :pretty), "[\n]\{\n}", 'empty containers, pretty';
is J.to-json((a => [1]), :pretty), "\{\n  \"a\": [\n    1\n  ]\n}", 'a Pair is a one-key object';
is J.to-json(Map.new((m => 1)), :pretty), "\{\n  \"m\": 1\n}", 'a Map is an object';
is (42, -7, 1.5, 2e10, "s", True, Any).map({ J.to-json($_) }).join(' '),
    '42 -7 1.5 20000000000e0 "s" true null', 'scalars';
is J.to-json((1..3).map(* * 2)) ~ J.to-json(1..3) ~ J.to-json((1, 2)), '[2,4,6][1,2,3][1,2]',
    'a Seq, a Range and a List are arrays';
is J.to-json([1, 2], :frobnicate), '[1,2]', 'a named argument no writer takes is ignored';

# zef's own wrapper: `our sub to-json(|c) { ::("Rakudo::Internals::JSON").to-json(|c) }`
sub zef-to-json(|c) { ::("Rakudo::Internals::JSON").to-json(|c) }
is zef-to-json(:pretty, { name => 'Foo', depends => ['Bar'] }.Hash, :sorted-keys),
    "\{\n  \"depends\": [\n    \"Bar\"\n  ],\n  \"name\": \"Foo\"\n}", "zef's report call";
is zef-to-json({ name => 'Foo' }), '{"name":"Foo"}', "zef's META6 call";

# The first-party name is the same writer.
if $*RAKU.compiler.name eq 'rakudo' {
    skip 'Rakupp::Internals::JSON is Raku++\'s';
}
else {
    is ::('Rakupp::Internals::JSON').to-json(%h, :pretty, :sorted-keys), $pretty,
        'Rakupp::Internals::JSON writes the same text';
}
