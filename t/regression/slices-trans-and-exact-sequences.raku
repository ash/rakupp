# Regression: five unrelated gaps found against the documented examples.
#   * a subscript that is a CALL is a slice when its result is a list —
#     `%orig{ %new.keys } = %new.values` assigned to one key named "b c" before,
#     because sliceness was decided syntactically (list literal / @-var / Range).
#     A call returning a single value still takes the ordinary single-key path.
#   * a bare `%` in TERM position is the anonymous empty Hash (`% .classify-list:
#     …` builds its result in one); as an infix it is still modulo.
#   * a Pair is ONE element: `.keys` is the key, not the index 0. And a Pair whose
#     value is a Hash inverts to one pair per ENTRY.
#   * `.trans` CYCLES a shorter replacement side, and takes `:squash` (collapse a
#     run of the same replacement) and `:complement` (translate everything the
#     left side does NOT name). The adverbs were being read as mappings.
#   * `$s.substr-rw(from, len) = …` splices in place; a zero length inserts.
#   * a list of Matches answers `.from`/`.to` — the span it covers.
#   * an arithmetic sequence over Rats stays exact: `⅓, ⅔ … 30` HITS 15. Stepping
#     through doubles drifted, so `.first(15)` never matched.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a call as a slice subscript
my %orig = :1a, :2b; my %new = :5b, :6c;
%orig{ %new.keys } = %new.values;
check(%orig.raku, '{:a(1), :b(5), :c(6)}', 'a method call as a slice subscript');
my %one = :1a;
sub akey { 'a' }
%one{ akey() } = 9;
check(%one.raku, '{:a(9)}', 'a call returning one value is still one key');

# the anonymous Hash term
my @mapper = [['1a','1b','1c'],['2a','2b','2c'],['3a','3b','3c']];
check((% .classify-list: @mapper, 1,2,1,1,2,0).gist,
      '{1a => {1b => {1c => [0]}}, 2a => {2b => {2c => [1 1 1]}}, 3a => {3b => {3c => [2 2]}}}',
      'a bare % is an empty Hash');
check(7 % 3, '1', 'and % between terms is still modulo');
check((10 % 4), '2', 'even parenthesised');

# a Pair is one element
my $p = (Raku => 'd');
check($p.keys.raku,   '("Raku",).Seq',    'keys of a Pair');
check($p.values.raku, '("d",).Seq',       'values');
check($p.pairs.raku,  '(:Raku("d"),).Seq','pairs');
check($p.kv.raku,     '("Raku", "d").Seq','kv is both');
check(:foo{ :42a }.invert.raku, '((:a(42)) => "foo",).Seq', 'a Hash value inverts per entry');
check((a => (1,2)).invert.raku, '(1 => "a", 2 => "a").Seq', 'and a list value per element');

# trans
check("abcd".trans('abcd' => 'xy'),        'xyxy',      'a short replacement side cycles');
check("a123b123c".trans('123' => "\c[LATIN SMALL LETTER THORN]\c[LATIN SMALL LETTER ETH]"),
      "a\c[LATIN SMALL LETTER THORN]\c[LATIN SMALL LETTER ETH]\c[LATIN SMALL LETTER THORN]b\c[LATIN SMALL LETTER THORN]\c[LATIN SMALL LETTER ETH]\c[LATIN SMALL LETTER THORN]c",
      'across multi-byte characters too');
check("a123b123c".trans(['a'..'z'] => 'x', :complement),        'axxxbxxxc', ':complement');
check("aaa1123bb123c".trans('a'..'z' => 'A'..'Z', :squash),     'A1123B123C', ':squash');
check("aaa1123bb123c".trans('a'..'z' => 'x', :complement, :squash), 'aaaxbbxc', 'both');
check("a123b123c".trans('23' => '4'),      'a144b144c', 'plain trans is unaffected');

# substr-rw as an lvalue
my $s = 'abc';
$s.substr-rw(1, 1) = 'z';
check($s, 'azc', 'substr-rw replaces');
$s.substr-rw(2, 0) = '-Zorro-';
check($s, 'az-Zorro-c', 'and a zero length inserts');
my $s2 = 'abc';
substr-rw($s2, 1, 1) = 'z';
check($s2, 'azc', 'the sub form writes through too');

# a list of Matches spans
'abcdefg' ~~ /(c)(d)/;
check($/.list.from, '2', 'from of a Match list');
check($/.list.to,   '4', 'and to');
"abc123def" ~~ m:g/\d/;
check($/.list.from, '3', 'a :g match reports its first');

# exact Rat sequences
check((⅓,⅔…30).first(0xF), '15', 'a Rat sequence hits an exact value');
check((1/3, 2/3 ... 3)[3].^name, 'Rat', 'its elements stay Rat');
check((1.5, 2.5 ... 5).gist,  '(1.5 2.5 3.5 4.5)', 'a decimal step stays exact');
check((1, 3 ... 11).gist,     '(1 3 5 7 9 11)',    'an Int step is unaffected');
check((1, 2, 4 ... 32).gist,  '(1 2 4 8 16 32)',   'and so is a geometric one');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
