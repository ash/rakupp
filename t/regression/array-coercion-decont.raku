# Regression: `.Array` DECONTAINERIZES, and the callers that assumed otherwise.
#
# A hash value (and a scalar's contents) sits in a container, so an Array read
# back out of one is ITEMIZED — `my @a = %h<k>` is one element, which is what
# Rakudo does and what Raku++ now does too. But `.Array` is a coercion: it hands
# back a plain Array, so `my @a = $v.Array` spreads. Raku++ returned the itemized
# value unchanged, so `.Array.elems` said 3 while `my @a = $v.Array` bound 1 —
# the same expression disagreeing with itself depending on where it was used.
#
# Found by running showcase/perl against real perl: its interpreter reads Perl
# arrays out of an environment hash, and three of six examples silently produced
# wrong output (sieve printed no primes at all). Roast never saw it.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my %h;
my @x = 7, 8, 9;
%h{'k'} = @x;
my $v = %h{'k'};

# the container semantics themselves (Rakudo's, which we now match)
check((my @i = %h{'k'}).elems, '1', 'a hash value binds as ONE itemized element');
check((my @j = @x).elems,      '3', 'while the array itself spreads');
check((my @k = $v).elems,      '1', 'and so does a scalar holding it');

# …and the coercions that undo it
check($v.Array.elems,          '3', '.Array reports three');
check((my @a = $v.Array).elems, '3', 'AND binds three — these must agree');
check((my @b = @($v)).elems,   '3', '@() agrees');
check((my @c = $v.list).elems, '3', 'and .list');

# a coercion result is a fresh Array, not the itemized original
check($v.Array.WHAT.gist, '(Array)', '.Array of an Array is an Array');
check((my @d = [1, 2, 3].Array).elems, '3', 'from an array literal too');
check((my @e = (1, 2, 3).Array).elems, '3', 'and from a List');

# nested containers still nest — the decont is one level, not recursive
my %g; %g{'n'} = [[1, 2], [3, 4]];
check((my @f = %g{'n'}.Array).elems, '2', 'two inner arrays');
check(@f[0].elems,                   '2', 'each still holding its own two');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
