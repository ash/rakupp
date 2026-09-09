# Regression: `run` must NOT hand the child a process group of its own.
#
# The child used to call setpgid(0, 0) unconditionally, so it landed in a group
# that is not the terminal's foreground group — a BACKGROUND job. The kernel
# stops such a process the moment it touches the terminal: SIGTTOU on tcsetattr,
# SIGTTIN on a read. `run 'stty', '-echo'` therefore stopped forever, which is
# how `fez login` echoed the password back and then wedged (issue #72), and so
# did every interactive child — `less`, `vi`, a `sudo` prompt.
#
# The tty half of that cannot be probed without a controlling terminal, so what
# is pinned here is the mechanism behind it: whose process group the child is in.
# `ps` is asked about OUR pid independently, so the expected value does not come
# from the same inheritance the rows are testing.
#
# The group is still taken when `:timeout` is in play — that is the one caller
# that has to kill grandchildren, and `kill(-pid)` needs a group to name.
# Checked against Rakudo (which never made a group either) for every row that
# does not use rakupp's own `:timeout`.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

if $*DISTRO.is-win {
    say "ok - skipped on Windows: no POSIX process groups";
    say "PASS";
    exit 0;
}

my $pp = $*RAKU.compiler.name eq 'Raku++';

# our own group, asked of ps directly rather than inferred from a child
my $mine = run('ps', '-o', 'pgid=', '-p', ~$*PID, :out).out.slurp.trim;
ck(($mine ~~ /^\d+$/).so, True, 'ps reports our own process group');

# --- a plain run: the child stays in OUR group ----------------------------
my $plain = run('sh', '-c', 'ps -o pgid= -p $$', :out).out.slurp.trim;
ck($plain, $mine, 'run() leaves the child in our process group');

# --- so does one whose stderr is captured too -----------------------------
my $both = run('sh', '-c', 'ps -o pgid= -p $$ 1>&2', :out, :err).err.slurp.trim;
ck($both, $mine, 'and with :err captured as well');

# --- and shell(), which goes through the same spawn -----------------------
my $sh = shell('ps -o pgid= -p $$', :out).out.slurp.trim;
ck($sh, $mine, 'shell() too');

# --- Proc::Async takes the same path --------------------------------------
my $async = '';
my $proc  = Proc::Async.new('sh', '-c', 'ps -o pgid= -p $$');
$proc.stdout.tap(-> $c { $async ~= $c });
await $proc.start;
ck($async.trim, $mine, 'Proc::Async leaves the child in our group');

# --- a child that would have been stopped by SIGTTIN now runs to completion.
#     (Without a terminal it never could be — this row pins that a stdin-reading
#     child still gets a working stdin, which the fix must not have disturbed.)
my $fed = run('cat', :in, :out);
$fed.in.say('marker-ttin-row');
$fed.in.close;
ck($fed.out.slurp.trim, 'marker-ttin-row', 'a stdin-reading child still reads its stdin');

# --- :timeout is the one caller that DOES want the group ------------------
if $pp {
    # `$$` and its group: with :timeout the child must be its own group leader
    my $t = run('sh', '-c', 'echo $$ $(ps -o pgid= -p $$)', :out, :timeout(20)).out.slurp.trim;
    my ($cpid, $cpgid) = $t.words;
    ck($cpgid, $cpid,   'run(:timeout) makes the child its own group leader');
    ck($cpgid eq $mine, False, 'which is NOT our group');

    # and the group is what the deadline kills: the grandchild is detached from
    # the shell that started it, so only a group-wide kill reaches it
    my $t0 = now;
    my $mark = 'rakupp-pgroup-timeout-marker';
    run('sh', '-c', "sh -c 'exec sleep 25 $mark' & wait", :out, :err, :timeout(2));
    ck((now - $t0) < 15, True, 'the deadline fires rather than waiting out the child');
    my $left = run('sh', '-c', "ps -axo command | grep -c '[s]leep 25 $mark'", :out).out.slurp.trim;
    ck($left, '0', 'and it kills the detached grandchild with the group');
}
else {
    say "ok - :timeout rows are rakupp's own extension, skipped under {$*RAKU.compiler.name}";
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
