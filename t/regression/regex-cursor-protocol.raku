# Regression: the regex cursor protocol behind String::Utils' replace/replace-all.
#
# rakupp refused to invoke a Regex value at all. String::Utils walks a
# haystack the way Rakudo's own Str.subst does:
# `Match.^lookup("!cursor_init")(Match, $haystack, :0c)` makes a cursor, the
# Regex is CALLED on it, `nqp::getattr_i($cursor, Match, '$!from'/'$!pos')`
# read the match (pos -3 for none, as MoarVM spells it), and `Match.^lookup("CURSOR_MORE")`
# continues after it.
#
# Every expectation was checked against Rakudo.

use nqp;

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my $rx = / \d+ /;

my constant $cursor-init = Match.^lookup("!cursor_init");
my constant $more        = Match.^lookup("CURSOR_MORE");

my $c := $rx($cursor-init(Match, "ab42cd88", :0c));
ck(nqp::getattr_i($c, Match, '$!from'), 2, 'the first match starts at 2');
ck(nqp::getattr_i($c, Match, '$!pos'),  4, '…and ends at 4');
$c := $more($c);
ck(nqp::getattr_i($c, Match, '$!from'), 6, 'CURSOR_MORE finds the next one');
ck(nqp::getattr_i($c, Match, '$!pos'),  8, '…to the end');
$c := $more($c);
ck(nqp::getattr_i($c, Match, '$!pos'), -3, 'and -3 when there is no more');
my $n := $rx($cursor-init(Match, "abc", :0c));
ck(nqp::getattr_i($n, Match, '$!pos'), -3, 'no match at all is -3 straight away');

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
