# Regression: three errors that have TWO positions, or none, reported neither
# (issue #67, the second slice).
#
#   warn   printed its message with no indication of where it came from.
#   fail   reported only where the Failure DETONATED — the line that used the
#          value — never where it was made, which is where the error is.
#   await  reported the awaiting line, never the worker frames; the chain was
#          captured on the worker and then dropped when the Promise stored the
#          exception.
#
# Contract: exit 0 + last line PASS.
my @fail;

my $dir = $*TMPDIR.add("rakupp-btp3-{$*PID}");
$dir.mkdir;

sub runit($body, *@args) {
    my $f = $dir.add("case-{$++}.raku");
    $f.spurt($body);
    my $p = run($*EXECUTABLE.absolute, |@args, $f.absolute, :out, :err);
    my $o = $p.out.slurp(:close); my $e = $p.err.slurp(:close);
    $f.unlink;
    ($o, $e)
}

# --- warn: the message, then the ONE frame it was warned from ---------------
my ($o1, $e1) = runit(q:to/END/);
sub deep { warn "careful" }
sub mid  { deep() }
mid();
say "after";
END
@fail.push("warn keeps its message first, got:\n$e1") unless $e1.lines[0] eq 'careful';
@fail.push('warn names where it warned') unless $e1 ~~ /'in sub deep at ' .*? 'line 1'/;
# …one frame, not the whole chain: a warning points at a line, it is not an incident
@fail.push("warn shows ONE frame, got {$e1.lines.elems} lines") unless $e1.lines.elems == 2;
@fail.push('a warning does not stop the program') unless $o1.trim eq 'after';

# --- fail: BOTH positions, creation first --------------------------------
my ($o2, $e2) = runit(q:to/END/);
sub inner($x) { fail "bad $x" }
sub outer($x) { inner($x) }
my $r = outer(5);
say "before use";
say "got: $r";
END
@fail.push("fail keeps its message first, got:\n$e2") unless $e2.lines[0] eq 'bad 5';
@fail.push('fail names where it was MADE') unless $e2 ~~ /'in sub inner at ' .*? 'line 1'/;
@fail.push('…through its caller')          unless $e2 ~~ /'in sub outer at ' .*? 'line 2'/;
@fail.push('…and labels the detonation')   unless $e2 ~~ /'Actually thrown at:'/;
# the detonation is line 5 (`say "got: $r"`), and it comes AFTER the label
my $lbl = $e2.index('Actually thrown at:');
@fail.push('the label precedes the second position')
    unless $lbl.defined && $e2.substr($lbl) ~~ /'line 5'/;
# …and the creation position comes BEFORE the label
@fail.push('creation comes first') unless $e2.index('in sub inner') < $lbl;
@fail.push('the program ran up to the use') unless $o2 ~~ /'before use'/;

# --- await: the WORKER's frames, then where it was collected ---------------
my ($o3, $e3) = runit(q:to/END/);
sub work { die "in worker" }
my $p = start { work() };
await $p;
END
@fail.push("await keeps the message first, got:\n$e3") unless $e3.lines[0] eq 'in worker';
@fail.push("await reports the WORKER's frame, got:\n$e3")
    unless $e3 ~~ /'in sub work at ' .*? 'line 1'/;
@fail.push('…and labels the awaiting line') unless $e3 ~~ /'Awaited at:'/;
my $al = $e3.index('Awaited at:');
@fail.push('the awaiting line follows the label')
    unless $al.defined && $e3.substr($al) ~~ /'line 3'/;
@fail.push('the worker frames come first') unless $e3.index('in sub work') < $al;
# a worker thread's empty base frame names nowhere and must not be printed
@fail.push("no line-0 phantom frame, got:\n$e3") unless $e3 !~~ /'line 0'/;

# --- a syntax error shows the line it is talking about --------------------
my ($o4, $e4) = runit("my \$x = 1;\nsay \$x +;\n");
@fail.push('a parse error still says the line number') unless $e4 ~~ /'line 2'/;
@fail.push("…and shows that line, got:\n$e4") unless $e4 ~~ /'say $x +;'/;

# --- and RAKUPP_BACKTRACE=0 silences all of it ----------------------------
my %env = %*ENV.clone; %env<RAKUPP_BACKTRACE> = '0';
my $f5 = $dir.add('quiet.raku');
$f5.spurt("sub deep \{ warn 'careful' \}\ndeep();\nsay 'after';\n");
my $p5 = run($*EXECUTABLE.absolute, $f5.absolute, :out, :err, :%env);
my $e5 = $p5.err.slurp(:close); $p5.out.slurp(:close);
@fail.push("RAKUPP_BACKTRACE=0 leaves warn bare, got:\n$e5") unless $e5.trim eq 'careful';
$f5.unlink;

rmdir($dir);
if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
