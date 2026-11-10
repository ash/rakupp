# An `exit` in a regex's code block ends the program: the block ran inside
# a catch of everything, kept for errors that are not Raku's, and `exit` is
# raised as one of those — so `"abc" ~~ / a { exit 3 } b /; say "still
# running"` said still running and ended with 0, in a `{ }` block, a
# `<?{ }>` assertion, a `<{ }>` pattern and a grammar rule alike. Found by
# the review of src/ (docs/dev/findings/src-review-2026-11-09.md, A3).
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub ran(Str $code) {
    my $p = run $*EXECUTABLE, '-e', $code, :out, :err;
    my $out = $p.out.slurp(:close); $p.err.slurp(:close);
    $p.exitcode ~ ' ' ~ $out.trim;
}

ck ran(Q[print "in "; "abc" ~~ / a { exit 3 } b /; print "after"]), '3 in', 'a { } block';
ck ran(Q["abc" ~~ / a <?{ exit 4 }> b /; print "after"]), '4 ', 'a <?{ }> assertion';
ck ran(Q[grammar G { token TOP { a { exit 5 } b } }; G.parse("ab"); print "after"]), '5 ', 'a grammar rule';
ck ran(Q["abc" ~~ / a <{ exit 6 }> b /; print "after"]), '6 ', 'a <{ }> pattern';
ck ran(Q["abc" ~~ / a { 1 } b /; print "after"]), '0 after', 'a block that does not exit, as before';

sub f { "abc" ~~ / a { return 7 } b /; 9 }
ck f(), 7, 'a return in a block, as before';
ck ~("abc" ~~ / a <?{ True }> b /), 'ab', '…and an assertion';

say 'PASS';
