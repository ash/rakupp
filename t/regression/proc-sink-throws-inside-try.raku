# Regression: `.sink` on a Proc that exited unsuccessfully throws
# X::Proc::Unsuccessful THERE, inside whatever `try` holds the call, as
# Rakudo's Proc.sink does. The method handed the Proc back instead, and the
# statement sank it after the `try` was over: `try $p.sink` died with the
# very exception it was written to catch.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $p = run 'false';
try $p.sink;
ck $!.^name, 'X::Proc::Unsuccessful', 'try $p.sink catches a failed run';
$! = Nil;
try { $p.sink };
ck $!.^name, 'X::Proc::Unsuccessful', '…and so does a try block around it';
my $caught = '';
{ $p.sink; CATCH { default { $caught = .^name } } }
ck $caught, 'X::Proc::Unsuccessful', '…and a CATCH in the block';
ck (try { $p.sink; 'lived' }), Nil, 'the .sink itself throws, before the next statement';
my $ok = run 'true';
ck $ok.sink, Nil, '.sink on a successful run is Nil';
ck $p.exitcode, 1, 'the Proc is still there to ask';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
