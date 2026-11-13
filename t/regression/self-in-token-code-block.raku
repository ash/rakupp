# `self` in a standalone token's code block is the cursor it was entered with,
# as in Rakudo (rakudo#3410): it died "'self' used where no object is
# available" in an anonymous token and in a `my token`. `self.pos` is where
# the token began; for a token matched directly with `~~`, where the scan began.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my @seen;
my $a = token { 'a' <?{ @seen.push(self.pos); True }> 'a' };
check ('xxxxxaa' ~~ $a).so, 'True', 'an anonymous token (rakudo#3410)';
check @seen.join(','), '0', '…self.pos is where the scan began';

@seen = ();
my token b { 'b' <?{ @seen.push(self.pos); True }> 'b' }
check ('xxxxxbb' ~~ /<b>/).so, 'True', 'a my token called as a subrule';
check @seen.join(','), '5', '…self.pos is where it was called';

@seen = ();
my $r = regex { 'r' { @seen.push(self.^name) } };
'r' ~~ $r;
check @seen.join(','), 'Match', 'a plain block: self is a Match';

@seen = ();
my token o { 'o' { @seen.push(self.orig) } }
'xo' ~~ &o;
check @seen.join(','), 'xo', 'its .orig';

class C { method m { 'ab' ~~ / a <?{ @seen.push(self.^name); True }> b / } }
@seen = ();
C.m;
check @seen.join(','), 'C', 'inside a method, self is still the invocant';

grammar G { token TOP { 'x' <t> }; token t { 'a' <?{ @seen.push(self.pos); True }> 'a' } }
@seen = ();
G.parse('xaa');
check @seen.join(','), '1', 'a grammar token, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
