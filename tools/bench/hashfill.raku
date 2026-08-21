# Fill a hash through interpolated string keys, sweep the values, build a
# string. This is the one kernel with a Perl 5 twin (hashfill.pl) — the same
# program, line for line, for a direct perl comparison.
my %h;
for 1 .. 200_000 -> $i {
    %h{"key$i"} = $i * 2;
}
my $sum = 0;
for %h.values -> $v { $sum += $v }
my $s = '';
for 1 .. 50_000 { $s ~= 'x' }
say $sum, ' ', $s.chars;
