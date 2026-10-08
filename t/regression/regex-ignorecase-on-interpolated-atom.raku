# `token { :i $x }` — :i and :m apply to an atom whose value is looked up at
# match time: a variable, `$( … )`, a `{ … }` in a double-quoted atom.
#
#   A grammar rule reads such an atom when it is matched, and the engine
#   compared its value byte for byte whatever the adverbs said, so
#   `token { :i $x }` refused "ABC" for $x = 'abc'. (A plain regex splices the
#   value in as text first, so `m:i/$x/` already folded.) The value now goes
#   through the literal matcher, case folds and all (`straße` against
#   "STRASSE"). A backreference stays exact: `m:i/(a) $0/` does not match "aA",
#   in Rakudo as here.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want }

constant NAME = 'abc';
my $x = 'abc';
my $y = 'straße';

grammar GV { token TOP { :i $x } }
check so(GV.parse('ABC')),     True,  ':i $x';
grammar GB { token TOP { :i "{NAME}" } }
check so(GB.parse('ABC')),     True,  ':i "{NAME}"';
grammar GQ { token TOP { :i "x {NAME}" } }
check so(GQ.parse('X ABC')),   True,  ':i "x {NAME}"';
grammar GD { token TOP { :i "x $x" } }
check so(GD.parse('X ABC')),   True,  ':i "x $x"';
grammar GE { token TOP { :i $($x) } }
check so(GE.parse('ABC')),     True,  ':i $( … )';
grammar GF { token TOP { :i $y } }
check so(GF.parse('STRASSE')), True,  ':i folds ß';
grammar GM { token TOP { :m $x } }
check so(GM.parse('äbc')),     True,  ':m $x';
grammar GP { token TOP { $x } }
check so(GP.parse('ABC')),     False, 'no :i, no match';
grammar GX { token TOP { [:i $x] 'd' } }
check so(GX.parse('ABCd')),    True,  'the match goes on after it';
check so(GX.parse('ABCD')),    False, '…and outside the group nothing folds';

# plain regexes, which already folded
check so('ABC' ~~ m:i/$x/),        True, 'm:i/$x/';
check so('ABC' ~~ m:i/"{NAME}"/),  True, 'm:i/"{NAME}"/';
my $s = 'x ABC y'; $s ~~ s:i/"{NAME}"/W/;
check $s, 'x W y', 's:i///';

# backreferences stay exact
check so('aA' ~~ m:i/(a) $0/),           False, 'm:i/(a) $0/';
check so('aA' ~~ m:i/$<x>=(a) $<x>/),    False, 'm:i/$<x>=(a) $<x>/';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
