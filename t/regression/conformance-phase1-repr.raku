# Regression: raku-spec conformance Phase 1 — representation defects found by
# running every documented example on both engines (see raku-spec/CONFORMANCE.md).
#   1. a Seq's `.raku` is the list form plus the coercion that rebuilds it,
#      `(1, 2).Seq` — .gist/.Str stay `(1 2)`.
#   2. `.kv`/`.keys`/`.values`/`.pairs`/`.antipairs`/`.invert` answer a Seq on
#      EVERY container, not a List on some of them.
#   3. Pair.antipairs/.invert existed only as the singular `.antipair`; a list
#      VALUE inverts to one pair per element. Inverted keys keep their type.
#   4. a Capture is TWO collections: `.elems` counts positionals only, and
#      .keys/.values/.pairs run positionals then nameds.
#   5. an itemized container shows its `$` marker at the TOP level of .raku.
#   6. `blob8` IS `Blob[uint8]`; the hex groups by ELEMENT, big-endian.
#   7. pop/shift of an empty Array is a FAILURE — falsy, so `while @a.shift`
#      terminates, but it detonates with X::Cannot::Empty when the value is used.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. Seq.raku
check((1, 2).Seq.raku,        '(1, 2).Seq',    'seq-raku');
check(().Seq.raku,            '().Seq',        'seq-raku-empty');
check((1,).Seq.raku,          '(1,).Seq',      'seq-raku-one');
check((^3).map({ $_ }).raku,  '(0, 1, 2).Seq', 'map-is-seq');
check((1, 2).Seq.gist,        '(1 2)',         'seq-gist-unchanged');
check((1, 2).Seq.Str,         '1 2',           'seq-str-unchanged');
check((1, 2).List.raku,       '(1, 2)',        'list-raku-unchanged');
check([1, 2].raku,            '[1, 2]',        'array-raku-unchanged');

# 2. the five accessors are Seq everywhere
my $p = (a => 1); my %h = a => 1; my @a = 10, 20;
for 'pair' => $p, 'hash' => %h, 'array' => @a, 'list' => (1, 2) -> $c {
    for <kv keys values pairs antipairs> -> $m {
        my $got = $c.value."$m"().^name;
        @fail.push($c.key ~ ".$m is $got") unless $got eq 'Seq';
    }
}

# 3. Pair.antipairs / .invert
check($p.antipairs.raku,          '(1 => "a",).Seq',        'pair-antipairs');
check($p.invert.raku,             '(1 => "a",).Seq',        'pair-invert');
check((a => (1, 2)).invert.raku,  '(1 => "a", 2 => "a").Seq', 'pair-invert-list-value');
check(%h.invert.raku,             '(1 => "a",).Seq',        'hash-invert-keeps-key-type');
check(%h.antipairs.raku,          '(1 => "a",).Seq',        'hash-antipairs-keeps-key-type');

# 4. Capture partitions
my $c = \(1, 2, :x(3));
check($c.elems,        '2',                       'capture-elems-is-positionals');
check($c.list.raku,    '(1, 2)',                  'capture-list');
check($c.keys.raku,    '(0, 1, "x").Seq',         'capture-keys');
check($c.values.raku,  '(1, 2, 3).Seq',           'capture-values');
check($c.pairs.raku,   '(0 => 1, 1 => 2, :x(3)).Seq', 'capture-pairs');
check($c.hash.WHAT.gist, '(Map)',                 'capture-hash-is-a-map');

# 5. the itemized marker, top level only
check($[1, 2, 3].raku, '$[1, 2, 3]', 'itemized-array');
check($(1, 2).raku,    '$(1, 2)',    'itemized-list');
my @nested = $[1, 2];
check(@nested.raku,    '[[1, 2],]',  'itemized-element-has-no-marker');

# 6. blob element type and hex grouping
check(blob8.new(1, 2).gist,          'Blob[uint8]:0x<01 02>',            'blob8-is-parameterized');
check(Blob[uint8].new(3, 6, 254).gist, 'Blob[uint8]:0x<03 06 FE>',       'blob-param-spelling');
check(blob32.new(1, 2).gist,         'Blob[uint32]:0x<00000001 00000002>', 'blob32-groups-by-element');
check(Buf.new(1, 42, 3).gist,        'Buf:0x<01 2A 03>',                 'plain-buf-unparameterized');

# 7. pop/shift of an empty Array
my @e;
@fail.push('empty-shift-is-truthy') if ?(@e.shift);
my $n = 0;
my @drain = 1, 2;
while @drain.shift -> $x { $n++ }
check($n, '2', 'while-shift-terminates');
my $detonated = (try { my @z; ~@z.pop; 'no' }) // 'yes';
check($detonated, 'yes', 'empty-pop-detonates-when-used');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
