# Issue #180: a regex holding a code block (`<?{ … }>`, `{ … }`) runs its
# blocks in the scope it was written in (#147), but the finished match was
# published there too. A `~~` in another routine or module answered True and
# left its own `$/` (and `$0`, `$<name>`) untouched, while the `$/` of the
# regex's home was overwritten. `.match` was right, so only code reading `$/`
# after a smartmatch went silently wrong. An anonymous `regex { … }` value
# (wired before #147) had the same fault. The match is now published in the
# scope that did the matching.
#
# Contract: exit 0 + last line PASS. Every case is oracle-checked against
# Rakudo 2026.09 — run this file under `rakudo` too.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# the issue's shape: the regex made in one module, matched in a method of a
# class in another
my $dir = $*TMPDIR.add("issue180-{$*PID}");
$dir.mkdir;
$dir.add('MkRx180.rakumod').spurt: q:to/END/;
    unit module MkRx180;
    our sub four(Str $s --> Bool) { $s.chars == 4 }
    sub plain-rx() is export { / \d+ / }
    sub block-rx() is export { / \d+ <?{ True }> / }
    sub call-rx()  is export { / \d+ <?{ MkRx180::four(~$/) }> / }
    END
$dir.add('UseRx180.rakumod').spurt: q:to/END/;
    unit class UseRx180;
    has $.rx;
    method find(Str:D $line) { $line ~~ $!rx ?? ~$/ !! 'NONE' }
    method find-match(Str:D $line) { with $line.match($!rx) { ~$_ } else { 'NONE' } }
    END
my $code = q:to/END/;
    use MkRx180; use UseRx180;
    for plain-rx(), block-rx(), call-rx() -> $rx {
        my $u = UseRx180.new(:$rx);
        print $u.find("id 1234 x"), '/', $u.find-match("id 1234 x"), ' ';
    }
    END
my $p = run($*EXECUTABLE, '-I', ~$dir, '-e', $code, :out, :err);
my $out = $p.out.slurp(:close).trim;
my $err = $p.err.slurp(:close);
check $out, '1234/1234 1234/1234 1234/1234',
    "the issue: two modules, `~~` then \$/{$err ?? " ($err.lines.head())" !! ''}";
for $dir.dir { .d ?? run('rm', '-rf', ~$_) !! .unlink }
$dir.rmdir;

# one file, a routine between where the regex is written and where it is matched
sub mk-assert() { / \d+ <?{ True }> / }
sub mk-block()  { / (\d+) { } / }
sub mk-anon()   { regex { \d+ <?{ True }> } }
sub mk-named()  { / $<n>=(\d+) <?{ $<n>.chars > 1 }> / }
sub slash($rx)  { "id 1234 x" ~~ $rx; $/ ?? ~$/ !! 'NONE' }
check slash(mk-assert()), '1234', 'a code assertion: the matcher’s $/';
check slash(mk-block()),  '1234', 'a code block: the matcher’s $/';
check slash(mk-anon()),   '1234', 'an anonymous regex {}: the matcher’s $/';
sub pos0($rx) { "id 1234 x" ~~ $rx; $0 ?? ~$0 !! 'NONE' }
check pos0(mk-block()), '1234', '…and its $0';
sub named($rx) { "id 1234 x" ~~ $rx; "{$<n> // 'NONE'} {$/<n> // 'NONE'}" }
check named(mk-named()), '1234 1234', '…and its $<n>, which the assertion read';
my $seen;
sub in-if($rx) { if "id 1234 x" ~~ $rx { $seen = ~$/ } }
in-if(mk-assert());
check $seen, '1234', '$/ inside the `if` block the match opened';
sub failed($rx) { "abc" ~~ /b/; "id 1 x" ~~ $rx; $/.defined }
check failed(mk-named()), False, 'a failed match leaves the matcher’s $/ undefined, not stale';

# the regex's home keeps its own $/
my &home;
sub mk-home() { "zz" ~~ /zz/; &home = -> { ~$/ }; / \d+ <?{ True }> / }
slash(mk-home());
check home(), 'zz', 'the scope the regex was written in keeps its $/';

# blocks still run in the regex's home
sub mk-count() { my $n = 0; (/ a { $n++ } /, -> { $n }) }
my ($rc, $count) = mk-count();
sub run-it($rx) { 'a' ~~ $rx; ~$/ }
check run-it($rc) ~ run-it($rc), 'aa', 'a counting block’s regex matched twice';
check $count(), 2, '…and its block wrote the scope it closed over';
sub mk-my() { / :my $c = 0; [ \d { $c++ } ]+ <?{ $c == 3 }> / }
sub three($rx) { "ab 123 cd" ~~ $rx; $/ ?? ~$/ !! 'NONE' }
check three(mk-my()), '123', 'a `:my` variable in the match scope';

.note for @fail;
say @fail ?? "FAIL ({+@fail})" !! 'PASS';
exit @fail ?? 1 !! 0;
