sub worker(Any $a, Int $b) {}
my $deaths = 0;
for ^200 {
    my $value = Any;
    my @workers = (^4).map: { start { worker($value) } };
    try {
        await @workers;
        CATCH { default { $deaths++ } }
    }
}
say "deaths: $deaths";
