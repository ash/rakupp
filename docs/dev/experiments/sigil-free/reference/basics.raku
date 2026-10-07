my $greeting = 'Hello';
my $count = 3;
$count++;
say "{$greeting}, world x{$count}";
my @words = <apple banana cherry>;
@words.push('date');
say @words.elems, ' ', @words[1];
for @words -> $w { print $w.uc, ' ' }
say '';
my %ages = { alice => 30, bob => 25 };
%ages<carol> = 35;
for %ages.keys.sort -> $name { say "{$name} is {%ages{$name}}" }
my &double = -> $n { $n * 2 };
say double(21);
my $total = 0;
$total += $_ for 1..10;
say $total;
