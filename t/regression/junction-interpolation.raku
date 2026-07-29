# The junction variant of the quadratic program: `±` returns `($p+$q) | ($p-$q)`
# and the program prints the junction. Interpolating a Junction AUTOTHREADS the
# whole string in Rakudo — rakupp instead concatenated the eigenstates, so
# "Roots: $roots" printed "Roots: 31" where Rakudo says any(Roots: 3, Roots: 1).
# Prefix ~ (an operator, so it threads) had the same gap. (.Str as a METHOD
# autothreads too — method calls on junctions thread in both engines.)
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want }

my $j = 1 | 2;
check("v=$j".gist,  'any(v=1, v=2)',  'interpolating a junction autothreads the string');
check((~$j).gist,   'any(1, 2)',      'prefix ~ threads too');
check(((1 & 2).Str).gist, 'all(1, 2)', '.Str threads as a method call, kind preserved');

my $a = 1 | 2; my $b = 3 | 4;
check("$a$b".gist, 'any(13, 14, 23, 24)', 'two same-kind junctions flatten (interp only)');
my $c = 3 & 4;
check("x$a-$c".gist.substr(0, 4), 'all(', 'mixed kinds: all threads outermost');

# the program end-to-end, matching Rakudo line for line
sub infix:<±>($p, $q) { ($p + $q) | ($p - $q) }
sub prefix:<√>($x) { $x.sqrt }
my (\a, \b, \c) = 1, -4, 3;
if (my $D = b² − 4 × a × c) ≥ 0 {
    my $roots = (−b ± √$D) ÷ (2 × a);
    check("Roots: $roots".gist, 'any(Roots: 3, Roots: 1)', 'the junction of roots interpolates threaded');
    check((3 == $roots).gist, 'any(True, False)',  'and interrogates');
    check((5 == $roots).gist, 'any(False, False)', 'negatively too');
}
else { @fail.push('discriminant branch not taken') }

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
