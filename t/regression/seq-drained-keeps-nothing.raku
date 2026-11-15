# Regression (issue #263): a Seq drained by the one thing that holds it keeps
# nothing of what it handed out, as Rakudo's single-pass Seq keeps nothing.
# Every value pulled through `for gather {…}`, `for Seq.new($iterator)` or
# `Seq.new($iterator).iterator` stayed in the Seq's buffer until the Seq died,
# about 280 bytes a Hash: a million of them cost 280 MB where the same
# iterator drained directly cost nothing. A Seq someone else still holds (a
# variable, an Array) keeps every value, so it can be read again.
#
# Runs clean under Rakudo too.
use Telemetry;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}
my class It does Iterator {
    has Int $!i = 0;
    has Int $.n;
    method pull-one() {
        return IterationEnd if $!i >= $!n;
        my %h = a => $!i++;
        %h
    }
}
my $n = 1_500_000;
my $want = $n * ($n - 1) div 2;
# what a drain may add to the peak: the old buffer held about 400 MB here, and
# Rakudo's own heap grows about 100 MB on the first
sub drained($desc, &drain) {
    my $before = T<max-rss>;   # (KiB)
    ck drain(), $want, $desc;
    my $grew = (T<max-rss> - $before) div 1024;
    if $grew < 180 { say "ok - …and keeps what it drained ($grew MiB)" }
    else { $fails++; say "FAIL: $desc grew the peak by $grew MiB" }
}
drained 'for Seq.new($iterator)', { my $s = 0; for Seq.new(It.new(:$n)) -> %h { $s += %h<a> }; $s };
drained 'Seq.new($iterator).iterator', {
    my $s = 0; my $it = Seq.new(It.new(:$n)).iterator;
    loop { my $v := $it.pull-one; last if $v =:= IterationEnd; $s += $v<a> }; $s };
drained 'for gather', { my $s = 0; for gather { for ^$n { my %h = a => $_; take %h } } -> %h { $s += %h<a> }; $s };
drained 'a statement-modifier for', { my $s = 0; $s += $_<a> for gather { for ^$n { my %h = a => $_; take %h } }; $s };
drained 'gather {…}.map(…).grep(…)', {
    my $s = 0; for (gather { for ^$n { my %h = a => $_; take %h } }).map(*<a>).grep(*.defined) { $s += $_ }; $s };

# …while a Seq that is still held keeps all of it
my $m = 3000;
my $g = gather { take $_ for ^$m };
my $c = 0; for @$g { $c += $_ };
ck ($c, $g.elems, $g[0], $g[$m - 1]), ($m * ($m - 1) div 2, $m, 0, $m - 1), 'a Seq in a variable is read again';
my $v = (gather { take $_ for ^$m }).map(* * 2);
$c = 0; for @$v { $c += $_ };
ck ($c, $v.elems, $v[1]), ($m * ($m - 1), $m, 2), '…and so is a map of one';
my @a = gather { take $_ for ^$m };
$c = 0; for @a { $c++ };
ck ($c, @a.elems, @a[$m - 1]), ($m, $m, $m - 1), '…and an Array';
sub f(@x) { my $s = 0; for @x { $s += $_ }; $s + @x.elems }
ck f(gather { take $_ for ^$m }), $m * ($m - 1) div 2 + $m, '…and a parameter';
my @r = do for gather { take $_ for ^$m } { $_ * 2 };
ck (@r.elems, @r[$m - 1]), ($m, 2 * ($m - 1)), 'what the loop answers is all there';
my $p = 0; for gather { take $_ for ^$m } -> $x, $y { $p += $y - $x };
ck $p, $m div 2, 'two at a time';
my $last; for gather { take $_ for ^$m } { LAST $last = $_ };
ck $last, $m - 1, 'a LAST phaser sees the last one';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
