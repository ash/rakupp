# Regression: a Range remembers the endpoint OBJECTS it was written with.
# rakupp stored only the integers it iterates over, so `1/2 .. 1/3` gisted
# `0.5..0.3333…` instead of `0.5..<1/3>` and `True .. False` came out `1..0`.
# The objects ride in the otherwise-unused `ext`, so a plain `1..5` allocates
# nothing; iteration still walks the numeric fields.
# The rule is Rakudo's: a Real or undefined endpoint is kept as it is, a Str
# ("2" -> 2) or a list (-> its element count) is NUMIFIED. `minmax` follows the
# same rule, except that two Str extremes stay Strs.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# kept as written
check((1/2 .. 1/3).gist,   '0.5..<1/3>',              'rat-endpoints');
check((True .. False).gist, 'Bool::True..Bool::False', 'bool-endpoints');
check((1 .. 2.5).gist,     '1..2.5',                  'rat-upper');
check((1 .. 1e0).gist,     '1..1e0',                  'num-upper');
check((1 .. Any).gist,     '1..Any',                  'undefined-upper');
check((1.5 ..^ 3.5).gist,  '1.5..^3.5',               'fractional-exclusive');

# numified, as Rakudo does
check((1 .. "2").gist,     '1..2',                    'str-endpoint-numifies');
check(((1, 2) .. (3, 4, 5)).gist, '2..3',             'list-endpoint-numifies');

# plain integer ranges are untouched
check((1 .. 5).gist,       '1..5',                    'int-range');
check((1 ^..^ 5).gist,     '1^..^5',                  'int-range-exclusive');
check(("a" .. "e").gist,   '"a".."e"',                'str-range');

# .min/.max/.bounds answer the objects, not the integers iterated over
check((1/2 .. 1/3).min.^name, 'Rat', 'min-type');
check((1/2 .. 1/3).max.raku,  '<1/3>', 'max-value');
check((1/2 .. 1/3).bounds.raku, '(0.5, <1/3>)', 'bounds');
check(("a" .. "e").bounds.raku, '("a", "e")', 'str-bounds');
check((1 .. 5).min.^name, 'Int', 'int-min-type');

# iteration is unaffected — and a Rat endpoint yields Rats, not Nums
check((1 .. 5).list.raku,     '(1, 2, 3, 4, 5)', 'int-iteration');
check((1 ..^ 5).list.raku,    '(1, 2, 3, 4)',    'int-exclusive-iteration');
check((1.5 .. 3.5).list.raku, '(1.5, 2.5, 3.5)', 'rat-iteration');
check(("a" .. "e").list.raku, '("a", "b", "c", "d", "e")', 'str-iteration');
check((1 .. 5).sum,           '15',              'sum');
check(([+] (1 .. 100)),       '5050',            'reduce-over-range');

# minmax spans the extreme ELEMENTS
check(("a" minmax "b").gist,  '"a".."b"',                'minmax-str');
check((True minmax False).gist, 'Bool::False..Bool::True', 'minmax-bool');
check((1/2 minmax 1/3).gist,  '<1/3>..0.5',              'minmax-rat');
check((3 minmax 1).gist,      '1..3',                    'minmax-int');
check((1 minmax "2").gist,    '1..2',                    'minmax-mixed-numifies');
check((1 minmax Any).gist,    '1..1',                    'minmax-skips-undefined');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
