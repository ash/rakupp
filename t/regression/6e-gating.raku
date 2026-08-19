# Regression: the 6.e additions exist only for code that asked for 6.e.
#
# Every pair below is the same program run under both revisions. Under 6.d the
# routine, type or syntax is not there at all — as in Rakudo, where these live
# in CORE.e and a 6.d unit never loads it. Phase 2 of docs/dev/plans/6E-PLAN.md.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}
sub like-check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want /$want/") unless $got.contains($want)
}

# chomp, not trim: a Format pads on the LEFT, and trimming the answer would
# quietly turn "  foo" into the thing we are not testing.
sub both(Str $code) {
    my $d = run($*EXECUTABLE, '-e', $code, :out, :err);
    my $e = run($*EXECUTABLE, '-e', "use v6.e.PREVIEW; $code", :out, :err);
    ( ($d.out.slurp(:close) ~ $d.err.slurp(:close)).chomp,
      ($e.out.slurp(:close) ~ $e.err.slurp(:close)).chomp )
}

my ($d, $e);

($d, $e) = both 'say (1,2,3,4,5).snip(* < 3)';
like-check $d, 'No such method',  '.snip is absent under 6.d';
check      $e, '((1 2) (3 4 5))', '.snip works under 6.e';

($d, $e) = both 'say snip(* < 3, 1,2,3,4)';
like-check $d, 'Undefined routine', 'the snip sub is absent under 6.d';
check      $e, '((1 2) (3 4))',     'the snip sub works under 6.e';

($d, $e) = both 'say rotor(2, 1..6)';
like-check $d, 'Undefined routine', 'the rotor sub is absent under 6.d';
check      $e, '((1 2) (3 4) (5 6))', 'the rotor sub works under 6.e';

($d, $e) = both 'my $x = (1,2).snitch; say "kept"';
like-check $d, 'No such method', '.snitch is absent under 6.d';
like-check $e, 'kept',           '.snitch works under 6.e';

($d, $e) = both 'my $f := q:o/%5s/; say $f("foo")';
like-check $d, 'Unrecognized adverb', 'a Format literal does not parse under 6.d';
check      $e, '  foo',               'a Format literal works under 6.e';

($d, $e) = both 'say Format.new("%5s")("hi")';
like-check $d, 'Undeclared name', 'the Format type is absent under 6.d';
check      $e, '   hi',           'the Format type works under 6.e';

($d, $e) = both 'say unlink("/tmp/no-such-file-abc123").raku';
check $d, '["/tmp/no-such-file-abc123"]', '6.d unlink answers with the paths it removed';
check $e, 'Bool::True',                   '6.e unlink takes one path and answers one Bool';

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
