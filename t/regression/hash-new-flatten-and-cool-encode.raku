# Reported: github.com/ash/rakupp/issues/12 — a bencode grammar produced a
# scrambled dictionary, lost a binary blob, and crashed on encode. Three separate
# bugs, all reachable from ordinary Raku:
#
#   1. Hash.new FLATTENS its arguments before pairing them, all the way down.
#      Spreading only one level made `Hash.new: @keys Z @values` build a single
#      entry whose key was a stringified tuple: {"bar spam" => $("foo", 42)}.
#      An ITEMIZED list still does not flatten — that part was already right.
#   2. .decode did not validate UTF-8, so malformed bytes came back as a string
#      full of U+FFFD instead of throwing. That silently broke the documented
#      `(try $blob.decode) // $blob` fallback, which is how you keep a binary
#      blob binary.
#   3. A List is a Cool, so `.encode` stringifies first. It was gated to Str and
#      died with "No such method 'encode' for invocant of type 'List'".
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. Hash.new flattening
my @k = 'bar', 'foo';
my @v = 'spam', 42;
check(Hash.new(@k Z @v).raku, '{:bar("spam"), :foo(42)}', 'Hash.new over a zip');
check(Hash.new((("a","1"),("b","2"))).raku, '{:a("1"), :b("2")}', 'a list of 2-element lists');
check(Hash.new(("a","1","b","2")).raku,     '{:a("1"), :b("2")}', 'the flat form still works');
check(Hash.new((("a","1"),"b","2")).raku,   '{:a("1"), :b("2")}', 'a mixed shape flattens too');
check(Hash.new(((("a","1"),),("b","2"))).raku, '{:a("1"), :b("2")}', 'and it flattens deeply');
check(Hash.new((a=>1),(b=>2)).raku,         '{:a(1), :b(2)}',     'Pairs pass through');
# an ITEMIZED sublist is one item, so it stringifies into the key — Rakudo agrees
check(Hash.new(($("a","1"),$("b","2"))).raku, '{"a 1" => $("b", "2")}',
      'an itemized sublist does NOT flatten');

# 2. decode validates UTF-8
my $bad = "a\x[FF]c".encode('latin-1');
check($bad.^name, 'Blob[uint8]', 'latin-1 encode yields a Blob[uint8]');
check(((try $bad.decode) // $bad).raku, 'Blob[uint8].new(97,255,99)',
      'a malformed decode fails, so the blob survives');
check("ok".encode.decode, 'ok', 'valid UTF-8 still round-trips');
check("héllo".encode.decode, 'héllo', 'including multi-byte');
my $threw = False;
try { Blob.new(97,255,99).decode; CATCH { default { $threw = True } } }
@fail.push('malformed UTF-8 did not throw') unless $threw;

# 3. .encode on a List stringifies (Cool), .decode does not exist there
check((1,2).encode.raku, 'utf8.new(49,32,50)', 'a List encodes via its Str');
check([1,2].encode.raku, 'utf8.new(49,32,50)', 'and so does an Array');
my $nodecode = False;
try { (1,2).decode; CATCH { default { $nodecode = True } } }
@fail.push('.decode on a List should not exist') unless $nodecode;

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
