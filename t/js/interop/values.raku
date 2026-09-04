# --target=js interop golden: values crossing both ways, and EVAL :lang<JavaScript>.
use JS;
say JS.Math.sqrt(16);
say JS.Math.max(3, 9, 2);
say JS.Math.PI.round(0.001);
say JS.JSON.stringify([1, 2, 3]);
say JS.JSON.stringify({ a => 1, b => "two" });
my $arr = JS.JSON.parse('[10, 20, 30]');
say $arr.elems, " ", $arr[1], " ", $arr.list;
my $obj = JS.JSON.parse('{"name":"Camelia","legs":6}');
say $obj<name>, " has ", $obj<legs>, " legs";
say $obj.Hash;
say EVAL '2 ** 10', :lang<JavaScript>;
say EVAL '[1,2,3].map(x => x * 2)', :lang<JavaScript>;
say EVAL 'typeof globalThis.process', :lang<JavaScript>;
my $sorter = JS.Array.from([3, 1, 2]);
say $sorter.sort.join(",");
say JS.String.fromCharCode(82, 97, 107, 117);
say JS.Number.isInteger(3), " ", JS.Number.isInteger(3.5);
say JS.parseInt("42abc");
# a Str crosses by copy and comes back a Str: graphemes here, code units there
say "e\x[301]".chars, " ", EVAL '"e\u0301".length', :lang<JavaScript>;
