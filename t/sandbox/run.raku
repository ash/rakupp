# The --sandbox gate (docs/dev/plans/SANDBOX-PLAN.md).
#
#   build/rakupp t/sandbox/run.raku
#   build/rakupp t/sandbox/run.raku --only=write    # one capability
#
# The binary that runs this file is the one under test.
#
# Every probe is one operation that reaches outside the process, run as a
# child in up to four lanes:
#
#   without --sandbox      it must work and leave its trace — which is what
#                          proves the probe can fail at all
#   --sandbox              both layers: refused with X::SecurityPolicy::Sandbox
#                          naming the right capability, and NO trace (the file
#                          it would have made does not exist, the one it would
#                          have removed is still there, the process it would
#                          have started never ran, the listener it would have
#                          reached saw no connection)
#   --sandbox=language     the interpreter's checks alone: the same
#   the kernel alone       --sandbox with RAKUPP_SANDBOX_SELFTEST=os-only, which
#                          turns the checks off: still no trace, and nothing
#                          read. Only file METADATA (.e, .mode, …) gets through,
#                          as SANDBOX.md says, and a native call, which the
#                          kernel confines like the rest of the process.
#
# Where `rakupp -V` names no OS layer, the first and third lanes run, and plain
# --sandbox must refuse to run at all.
#
# Then the other direction: what a sandboxed program CAN do (standard handles,
# threads, EVAL, a module from the host's -I, path arithmetic) must work, and
# the command line must refuse the combinations that make no sense.
#
# The last line is PASS or FAIL, and the exit code is 0 or 1, as in t/stress.

my $EXE = $*EXECUTABLE.absolute;
my $tmp = $*TMPDIR.add("sandbox-gate-$*PID-{(^1_000_000).pick}");
$tmp.mkdir;
LEAVE { run 'rm', '-rf', $tmp.Str }

# What --sandbox confines with on this machine, as `rakupp -V` reports it.
my $OS-LINE = (run $EXE, '-V', :out, :err).out.slurp(:close).lines.first(*.starts-with('Sandbox')) // '';
my $HAVE-OS = $OS-LINE.contains('built-in checks +');
my @SBX = $HAVE-OS ?? '--sandbox' !! '--sandbox=language';   # what the "allowed" half runs under
my %SELFTEST = |%*ENV, RAKUPP_SANDBOX_SELFTEST => 'os-only';
say "# $OS-LINE";

my ($ok, $bad) = 0, 0;
sub check(Bool() $cond, $desc, $why = '') {
    if $cond { $ok++;  say "ok - $desc" }
    else     { $bad++; say "NOT OK - $desc" ~ ($why ?? "\n    $why" !! '') }
}

# A listener the net probes aim at. It counts the connections it accepts, so a
# refused connect is shown not to have happened.
my $conns = 0;
my $conns-lock = Lock.new;
my $listener = IO::Socket::Async.listen('127.0.0.1', 0);
my $tap = $listener.tap: { $conns-lock.protect: { $conns++ }; .close };
my $PORT = await $tap.socket-port;
sub connections() { $conns-lock.protect: { $conns } }

# A fresh directory per run, with the same fixtures every time, so a probe that
# deletes or renames something cannot disturb the next one.
my $runs = 0;
sub fixture() {
    my $d = $tmp.add('run-' ~ ++$runs);
    $d.mkdir;
    $d.add('in.txt').spurt("line one\nline two\n");
    $d.add('sub').mkdir;
    $d.add('gone.txt').spurt("x");
    $d.add('code.raku').spurt('say "from EVALFILE"');
    $d.add('lib').mkdir;
    $d.add('lib/SbxProbe.rakumod').spurt('unit module SbxProbe; our sub hello is export { "hello from SbxProbe" }');
    $d
}

# The child's program: the operation inside a CATCH that reports a refusal as
# "REFUSED <capability> <operation>", anything else that went wrong as
# "OTHER", and success as "RAN". `.sink` turns a returned Failure into a throw.
sub wrap(Str $code) {
    "\{\n"
    ~ "    CATCH \{\n"
    ~ "        when X::SecurityPolicy::Sandbox \{\n"
    ~ "            say \"REFUSED \{.capability\} \{.operation\} \{\$_ ~~ X::SecurityPolicy\}\"; exit 0\n"
    ~ "        \}\n"
    ~ "        default \{ say \"OTHER \{.^name\}: \{.message.lines.head\}\"; exit 0 \}\n"
    ~ "    \}\n"
    ~ "    my \\r = do \{ $code \};\n"
    ~ "    r.sink;\n"
    ~ "    say 'RAN';\n"
    ~ "\}\n"
}

sub child(@flags, Str $code, :@args, :$in, Bool :$raw, :$cwd, :%env) {
    my $src = $raw ?? $code !! wrap($code);
    my $p = run $EXE, |@flags, '-e', $src, |@args, :out, :err, :in, :cwd($cwd // $*CWD.Str),
                :env(%env || %*ENV);
    $p.in.print($in) if $in.defined;
    my $ = $p.in.close;     # (sunk, a Proc that exited non-zero would throw)
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    ($p.exitcode, $out, $err)
}

# ---- the refusals -----------------------------------------------------------
# cap: the capability the refusal must name. effect: what the operation leaves
# behind when it runs (True) — checked False after the sandboxed run.
# %T% is the run's fixture directory.
my @probes =
    # read
    { cap => 'read', name => 'open a file',              code => 'open("%T%/in.txt").get' },
    { cap => 'read', name => 'slurp (sub)',              code => 'slurp "%T%/in.txt"' },
    { cap => 'read', name => 'IO::Path.slurp',           code => '"%T%/in.txt".IO.slurp' },
    { cap => 'read', name => 'IO::Path.lines',           code => '"%T%/in.txt".IO.lines.elems' },
    { cap => 'read', name => 'IO::Path.words',           code => '"%T%/in.txt".IO.words.elems' },
    { cap => 'read', name => 'IO::Path.open',            code => '"%T%/in.txt".IO.open.get' },
    { cap => 'read', name => 'IO::Handle.new.open',      code => 'IO::Handle.new(:path("%T%/in.txt")).open.get' },
    { cap => 'read', name => 'IO::CatHandle',            code => 'IO::CatHandle.new("%T%/in.txt").slurp' },
    { cap => 'read', name => 'dir (sub)',                code => 'dir("%T%").elems' },
    { cap => 'read', name => 'IO::Path.dir',             code => '"%T%".IO.dir.elems' },
    { cap => 'read', name => 'IO::Path.e',               code => '"%T%/in.txt".IO.e', meta => True },
    { cap => 'read', name => 'IO::Path.f',               code => '"%T%/in.txt".IO.f', meta => True },
    { cap => 'read', name => 'IO::Path.d',               code => '"%T%/sub".IO.d', meta => True },
    { cap => 'read', name => 'IO::Path.s',               code => '"%T%/in.txt".IO.s', meta => True },
    { cap => 'read', name => 'IO::Path.r',               code => '"%T%/in.txt".IO.r', meta => True },
    { cap => 'read', name => 'IO::Path.modified',        code => '"%T%/in.txt".IO.modified', meta => True },
    { cap => 'read', name => 'IO::Path.mode',            code => '"%T%/in.txt".IO.mode', meta => True },
    { cap => 'read', name => 'IO::Path.resolve',         code => '"%T%/in.txt".IO.resolve', meta => True },
    { cap => 'read', name => 'EVALFILE',                 code => 'EVALFILE "%T%/code.raku"' },
    { cap => 'read', name => 'lines() over named files', code => 'lines().elems', args => ['%T%/in.txt'] },
    { cap => 'read', name => 'slurp() over named files', code => 'slurp()', args => ['%T%/in.txt'] },
    # write
    { cap => 'write', name => 'spurt (sub)',      code => 'spurt "%T%/new.txt", "x"',        effect => { .add('new.txt').e } },
    { cap => 'write', name => 'IO::Path.spurt',   code => '"%T%/new.txt".IO.spurt("x")',     effect => { .add('new.txt').e } },
    { cap => 'write', name => 'open :w',          code => 'open("%T%/new.txt", :w).close',   effect => { .add('new.txt').e } },
    { cap => 'write', name => 'open :a',          code => 'open("%T%/new.txt", :a).close',   effect => { .add('new.txt').e } },
    { cap => 'write', name => 'IO::Path.open :w', code => '"%T%/new.txt".IO.open(:w).close', effect => { .add('new.txt').e } },
    { cap => 'write', name => 'mkdir (sub)',      code => 'mkdir "%T%/made"',                effect => { .add('made').e } },
    { cap => 'write', name => 'IO::Path.mkdir',   code => '"%T%/made".IO.mkdir',             effect => { .add('made').e } },
    { cap => 'write', name => 'rmdir (sub)',      code => 'rmdir "%T%/sub"',                 effect => { !.add('sub').e } },
    { cap => 'write', name => 'IO::Path.rmdir',   code => '"%T%/sub".IO.rmdir',              effect => { !.add('sub').e } },
    { cap => 'write', name => 'unlink (sub)',     code => 'unlink "%T%/gone.txt"',           effect => { !.add('gone.txt').e } },
    { cap => 'write', name => 'IO::Path.unlink',  code => '"%T%/gone.txt".IO.unlink',        effect => { !.add('gone.txt').e } },
    { cap => 'write', name => 'rename (sub)',     code => 'rename "%T%/gone.txt", "%T%/moved.txt"', effect => { .add('moved.txt').e } },
    { cap => 'write', name => 'IO::Path.rename',  code => '"%T%/gone.txt".IO.rename("%T%/moved.txt")', effect => { .add('moved.txt').e } },
    { cap => 'write', name => 'move (sub)',       code => 'move "%T%/gone.txt", "%T%/moved.txt"', effect => { .add('moved.txt').e } },
    { cap => 'write', name => 'copy (sub)',       code => 'copy "%T%/in.txt", "%T%/copy.txt"', effect => { .add('copy.txt').e } },
    { cap => 'write', name => 'IO::Path.copy',    code => '"%T%/in.txt".IO.copy("%T%/copy.txt")', effect => { .add('copy.txt').e } },
    { cap => 'write', name => 'chmod (sub)',      code => 'chmod 0o600, "%T%/in.txt"',       effect => { .add('in.txt').mode eq '0600' } },
    { cap => 'write', name => 'IO::Path.chmod',   code => '"%T%/in.txt".IO.chmod(0o600)',    effect => { .add('in.txt').mode eq '0600' } },
    { cap => 'write', name => 'symlink (sub)',    code => 'symlink "%T%/in.txt", "%T%/sym"', effect => { .add('sym').l } },
    { cap => 'write', name => 'IO::Path.symlink', code => '"%T%/in.txt".IO.symlink("%T%/sym")', effect => { .add('sym').l } },
    { cap => 'write', name => 'link (sub)',       code => 'link "%T%/in.txt", "%T%/hard"',   effect => { .add('hard').e } },
    { cap => 'read',  name => 'chdir',            code => 'chdir "%T%/sub"', meta => True },
    { cap => 'read',  name => 'indir',            code => 'indir "%T%/sub", { 1 }', meta => True },
    # run
    { cap => 'run', name => 'run',             code => 'run "touch", "%T%/ran"',                     effect => { .add('ran').e } },
    { cap => 'run', name => 'shell',           code => 'shell "touch %T%/ran"',                      effect => { .add('ran').e } },
    { cap => 'run', name => 'qx',              code => 'qx{touch %T%/ran}',                          effect => { .add('ran').e } },
    { cap => 'run', name => 'qqx',             code => 'my $f = "%T%/ran"; qqx{touch $f}',           effect => { .add('ran').e } },
    { cap => 'run', name => 'Proc::Async',     code => 'await Proc::Async.new("touch", "%T%/ran").start', effect => { .add('ran').e } },
    { cap => 'run', name => 'Proc.spawn',      code => 'Proc.new.spawn("touch", "%T%/ran")',         effect => { .add('ran').e } },
    # net
    { cap => 'net', name => 'IO::Socket::INET connect', code => 'IO::Socket::INET.new(:host<127.0.0.1>, :port(%P%)).close', net => True },
    { cap => 'net', name => 'IO::Socket::INET listen',  code => 'IO::Socket::INET.new(:listen, :localhost<127.0.0.1>, :localport(0)).close' },
    { cap => 'net', name => 'IO::Socket::Async connect', code => '(await IO::Socket::Async.connect("127.0.0.1", %P%)).close', net => True },
    { cap => 'net', name => 'IO::Socket::Async listen', code => 'my $t = IO::Socket::Async.listen("127.0.0.1", 0).tap({ .close }); await $t.socket-port; $t.close' },
    { cap => 'net', name => 'IO::Socket::Async UDP',    code => 'IO::Socket::Async.udp.close', inert => True },
    # ffi
    { cap => 'ffi', name => 'an `is native` routine',  code => 'use NativeCall; sub getpid(--> int32) is native {*}; getpid()', kernel => False },
    ;

my $only = @*ARGS.first(*.starts-with('--only='));
$only = $only.substr(7) with $only;

# One run of a probe in a fresh fixture directory: the outcome line (RAN,
# REFUSED … or OTHER …), the directory to look for traces in, how many
# connections the listener had before, and a diagnostic for a failure.
sub run-probe(%p, @flags, :%env) {
    my $d = fixture();
    my $code = %p<code>.subst('%T%', $d.Str, :g).subst('%P%', $PORT, :g);
    my @args = (%p<args> // []).map(*.subst('%T%', $d.Str, :g));
    my $before = connections();
    my ($rc, $out, $err) = child(@flags, $code, :@args, :cwd($d.Str), :%env);
    my $line = $out.lines.grep(/^ [REFUSED|OTHER|RAN] /).head // '';
    ($line, $d, $before, "rc=$rc out=[{$out.trim}] err=[{$err.lines.head(3).join(' | ')}]")
}
sub reached-no-listener($before) { sleep 0.3; connections() == $before }

my @lanes = $HAVE-OS ?? ('--sandbox', '--sandbox=language') !! ('--sandbox=language',);

for @probes -> %p {
    next if $only && %p<cap> ne $only;
    my $what = "%p<cap>: %p<name>";

    # without --sandbox: it works, and leaves its trace
    {
        my ($line, $d, $before, $diag) = run-probe(%p, []);
        check $line eq 'RAN', "$what works without --sandbox", $diag;
        check %p<effect>($d), "$what leaves its trace without --sandbox" if %p<effect>;
        if %p<net> {
            my $t = now;
            sleep 0.05 while connections() == $before && now - $t < 3;
            check connections() > $before, "$what reaches the listener without --sandbox";
        }
    }
    # each lane: refused, naming the capability, and no trace
    for @lanes -> $flag {
        my ($line, $d, $before, $diag) = run-probe(%p, [$flag]);
        check $line.starts-with("REFUSED %p<cap> ") && $line.ends-with(' True'), "$what is refused under $flag", $diag;
        check !%p<effect>($d), "$what leaves no trace under $flag" if %p<effect>;
        check reached-no-listener($before), "$what reaches no listener under $flag" if %p<net>;
    }
    # the kernel alone
    if $HAVE-OS && !%p<inert> && (%p<kernel> // True) {
        my ($line, $d, $before, $diag) = run-probe(%p, ['--sandbox'], :env(%SELFTEST));
        # A trace is the verdict where there is one: `unlink`, `chmod`, `qx`
        # and the like report a failure in what they RETURN, not by throwing,
        # so their child may well say RAN. A read leaves none, so it has to
        # end in an error — RAN would mean the bytes came back.
        if %p<meta> {
            check $line ne '', "$what runs under the kernel alone (metadata is not confined)", $diag;
        }
        elsif !%p<effect> {
            check $line.starts-with('OTHER'), "$what is stopped by the kernel alone", $diag;
        }
        check !%p<effect>($d), "$what leaves no trace under the kernel alone" if %p<effect>;
        check reached-no-listener($before), "$what reaches no listener under the kernel alone" if %p<net>;
    }
}

# ---- what a sandboxed program can still do -----------------------------------
unless $only {
    my @allowed =
        'say + print + note'     => \('say 1; print "2\n"; note "3"; $*OUT.say(4)', '1 2 4'),
        'stdin: get'             => \('say get', 'hello', :in("hello\nworld\n")),
        'stdin: lines()'         => \('say lines().elems', '2', :in("a\nb\n")),
        'stdin: $*IN.slurp'      => \('say $*IN.slurp.chars', '4', :in("abc\n")),
        'stdin: open("-")'       => \('say open("-").get; say "-".IO.open.get', 'a b', :in("a\nb\n")),
        'stdin: "-".IO.lines'    => \('say "-".IO.lines.elems', '2', :in("a\nb\n")),
        'stdin: "-".IO.words'    => \('say "-".IO.words.elems', '3', :in("a b\nc\n")),
        'stdin: "-".IO.slurp'    => \('say "-".IO.slurp.chars', '4', :in("abc\n")),
        'stdin: slurp("-")'      => \('say slurp("-").chars', '4', :in("abc\n")),
        'stdout: open("-", :w)'  => \('open("-", :w).say("out")', 'out'),
        'start / await'          => \('say await start { 6 * 7 }', '42'),
        'Thread'                 => \('my $x; Thread.start({ $x = 7 }).finish; say $x', '7'),
        'react + Supply.interval' => \('react { whenever Supply.interval(0.01) { say "tick"; done } }', 'tick'),
        'sleep + Promise.in'     => \('sleep 0.01; await Promise.in(0.01); say "slept"', 'slept'),
        'EVAL'                   => \('use MONKEY-SEE-NO-EVAL; say EVAL "1 + 2"', '3'),
        'use Test'               => \('use Test; ok 1, "fine"', 'ok 1 - fine'),
        'path arithmetic'        => \('my $p = "/a/b/c.txt".IO; say $p.basename, " ", $p.extension, " ", $p.parent.Str, " ", $p.add("d").Str, " ", "x".IO.is-relative', 'c.txt txt /a/b /a/b/c.txt/d True'),
        'exit code'              => \('exit 3', ''),
        'a caught refusal'       => \('try slurp "/etc/hosts"; say $!.^name; say "still here"', 'X::SecurityPolicy::Sandbox still here'),
        '%*ENV is empty'         => \('say %*ENV.elems', '0'),
        '%*ENV is writable'      => \('%*ENV<X> = 5; say %*ENV<X>', '5'),
        'rand + DateTime.now'    => \('srand 1; say rand < 1; say DateTime.now.year > 2000', 'True True'),
        ;
    for @allowed -> $a {
        my $c = $a.value;
        my ($code, $want) = $c[0], $c[1];
        my ($rc, $out, $err) = child([|@SBX], $code, :raw, :in($c<in>));
        my $got = $out.words.join(' ');
        my $want-rc = $a.key eq 'exit code' ?? 3 !! 0;
        check $rc == $want-rc && $got eq $want, "allowed: {$a.key}",
              "rc=$rc out=[$got] want=[$want] err=[{$err.lines.head(3).join(' | ')}]";
    }

    # A module from the HOST's search path loads; `use lib` from the program
    # does not add one.
    my $d = fixture();
    my ($rc, $out, $err) = child([|@SBX, '-I', $d.add('lib').Str], 'use SbxProbe; say hello', :raw);
    check $rc == 0 && $out.trim eq 'hello from SbxProbe', 'allowed: a module from the host\'s -I',
          "rc=$rc out=[{$out.trim}] err=[{$err.lines.head(3).join(' | ')}]";
    ($rc, $out, $err) = child([|@SBX], "use lib '{$d.add('lib')}'; use SbxProbe; say hello", :raw);
    check $rc != 0 && $err.contains('not allowed in the sandbox: it needs read access'),
          'refused: `use lib` in the program', "rc=$rc out=[{$out.trim}] err=[{$err.lines.head(3).join(' | ')}]";
    ($rc, $out, $err) = child([], "use lib '{$d.add('lib')}'; use SbxProbe; say hello", :raw);
    check $rc == 0 && $out.trim eq 'hello from SbxProbe', '`use lib` works without --sandbox';

    # An uncaught refusal ends the program with the message, like any error.
    ($rc, $out, $err) = child([|@SBX], 'say "before"; slurp "/etc/hosts"; say "after"', :raw);
    check $rc != 0 && $out.trim eq 'before'
          && $err.contains('slurp is not allowed in the sandbox: it needs read access'),
          'an uncaught refusal ends the program with its message', "rc=$rc out=[{$out.trim}] err=[{$err.lines.head(2).join(' | ')}]";

    # A program file under --sandbox writes no precompilation cache, even with
    # the cache switched on for the run.
    my $home = $tmp.add('home');
    $home.mkdir;
    my $prog = $d.add('prog.raku');
    $prog.spurt("say 'from a file'\n");
    my %env = %*ENV;
    %env<HOME> = $home.Str;
    %env<RAKUPP_PRECOMP_FILES> = 'on';
    %env<RAKUPP_PRECOMP_MODULES> = 'on';
    %env{$_}:delete for <XDG_CACHE_HOME RAKUPP_PRECOMP_DIR RAKUPP_NO_PRECOMP RAKUPP_CONFIG>;
    my $p = run $EXE, |@SBX, $prog.Str, :out, :err, :%env;
    my $pout = $p.out.slurp(:close); $p.err.slurp(:close);
    my @left = find-files($home);
    check $pout.trim eq 'from a file' && !@left, 'a sandboxed program file leaves no precompilation cache',
          "out=[{$pout.trim}] files=[{@left.join(', ')}]";
    $p = run $EXE, $prog.Str, :out, :err, :%env;
    $p.out.slurp(:close); $p.err.slurp(:close);
    check find-files($home).elems > 0, 'the same program without --sandbox does write one (the check above can fail)';

    # The command line.
    ($rc, $out, $err) = child(['--sandbox', '-i', '-p'], '.=uc', :raw, :args([$d.add('in.txt').Str]));
    check $rc == 4 && $err.contains('-i writes the argument files'), '-i is refused with --sandbox';
    for <--highlight --ast -c --lint --exe> -> $mode {
        my $q = run $EXE, '--sandbox', $mode, '-e', 'say 1', :out, :err;
        my $qerr = $q.err.slurp(:close); $q.out.slurp(:close);
        check $qerr.contains('Illegal option --sandbox'), "--sandbox is refused with $mode";
    }
    ($rc, $out, $err) = child(['--sandbox=os'], 'say 1', :raw);
    check $rc == 4 && $err.contains('--sandbox takes no value, or =language'), '--sandbox takes only =language';

    # The OS layer: there, or plain --sandbox does not run at all.
    if $HAVE-OS {
        ($rc, $out, $err) = child(['--sandbox=language'], 'say 6 * 7', :raw);
        check $rc == 0 && $out.trim eq '42', "--sandbox=language runs ($OS-LINE)";
    }
    else {
        ($rc, $out, $err) = child(['--sandbox'], 'say 6 * 7', :raw);
        check $rc == 4 && $out eq '' && $err.contains('--sandbox=language runs with the interpreter'),
              "without an OS layer, --sandbox refuses to run ($OS-LINE)", "rc=$rc err=[{$err.trim}]";
        ($rc, $out, $err) = child(['--sandbox=language'], 'say 6 * 7', :raw);
        check $rc == 0 && $out.trim eq '42', '…and --sandbox=language runs';
    }
}

sub find-files(IO::Path $d) {
    gather for $d.dir -> $e { $e.d ?? take(|find-files($e)) !! take($e.Str) }
}

$tap.close;
say "sandbox gate: $ok ok, $bad failed";
say $bad ?? 'FAIL' !! 'PASS';
exit $bad ?? 1 !! 0;
