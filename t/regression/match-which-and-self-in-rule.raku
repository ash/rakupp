# Regression (issue #109, rows I and J): two Matches of the same text are two
# objects — distinct .WHICH, two keys of an object hash, two Set elements — and
# `self` in a rule's code is the cursor the rule was called with.
# Contract: exit 0 + last line PASS.
grammar GI { token TOP { <a> <a> }; token a { 'x' } }
my $m = GI.parse('xx');
my @fail;
@fail.push('WHICH') if $m<a>[0].WHICH eq $m<a>[1].WHICH;
my $copy = $m<a>[0];
@fail.push('copy WHICH') unless $copy.WHICH eq $m<a>[0].WHICH;
my %h{Any}; %h{$_} = 1 for $m<a>.list;
@fail.push('object hash') unless %h.elems == 2;
@fail.push('Set') unless set($m<a>.list).elems == 2;
my @seen;
grammar GJ { token TOP { :my $o = self.orig; { @seen.push($o) } <x> { @seen.push(self.pos) } .+ }; token x { a } }
@fail.push('self in rule') unless GJ.parse('abc') && @seen eqv ['abc', 0];
.say for @fail;
say @fail ?? 'FAIL' !! 'PASS';
