# JSON::Fast's to-json/from-json get a native fast path (wrapJsonFastExports):
# the module loads from disk unchanged, the CALL runs the engine's codec when
# the arguments are covered, and falls back to the module's own sub otherwise.
# These assertions pin the OUTPUT BYTES of the covered matrix — the same file
# passes under Rakudo, where to-json IS the pure-Raku module, so any drift
# between the replica and the real thing fails here first.
#
# (Two shapes are deliberately spelled via variables: `to-json((a => 1))` and
# `to-json([green])` trip pre-existing argument-parsing divergences unrelated
# to JSON — a parenthesized Pair reads as a named arg, an enum term inside an
# array literal misparses in an argument list.)

use Test;
use JSON::Fast;
plan 16;

is to-json({ b => 2, a => [1, "x", 2.5, True, Any] }, :sorted-keys),
   qq[\{\n  "a": [\n    1,\n    "x",\n    2.5,\n    true,\n    null\n  ],\n  "b": 2\n\}],
   'pretty object: sorted keys, nested array, ": " separator';
is to-json([1, [2, 3]], :sorted-keys),
   qq{[\n  1,\n  [\n    2,\n    3\n  ]\n]}, 'pretty nesting indents by 2';
is to-json("tab\there \"q\" back\\slash\nnl"),
   Q["tab\there \"q\" back\\slash\nnl"], 'string escapes';
is to-json("bell\x[7]"), Q["bell\u0007"], 'other control chars as \u%04x';
is to-json([]), "[\n]",   'empty array, pretty';
is to-json({}), "\{\n\}", 'empty hash, pretty';
is to-json(42), '42', 'Int';
is to-json(2.5e0), '2.5e0', 'Num gets e0';
is to-json(1e10), '10000000000e0', 'large Num';
is to-json(0.5), '0.5', 'Rat';
is to-json(3.0), '3.0', 'integral Rat keeps .0';
is to-json([1,2], :!pretty), '[1,2]', ':!pretty array';
is to-json({a=>1, b=>[2,3]}, :!pretty, :sorted-keys), '{"a":1,"b":[2,3]}', ':!pretty object';
is to-json({a=>1}, :spacing(4)), qq[\{\n    "a": 1\n\}], ':spacing';
enum RxColor <red green blue>;
my $g = green;
is to-json([$g], :!pretty), '["green"]', 'enum serializes as its key';
is-deeply from-json('{"a": [1, 2.5, "x", true, null], "b": 1e3}'),
   ${ a => [1, 2.5, "x", True, Any], b => 1e3 }, 'from-json round-trip typing';
