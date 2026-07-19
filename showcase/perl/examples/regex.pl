# Regex is Perl's signature: match, capture, substitute, split — run by a small
# backtracking engine inside the interpreter (rakupp can't build regexes at runtime).
my @lines = (
    "Alice: 30",
    "Bob:   25",
    "Carol: 41",
);
my $total = 0;
my $count = 0;
for my $line (@lines) {
    if ($line =~ /(\w+):\s*(\d+)/) {
        my ($name, $age) = ($1, $2);
        printf "%-6s is %d\n", $name, $age;
        $total += $age;
        $count++;
    }
}
printf "average age: %.1f\n", $total / $count;

# substitution with a captured backreference in the replacement
my $date = "2023-01-15";
$date =~ s/(\d+)-(\d+)-(\d+)/$3\/$2\/$1/;
print "reformatted: $date\n";

# global match in list context pulls out every number
my $text = "room 7, floor 3, building 42";
my @nums = $text =~ /(\d+)/g;
print "numbers: @nums\n";
