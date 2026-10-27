# Regression: `samewith` in a routine that is not a multi died with
# "samewith is not in the dynamic scope of a dispatcher".
#
# Only a multi's dispatch pushed a frame that samewith could restart, but
# Rakudo's samewith calls the enclosing routine again with the new arguments
# when it is an only (control.pm6: `$caller(|c)`). Timezones::ZoneInfo's
# `sub timezone-data` resolves a link that way, from inside an `orwith` block:
#     } orwith $links{$olson-id} { return %cache{$olson-id} := samewith $_ }

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

sub count-down($x) { $x > 0 ?? samewith($x - 1) !! 'done' }
ok count-down(3) eq 'done', 'an only sub calls itself again';

sub link(Str() $id) {
    if $id eq 'a' { return 'found a' }
    orwith %(b => 'a'){$id} { return samewith $_ }
    else { 'none' }
}
ok link('b') eq 'found a', 'from inside an orwith block (the ZoneInfo shape)';

sub acc($x, $y = 0) { $x > 0 ?? samewith($x - 1, $y + 1) !! $y }
ok acc(4) == 4, 'new arguments, optional ones included';

sub named(:$n = 3) { $n ?? samewith(:n($n - 1)) !! 'z' }
ok named() eq 'z', 'named arguments';

sub in-map($n) { (1..$n).map({ $_ == 1 ?? 1 !! samewith($_ - 1) }).sum }
ok in-map(3) == 4, 'from a block a built-in runs';

# the routine it is WRITTEN in, not whichever one is running the block
sub apply(&f, $v) { f($v) }
sub via-apply($x) { $x > 0 ?? apply(-> $y { samewith($y - 1) }, $x) !! 'lexical done' }
ok via-apply(3) eq 'lexical done', 'from a block another user routine runs';

# a multi still restarts its dispatch
multi m(Int $x) { $x > 0 ?? samewith($x - 1) !! 'int done' }
multi m(Str $s) { samewith($s.chars) }
ok m('abc') eq 'int done', 'a multi re-dispatches over every candidate';

class C { method m($x) { $x > 0 ?? samewith(self, $x - 1) !! 'method done' } }
ok C.m(2) eq 'method done', 'an only method, given its invocant';

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
