# Regression: found by running HTTP::Tiny's t/responses.t.
#   * `$/` scopes to a METHOD activation, not just a sub's — a match inside a
#     method was destroying the caller's `$/`
#   * `last` inside a bare `when` inside a `for` stayed set after the loop
#     consumed the when-flag, escaped the routine, and broke the CALLER's loop
#   * `.slurp` belongs to IO::Path, not to every Str
#   * `.^lookup` answers Mu for a method the type does not have
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- $/ is per-routine, methods included ------------------------------------
class M {
    method inner(@l) { for @l { $_ ~~ /(\w+)/ }; 'done' }
    sub innersub(@l) { for @l { $_ ~~ /(\w+)/ }; 'done' }
    method outer() {
        'abc123' ~~ / $<num> = [\d ** 3] /;
        my $before = ~$<num>;
        self.inner(['x']);
        my $afterMethod = $<num>.defined ?? ~$<num> !! 'GONE';
        'abc123' ~~ / $<num> = [\d ** 3] /;
        innersub(['x']);
        my $afterSub = $<num>.defined ?? ~$<num> !! 'GONE';
        ($before, $afterMethod, $afterSub)
    }
}
ck M.new.outer.List, ('123', '123', '123'), 'a method call leaves the caller\'s $/ alone';

# the real shape: read the match again after a helper method ran
class H {
    method headers(@l) { my %h; for @l { %h{$_} = 1 }; %h }
    method parse(Str $s) {
        $s ~~ / $<status> = [\d ** 3] /;
        { status => +$<status>, extra => self.headers(['a']), ok => $<status>.starts-with('2') }
    }
}
ck H.new.parse('200 OK')<ok>, True, 'and a later $<capture> still reads';

# --- `last` in a `when` inside a `for` stays in its own routine -------------
sub scan(@l) { my $n = 0; for @l { when .not { last }; default { $n++ } }; $n }
class S { method scan(@l) { my $n = 0; for @l { when .not { last }; default { $n++ } }; $n } }
my @seen;
my $i = 0;
while $i < 3 { $i++; @seen.push: scan(['a', '', 'b']) }
ck @seen.List, (1, 1, 1), 'a `when { last }` in a sub does not break the caller\'s loop';
my @seen2;
my $j = 0;
repeat while $j < 3 { $j++; @seen2.push: S.new.scan(['a', '', 'b']) }
ck @seen2.List, (1, 1, 1), 'nor in a method, nor a repeat loop';
ck scan(['a', 'b']), 2, 'and the `last` still works where it belongs';

# a `when { return }` must still count as MATCHED, or a CATCH rethrows
class C { method m() { CATCH { when X::AdHoc { return 'CAUGHT' } }; die 'boom' } }
ck C.new.m, 'CAUGHT', 'a `when { return }` inside CATCH still handles';

# --- .slurp is IO::Path's, and .^lookup says so -----------------------------
ck ('some string'.^lookup('slurp')).defined, False, '.^lookup is Mu for a method Str lacks';
ck ('some string'.^lookup('uc')).defined, True,  'and a real one is still found';
ck (try 'nonexistent-path'.slurp).defined, False, 'a Str is not a path';
ck $*TMPDIR.IO.child('x').Str.chars > 0, True, 'IO::Path is untouched';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
