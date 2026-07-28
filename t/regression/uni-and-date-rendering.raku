# Regression: rendering belongs to the VALUE, not to one method arm.
#
#   * a Uni / NFC / NFD / NFKC / NFKD is an array of codepoints tagged in `s`.
#     Its `NFD:0x<0044 0323>` form lived only in the `.gist` METHOD, so a direct
#     `$u.gist` was right while `say $u`, string interpolation and any container
#     holding one printed the raw array `[68 803]`. The hex form is in
#     Value::gist now, and `.Str` decodes back to the text.
#   * `"x".NFD` tagged its result the generic "Uni" rather than the normalisation
#     form it had just applied, so `.gist` said Uni: and `.raku` lost the `.NFD`
#     suffix.
#   * the ISO 8601 date formatter was written out TWICE, verbatim — in the value
#     model and in the .Str/.gist/.yyyy-mm-dd arm — including the year>9999 `+`
#     prefix, the ±HH:MM suffix and the fractional-second branch. One copy now.
#
# The codepoint order is the ORIGINAL one and must never be routed through
# normalization: that is what `.NFD.gist` vs `.NFC.gist` distinguishes.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my $u = "Ḍ".NFD;
check($u.gist,        'NFD:0x<0044 0323>', 'a direct .gist');
check($u.Str,         "\c[LATIN CAPITAL LETTER D]\c[COMBINING DOT BELOW]", '.Str decodes to the text');
check("$u",           $u.Str,              'and interpolation agrees with .Str');
check([$u].gist,      '[NFD:0x<0044 0323>]', 'inside an Array');
check(($u,).gist,     '(NFD:0x<0044 0323>)', 'inside a List');
check(%(k => $u).gist, '{k => NFD:0x<0044 0323>}', 'as a hash value');
check($u.raku,        'Uni.new(0x0044, 0x0323).NFD', '.raku names the form');

my $c = "Ḍ".NFC;
check($c.gist,  'NFC:0x<1e0c>',            'NFC composes');
check($c.raku,  'Uni.new(0x1e0c).NFC',     'and reprs as NFC');
check($u.gist eq $c.gist, 'False',         'the two forms stay distinct');

# dates, through both the value model and the method
check(Date.new(2016,2,29).gist,       '2016-02-29', 'a Date gist');
check(Date.new(2016,2,29).Str,        '2016-02-29', 'and .Str');
check(Date.new(2016,2,29).yyyy-mm-dd, '2016-02-29', 'and .yyyy-mm-dd');
check([Date.new(2016,2,29)].gist,     '[2016-02-29]', 'and nested in a container');
my $d = DateTime.new(:2016year,:2month,:29day,:13hour,:4minute,:5second);
check($d.gist, '2016-02-29T13:04:05Z', 'a DateTime gist');
check("$d",    $d.Str,                 'interpolation agrees');
check(DateTime.new(:2016year,:second(5.25)).gist,
      '2016-01-01T00:00:05.250000Z', 'fractional seconds keep six places');
check(DateTime.new(:2016year,:timezone(-19800)).gist,
      '2016-01-01T00:00:00-05:30', 'a negative half-hour offset');
check(Date.new(12016,1,1).gist, '+12016-01-01', 'a year past 9999 carries a +');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
