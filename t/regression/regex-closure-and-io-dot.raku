# Regression: found by running IO::Glob's own test suite.
#   * a regex value closes over the scope that built it, so an interpolated
#     variable still resolves after that scope is gone
#   * `$^name` inside a regex literal is a placeholder of the enclosing block
#   * a bare `.` parent contributes nothing to a path
#   * `.dir`'s default :test is what excludes `.`/`..`; an explicit one replaces it
#   * `*` is a Whatever, not a wildcard for the type checker
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- a regex closes over its creating scope --------------------------------
sub mk($a) { rx/$a/ }
ck ('foo' ~~ mk('foo')).defined, True, 'an interpolated var outlives the sub that built the regex';
ck ('bar' ~~ mk('foo')).defined, False, 'and it is still the right value';

my @built = <foo bar>.map(-> $alt { rx/$alt/ });
ck ('foo' ~~ @built[0]).defined, True, 'a regex built in a map block, matched outside';
ck ('bar' ~~ @built[1]).defined, True, 'each closure keeps its own binding';

# a variable that CHANGES is still re-read at match time (the regex is a
# closure over the container, not a snapshot of its value)
my $w = 'a'; my $r = rx/$w/; $w = 'b';
ck ('b' ~~ $r).defined, True, 'the current value wins';
ck ('a' ~~ $r).defined, False, 'not the one it was built with';

# --- $^name inside a regex literal is a placeholder ------------------------
my @ph = <foo bar>.map({ rx/$^alt/ });
ck ('foo' ~~ @ph[0]).defined, True, '$^name in a regex declares the block parameter';
ck ('bar' ~~ @ph[1]).defined, True, 'and binds per call';

my $base = rx/<?>/;
my @combined = <foo bar>.map({ rx/$base$^alt/ });
ck ('foo' ~~ @combined[0]).defined, True, 'alongside an interpolated regex';

# --- `.` in a path ---------------------------------------------------------
ck '.'.IO.child('t').Str, 't', 'a bare `.` parent contributes nothing';
ck '.'.IO.child('t').child('u').Str, 't/u', 'and only the leading one';
ck './x'.IO.child('y').Str, './x/y', '`./x` is not the same as `.`';
ck 'a'.IO.child('b').Str, 'a/b', 'an ordinary parent is unchanged';

# --- .dir and its :test ----------------------------------------------------
my $tmp = $*TMPDIR.child("rakupp-dirtest-{$*PID}");
$tmp.mkdir;
$tmp.child('one').spurt('1');
$tmp.child('two').spurt('2');
ck $tmp.dir.map(*.basename).sort.List, ('one','two'), '.dir excludes . and .. by default';
ck $tmp.dir(test => *).map(*.basename).sort.List, ('.','..','one','two'),
   'an explicit :test replaces that filter';
ck $tmp.dir.map(*.Str).sort.List, ($tmp.child('one').Str, $tmp.child('two').Str),
   'and the entries carry the directory';
$tmp.child('one').unlink; $tmp.child('two').unlink; $tmp.rmdir;

# --- `*` is a Whatever, not a wildcard type --------------------------------
multi sub which( Str:D $p ) { 'str' }
multi sub which( Whatever $ ) { 'whatever' }
ck which('x'), 'str', 'a Str picks the Str candidate';
ck which(*), 'whatever', 'and `*` picks the Whatever one';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
