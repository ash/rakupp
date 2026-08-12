# Deterministic corpus generator for the grammar gate: N access-log lines
# (default 2000, ~58 KB — the size GRAMMAR-PLAN's measurements used) from a
# fixed-seed LCG, so every run and every platform gets byte-identical input.
#
#   rakupp tools/grammar/gen-log.raku [lines] > log.txt

my $n = @*ARGS.elems ?? @*ARGS[0].Int !! 2000;
my $seed = 42;
sub rnd($m) {
    $seed = ($seed * 1103515245 + 12345) % 2147483648;
    $seed % $m
}

my @verbs = <GET GET GET POST PUT DELETE>;
my @paths = </index.html /api/items /api/users /static/app.js /favicon.ico /login /search /docs/intro>;

for ^$n {
    my $ip = rnd(224) + 1 ~ '.' ~ rnd(256) ~ '.' ~ rnd(256) ~ '.' ~ rnd(254) + 1;
    my $ts = sprintf('10/Aug/2026:%02d:%02d:%02d +0000', rnd(24), rnd(60), rnd(60));
    my $req = @verbs[rnd(6)] ~ ' ' ~ @paths[rnd(8)] ~ ' HTTP/1.1';
    my $status = (200, 200, 200, 301, 404, 500)[rnd(6)];
    my $size = rnd(50000);
    say $ip ~ ' - - [' ~ $ts ~ '] "' ~ $req ~ '" ' ~ $status ~ ' ' ~ $size;
}
