# From the Cognates port (docs/rakupp-findings finding 8): string interpolation
# treated every non-ASCII BYTE as an identifier character, so `"$v…"` swallowed the
# ellipsis into the name and printed its own source text, while `"$v🙂"` reported
# an undeclared variable. Raku names DO take Unicode letters, so the rule is
# ID_Continue, not "stop at non-ASCII".
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want }

my $v = 'X';
check("$v.",    'X.',   'a full stop ends the name');
check("$v y",   'X y',  'a space does');
check("…$v",    '…X',   'a leading ellipsis was always fine');
check("$v…",    'X…',   'a TRAILING ellipsis (U+2026 Po) is not part of the name');
check("«$v»",   '«X»',  'guillemets (Pi/Pf) are not');
check("$v–",    'X–',   'an en dash (U+2013 Pd) is not');
check("$v🙂",   'X🙂',  'an emoji (So) is not');
check("$v€",    'X€',   'a currency sign (Sc) is not');

# …but a Unicode LETTER really does continue an identifier, in both engines.
my $vкнига = 'U';
check("$vкнига", 'U', 'a Cyrillic continuation is ONE name');
my $naïve = 'N';
check("$naïve",  'N', 'a Latin-1 letter with a diacritic too');

# The ASCII rules are unchanged.
my $a-b = 'H';
check("$a-b!",  'H!',  'a hyphenated name still lexes');
my @list = 1, 2;
check("@list[0]…", '1…', 'an @-sigil subscript then an ellipsis');
my %h = k => 'V';
check("%h<k>…",    'V…', 'a %-sigil subscript then an ellipsis');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
