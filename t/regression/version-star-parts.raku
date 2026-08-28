# Regression: Version.new('*').parts holds the STRING '*' (2026-08-28), as
# Rakudo stores it — not a Whatever. The Whatever leaked into module code:
# META6's unmarsh-version asks `$ver.parts[0] eq 'v'`, and a runtime Whatever
# curried that into a (truthy) WhateverCode, so "version": "*" was shifted
# down to an EMPTY version and Test::META's asterisk check passed vacuously
# where Rakudo fails it. Found writing raku.online's Test::META/TAP pages;
# every check Rakudo-verified.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

ck(Version.new('*').parts,     ('*',),        "a bare-star version's parts are the Str '*'");
ck(Version.new('1.2.*').parts, (1, 2, '*'),   'a trailing wildcard part too');
ck(Version.new('1.a').parts,   (1, 'a'),      'alpha parts stay Strs, numeric stay Ints');

# the shape META6/Test::META walk: no currying, no vacuous pass
ck((Version.new('*').parts[0] eq 'v'),          False, "parts[0] eq 'v' is a plain False");
ck((not any(Version.new('*').parts) eq '*'),    False, 'the asterisk check catches a bare star');
ck((not any(Version.new('1.2').parts) eq '*'),  True,  '…and passes a real version');

# .raku round-trips only what a v-literal can spell; .gist/.Str are unchanged
ck(Version.new('*').raku,     "Version.new('*')", 'bare star cannot follow a bare v');
ck(Version.new('1.2.*').raku, 'v1.2.*',           'digit-led spellings keep the literal form');
ck(Version.new('*').gist,     'v*',               '.gist is unchanged');
ck(Version.new('*').Str,      '*',                '.Str is unchanged');

# wildcard matching still works off the string, either side
ck(Version.new('1.5') ~~ Version.new('1.*'), True, 'wildcard match unaffected');

say $ok ?? 'PASS' !! 'FAIL';
exit($ok ?? 0 !! 1);
