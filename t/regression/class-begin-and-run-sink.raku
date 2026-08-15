# Regression: two ways a program's own words were thrown away without a sound.
#
# 1. A `BEGIN` block in a CLASS BODY never ran. The class-body parser keeps a
#    whitelist of statement kinds it puts in the body, and a phaser block was
#    not on it — so the block was dropped at parse time, with no error. That is
#    how HTTP::Tinyish::Base declares its whole public API:
#
#        BEGIN { for <get head put post delete> -> $method {
#                    ::?CLASS.^add_method: $method, method (…) {…} } }
#
#    and the class simply had no `get`.
#
# 2. `run(…, :out($fh))` read the handle as a boolean flag. It captured the
#    child's output and dropped it, instead of sending it to the handle — so
#    HTTP::Tinyish::Curl (`run |@cmd, :out($out-fh)`, then slurp that file)
#    fetched every page as an EMPTY BODY while its headers arrived intact.
#
# Both verified against Rakudo. Contract: exit 0 + last line PASS.
my @fail;
sub ok($desc, $got, $want = True) { @fail.push("$desc (got {$got.raku})") unless $got eqv $want }
sub is($desc, $got, $want) { @fail.push("$desc (got {$got.raku}, want {$want.raku})") unless $got eq $want }

# ---- BEGIN in a class body ----
# What the block DID is the observable, not when it ran: a phaser's timing
# relative to the mainline is where a tree-walker and Rakudo legitimately part
# company (Rakudo's BEGIN runs before a mainline `my $x = 0` even exists), so
# nothing here leans on it.
class Base {
    method request($verb, $url) { "$verb $url" }
    BEGIN {
        for <fetch send> -> $m {
            ::?CLASS.^add_method: $m, method ($url) { self.request($m.uc, $url) };
        }
    }
}
class Kid is Base { }
is('a method added in BEGIN',  Base.fetch('/a'), 'FETCH /a');
is('…the second one too',      Base.send('/b'),  'SEND /b');
is('…and it is inherited',     Kid.fetch('/c'),  'FETCH /c');
ok('.^can sees it',            Base.^can('fetch').elems, 1);
is('the class body still works', Base.request('GET', '/d'), 'GET /d');

# a class-body INIT is admitted on the same footing
class D {
    method greet() { 'hi' }
    INIT { ::?CLASS.^add_method: 'shout', method () { self.greet.uc } }
}
is('INIT in a class body', D.shout, 'HI');

# ---- run(:out($fh)) ----
my $tmp = $*TMPDIR.add("rakupp-run-sink-{$*PID}.txt");
{
    my $fh = open $tmp, :w;
    my $proc = run 'echo', 'child-said-this', :out($fh), :err($fh);
    $fh.close;
    ok('the child succeeded', $proc.exitcode, 0);
    is('its output reached the handle', $tmp.slurp, "child-said-this\n");
}
# :err has its own sink, and the two may be different handles
my $errf = $*TMPDIR.add("rakupp-run-sink-err-{$*PID}.txt");
{
    my $o = open $tmp, :w;
    my $e = open $errf, :w;
    run $*EXECUTABLE, '-e', 'note "to-err"; print "to-out"', :out($o), :err($e);
    $o.close; $e.close;
    is('stdout went to its handle', $tmp.slurp, 'to-out');
    ok('stderr went to the other',  $errf.slurp.contains('to-err'));
}
# :out(True) still CAPTURES into the Proc, and :out(False) still discards
is('captured, not redirected', run('echo', 'cap', :out).out.slurp(:close), "cap\n");

unlink $tmp; unlink $errf;

if @fail { note "FAIL: $_" for @fail; say "FAIL" }
else { say "PASS" }
