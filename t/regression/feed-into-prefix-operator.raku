# Regression: a feed whose TARGET is a prefix operator — `5 ==> §2`.
#
# A feed target is a call, and the fed value joins its argument list at the
# end: `5 ==> f(2)` is `f(2, 5)`. The feed had an arm for a Call and an arm for
# a bare name, and a user-defined PREFIX parses as neither — it is a Unary — so
# it fell through to the container arm and died "Target is not assignable",
# the same family of misfiling as the bare-name case before it.
#
# A user-defined POSTFIX never had the problem: it parses as a Call named
# `postfix:<!>`, so it always took the Call arm. It is checked here anyway,
# because the two spellings must stay one rule.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub prefix:<§>(*@a)  { '§' ~ @a.join(',') }
sub postfix:<!>(*@a) { '!' ~ @a.join(',') }
sub prefix:<¤>($a, $b) { "$a/$b" }          # the fed value fills a real parameter

ck (5 ==> §2),          '§2,5',   'a feed into a prefix operator appends the fed value';
ck (§2 <== 5),          '§2,5',   '…and the backward feed is the same call';
ck (5 ==> ¤2),          '2/5',    '…landing in a declared parameter, not just a slurpy';
ck ((1, 2) ==> §9),     '§9,1,2', '…and a fed LIST spreads into the slurpy';
ck (5 ==> 2!),          '!2,5',   'a postfix target follows the same rule';
ck (5 ==> §2 ==> §3),   '§3,§2,5', 'feeds chain: each stage is the next one\'s last argument';

# Every BUILT-IN prefix is unary, so the fed value is one positional too many.
# That is the complaint to make — the container arm's "Target is not
# assignable" said the target was the wrong KIND of thing, which it is not.
my $msg = do { my $m = ''; try { my $x = (4 ==> -2); CATCH { default { $m = .message } } }; $m };
ck ($msg.contains('positionals')), True, 'a built-in prefix target is an arity error, not an assignment one';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
