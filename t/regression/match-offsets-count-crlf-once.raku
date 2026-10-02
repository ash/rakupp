# Regression (issue #102): a match offset counts "\r\n" as ONE character, as
# .chars does. The all-ASCII fast path counted bytes, so a submatch after a CRLF
# pointed one past its text. Contract: exit 0 + last line PASS.
grammar G { token TOP { .*? <w> .* }; token w { 'XX' } }
my $s = "ab\r\nXX";
my @fail;
@fail.push("<w>.from {G.parse($s)<w>.from}") unless G.parse($s)<w>.from == 3;
@fail.push(".to {G.parse($s).to}")           unless G.parse($s).to == 5;
@fail.push("~~ .from")                       unless ($s ~~ /XX/).from == 3;
@fail.push("substr")                         unless $s.substr(G.parse($s)<w>.from, 2) eq 'XX';
@fail.push("accent")                         unless G.parse('ção XX')<w>.from == 4;
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
