# Regression (issue #263, the producers that count by position): a `for`
# over `1, 2 ... *`, `1 xx *`, `Seq.from-loop`, `.kv`, `.pairs`, `.keys`,
# `.skip`, `.flat`, `.rotor`, `.batch`, `Z` or `X` that ends with `last`
# keeps nothing of what it read. These index their own buffer by its size,
# or step from its last elements (a sequence's generator), so they kept
# every value: 2 million Ints held about 70 MB. Each now counts what has
# gone (LazySeqState::dropped) and keeps the elements it steps from
# (keepTail). A Seq that something still holds keeps every value.
#
# Runs clean under Rakudo too.
use Telemetry;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my $n = 2_000_000;
# (unmeasured: Rakudo's own heap grows by about 100 MB on its first long loop)
{ my $s = 0; for gather { for ^1_000_000 { my %h = a => $_; take %h } } -> %h { $s += %h<a> } }
sub held($desc, &walk, $want, $mib = 40) {
    my $before = T<max-rss>;   # (KiB)
    ck walk(), $want, $desc;
    my $grew = (T<max-rss> - $before) div 1024;
    if $grew < $mib { say "ok - …and keeps what it read ($grew MiB)" }
    else { $fails++; say "FAIL: $desc grew the peak by $grew MiB" }
}
held '1, 2 ... *', { my $c = 0; for 1, 2 ... * { last if $_ > $n; $c++ }; $c }, $n;
held 'a closure generator', { my $c = 0; for 1, 1, (* + *) % 1000 ... * { last if ++$c >= $n }; $c }, $n;
held '1 xx *', { my $c = 0; for 1 xx * { last if ++$c >= $n }; $c }, $n;
held 'Seq.from-loop', { my $i = 0; for Seq.from-loop({ $i++ }) { last if $_ >= $n }; $i }, $n + 1;
# (Rakudo's own .kv and .pairs take 50 to 110 MiB here)
held '.kv', { my $c = 0; for (1 ... *).kv -> $k, $v { last if $k >= $n; $c++ }; $c }, $n, 200;
held '.pairs', { my $c = 0; for (1 ... *).pairs { last if .key >= $n; $c++ }; $c }, $n, 200;
held '.skip', { my $c = 0; for (1 ... *).skip(3) { last if $_ > $n; $c++ }; $c }, $n - 3;
held '.rotor', { my $c = 0; for (1 ... *).rotor(3 => -1) { last if .[0] > $n; $c++ }; $c }, $n div 2;
held '.batch over a gather', { my $c = 0; for (gather { take $_ for ^$n }).batch(2) { $c++ }; $c }, $n div 2;
held 'Z', { my $c = 0; for (1 ... *) Z (1 ... *) { last if .[0] > $n; $c++ }; $c }, $n;
held 'X', { my $c = 0; for (1 ... *) X 1 { last if .[0] > $n; $c++ }; $c }, $n;

# …what they hand out is what they handed out before
sub nth(&s, $k) { my $c = 0; my $v; for s() { $v = $_; last if ++$c >= $k }; $v }
my $k = 5000;
ck nth({ 1, 1, * + * ... * }, $k).chars, 1045, 'a generator of two';
ck nth({ 1, 2, { [+] @_ } ... * }, 30).chars, 9, '…a slurpy one';
ck nth({ 'a' ... * }, $k), 'gjh', '…a succ walk';
ck nth({ (1 ... *).kv }, 2 * $k), 5000, '.kv';
ck nth({ (1 ... *).antipairs }, $k), 5000 => 4999, '.antipairs';
ck nth({ (1 ... *).keys }, $k), 4999, '.keys';
ck nth({ (1 ... *).rotor(3 => -1) }, $k).List, (9999, 10000, 10001), '.rotor';
ck nth({ (gather { take $_ for ^20000 }).rotor(4, :partial) }, $k).List, (19996, 19997, 19998, 19999), '.rotor(:partial)';
ck nth({ ((1 ... *).map({ ($_, $_) })).flat }, $k), 2500, '.flat';
ck nth({ (1 ... *) Z (1, 3 ... *) }, $k).List, (5000, 9999), 'Z';
ck nth({ (1 ... *) X <a b> }, $k).List, (2500, "b"), 'X';
my $h = (1 ... *).kv; my $c = 0; for @$h { last if ++$c > 2 * $k };
ck ($h[0..3], $h[2 * $k - 1]), ((0, 1, 1, 2), 5000), 'a Seq a variable holds reads again';
my $g = (gather { take $_ for ^20000 }).batch(3); $c = 0; for @$g { last if ++$c > $k };
ck ($g[0], $g.elems), ((0, 1, 2), 6667), '…and so does a batch of one';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
