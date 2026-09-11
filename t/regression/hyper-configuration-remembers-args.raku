# Regression: `.hyper(:batch, :degree)` — `.configuration` answers what was asked.
#
# rakupp's HyperSeq stand-in is serial: `.hyper` on a list is the list. That
# is fine for the work, but `.configuration.batch` / `.degree` answered the
# pool defaults whatever the call said, and hyperize's suite reads them
# straight back (`@a.&hyperize(42).configuration.batch` wants 42). The type
# object's defaults are unchanged.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @a;
my $c := @a.hyper(:batch(42), :degree(16)).configuration;
ck($c.batch, 42,  'batch as asked');
ck($c.degree, 16, 'degree as asked');
my $r := @a.race(:batch(12), :degree(3)).configuration;
ck($r.batch, 12, 'race: batch');
ck($r.degree, 3, 'race: degree');
my $d := Iterable.hyper.configuration;
ck($d.batch, 64, 'the type object still answers the default batch');
ck($d.degree > 0, True, 'and a positive default degree');
my @b = 1, 2, 3;
ck(@b.hyper(:batch(2)).map(* * 2).list, (2, 4, 6), 'the work still happens on the list');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
