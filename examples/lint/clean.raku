# A program that lints clean — `rakupp --lint clean.raku` reports no issues.
# It deliberately exercises idioms a naive linter would false-positive on:
# string interpolation, closures capturing outer variables, `&`-sigil
# callables, loop topic variables, do-blocks, and variables that are only ever
# referenced inside a regex.

sub fib($n) {
    my @f = 0, 1;
    @f.push(@f[*-1] + @f[*-2]) while @f < $n;
    @f;
}

my $count   = 10;
my @numbers = fib($count);
say "first $count Fibonacci numbers: @numbers[]";

my $running = 0;
my &accumulate = -> $x { $running += $x };   # closure over $running
accumulate($_) for @numbers;
say "sum = $running";

for @numbers.kv -> $i, $n {
    say "  [$i] $n" if $n %% 2;
}

my $label = do {
    my $big = @numbers[*-1];
    $big > 20 ?? "grows fast" !! "stays small";
};
say $label;

# $suffix is used *only* inside the regex — the linter scans pattern text.
my $suffix = "fast";
say $label ~~ / $suffix $ / ?? "ends in $suffix" !! "does not";
