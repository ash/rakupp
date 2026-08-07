#!/usr/bin/env raku
# The rakupp example + showcase regression suite. Runs every example and
# showcase with the same rakupp binary that runs this file ($*EXECUTABLE) and
# checks output — byte-for-byte against a golden where the program is
# deterministic, or by asserting on structure where it is not. TAP output;
# exits non-zero on any failure.
#
#   build/rakupp t/run.raku
#
# After an *intentional* output change, refresh a golden:
#   build/rakupp examples/NAME.raku > t/expected/NAME.out
#
# (Kept as one self-contained file on purpose: rakupp's module `is export` is
# still flaky for many-sub modules, so the helpers live here inline.)

my $ROOT  = $?FILE.IO.parent.parent;                 # repo root, CWD-independent
my $EXP   = $?FILE.IO.parent.add('expected');
my $count = 0;
my $fails = 0;

# ---- TAP helpers ------------------------------------------------------
sub ok(Mu $cond, Str $desc) {
    $count++;
    if $cond { say "ok $count - $desc" }
    else     { $fails++; say "not ok $count - $desc" }
}
sub is(Mu $got, Mu $want, Str $desc) {
    ok($got eq $want, $desc);
    diag("got '$got', expected '$want'") unless $got eq $want;
}
sub skip(Str $desc) {   # counts as a pass, but SAYS it did not run
    $count++;
    say "ok $count - $desc # SKIP";
}
sub diag(Str $m) { note "# $m" }
sub section(Str $t) { note ""; note "# ── $t ──" }

# Run `rakupp <args>`, return (stdout, exitcode).
sub run-rakupp(*@args) {
    my $p = run($*EXECUTABLE, |@args, :out, :!err);
    ($p.out.slurp(:close), $p.exitcode)
}

# stdout of `rakupp @args` must equal the golden file, byte for byte.
sub golden(@args, Str $goldenfile, Str $desc, :$retries = 0) {
    unless $goldenfile.IO.e { ok(False, $desc); diag("missing golden: $goldenfile"); return }
    my $want = $goldenfile.IO.slurp;
    my ($out, $exit);
    # Timing-based demos (sleep-sort) can misorder under load; a retry or
    # two distinguishes scheduler jitter from a real regression.
    for 0..$retries {
        ($out, $exit) = run-rakupp(|@args);
        last if $exit == 0 && $out eq $want;
    }
    if $exit != 0 { ok(False, $desc); diag("non-zero exit: $exit"); return }
    if $out eq $want { ok(True, $desc) }
    else {
        ok(False, $desc);
        diag("differs from {$goldenfile.IO.basename}: got {$out.chars} chars, want {$want.chars}");
        for ^min($out.chars, $want.chars) -> $i {
            if $out.substr($i,1) ne $want.substr($i,1) {
                diag("first diff at char $i: got …{$out.substr(max(0,$i-8),18).subst("\n","⏎",:g)}…");
                last;
            }
        }
    }
}

sub contains-all(Str $hay, @needles, Str $desc) {
    my @missing = @needles.grep({ !$hay.contains($_) });
    ok(!@missing, $desc);
    diag("missing: {@missing.join(' | ')}") if @missing;
}

# ---- examples ---------------------------------------------------------
section('examples/ (golden output)');
my @deterministic = <
    anagrams brainfuck calculator cipher echo-server factorize fibonacci hanoi
    json mandel matrix nqueens parallel pascal primes quicksort quine rationals
    roman rpn sierpinski sleep-sort wordcount
>;
for @deterministic -> $name {
    golden([$ROOT.add("examples/$name.raku").Str], $EXP.add("$name.out").Str, "example: $name",
           :retries($name eq 'sleep-sort' ?? 4 !! 0));
}
{
    my ($out, $exit) = run-rakupp($ROOT.add('examples/life.raku').Str);   # random seed
    ok($exit == 0 && $out.chars > 100, "example: life runs and draws (smoke)");
}

# ---- lisp showcase ----------------------------------------------------
section('showcase/lisp (a Scheme on a Raku grammar)');
my $lisp = $ROOT.add('showcase/lisp/lisp.raku').Str;
for <fact closures> -> $n {
    golden([$lisp, $ROOT.add("showcase/lisp/examples/$n.scm").Str],
           $EXP.add("lisp/$n.out").Str, "lisp example: $n.scm");
}
golden([$lisp, $ROOT.add('t/fixtures/lisp-features.scm').Str],
       $EXP.add('lisp-features.out').Str, "lisp: feature fixture (lists, HOFs, quasiquote, bignums)");
{
    my ($fact, $) = run-rakupp($lisp, $ROOT.add('showcase/lisp/examples/fact.scm').Str);
    ok($fact.contains('100! = 933262154439441526816992388562667004907'),
       "lisp: (fact 100) is exact to all 158 digits");
}

# ---- forth showcase ---------------------------------------------------
section('showcase/forth (a stack machine)');
{
    my $forth = $ROOT.add('showcase/forth/forth.raku').Str;
    golden([$forth, $ROOT.add('showcase/forth/examples/demo.fth').Str],
           $EXP.add('forth-demo.out').Str, "forth: demo.fth → golden output");
}

# ---- markdown showcase ------------------------------------------------
section('showcase/markdown (grammar → HTML)');
my $md = $ROOT.add('showcase/markdown/md2html.raku').Str;
golden([$md, $ROOT.add('showcase/markdown/sample.md').Str],
       $EXP.add('markdown-sample.out').Str, "markdown: sample.md → golden HTML");
{
    my ($out, $) = run-rakupp($md, $ROOT.add('showcase/markdown/sample.md').Str);
    contains-all($out, [
        '<h1>Raku++ Markdown</h1>', '<strong>Markdown</strong>', '<em>Raku</em>',
        '<code>grammar</code>', '<a href="https://github.com/ash/rakupp">links</a>',
        '<ul>', '<ol>', '<blockquote>', '<pre><code>', '<hr>',
    ], "markdown: every block and inline construct is present");
}

# ---- json showcase ----------------------------------------------------
section('showcase/json (parse + serialize + query)');
{
    my $json   = $ROOT.add('showcase/json/json.raku').Str;
    my $sample = $ROOT.add('showcase/json/sample.json').Str;
    golden([$json, $sample], $EXP.add('json-sample.out').Str, "json: pretty-print → golden");
    my ($name, $) = run-rakupp($json, '--query=.users[1].name', $sample);
    ok($name.trim eq '"Grace"', "json: --query pulls a nested value (indexes correctly)");
    my ($compact, $) = run-rakupp($json, '--compact', $sample);
    ok($compact.contains('"version":"0.5.1"') && !$compact.contains("\n  "),
       "json: --compact minifies to one line");
}

# ---- server showcases (pastebin + chat) -------------------------------
# The servers are long-lived accept loops. rakupp's Proc::Async deadlocks when
# the same process then does blocking socket I/O, so we background them through
# the shell (proven to work), poll until they accept, drive them over real
# sockets, then pkill by script name.
sub start-server(Str $script, Int $port) {
    shell("$*EXECUTABLE $script $port >/dev/null 2>&1 &");
    for ^40 {                                    # up to ~8 s to bind
        my $s = try IO::Socket::INET.new(:host('127.0.0.1'), :port($port));
        if $s { $s.close; return True }
        sleep 0.2;
    }
    False;
}
# `|| true` so a no-match pkill (or a server that already exited) can't fail the
# suite. NB pkill -f takes a REGEX and the full path contains `raku++` — an
# invalid pattern that matches nothing, so servers leaked and piled up across
# runs (a zombie on a colliding port answered INCR with a stale count). Kill by
# basename instead.
sub stop-server(Str $script) { try shell("pkill -f '{$script.IO.basename}' 2>/dev/null || true"); }

sub recv-all($sock --> Str) {              # read until the peer closes
    my $r = '';
    loop { my $c = $sock.recv; last unless $c.defined && $c ne ''; $r ~= $c }
    $r;
}
sub http(Int $port, Str $req --> Str) {
    my $s = IO::Socket::INET.new(:host('127.0.0.1'), :port($port));
    $s.print($req);
    my $resp = recv-all($s);
    $s.close;
    $resp;
}

section('showcase/pastebin (HTTP on raw sockets)');
{
    my $script = $ROOT.add('showcase/pastebin/pastebin.raku').Str;
    my $port = 8391;
    if start-server($script, $port) {
        my $home = http($port, "GET / HTTP/1.0\r\nHost: x\r\n\r\n");
        ok($home.contains('200') && $home.contains('rakupp pastebin'), "pastebin: GET / serves the form");

        my $body = 'content=hello+from+the+test+suite';
        my $post = http($port, "POST /paste HTTP/1.0\r\nHost: x\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: {$body.chars}\r\n\r\n$body");
        my $loc = ($post ~~ /'Location: ' (\S+)/) ?? ~$0 !! '';
        ok($post.contains('303') && $loc.starts-with('/p/'), "pastebin: POST /paste redirects to a new paste");

        my $view = http($port, "GET $loc HTTP/1.0\r\nHost: x\r\n\r\n");
        ok($view.contains('hello from the test suite'), "pastebin: the paste is retrievable at $loc");

        my $raw = http($port, "GET {$loc.subst('/p/','/raw/')} HTTP/1.0\r\nHost: x\r\n\r\n");
        ok($raw.contains('text/plain') && $raw.contains('hello from the test suite'), "pastebin: /raw/<id> serves plain text");
    }
    else { ok(False, "pastebin: server did not start"); }
    stop-server($script);
}

section('showcase/chat (concurrent TCP)');
{
    my $script = $ROOT.add('showcase/chat/chat.raku').Str;
    my $port = 6691;
    if start-server($script, $port) {
        # The two-client interaction runs in its own process (see the fixture).
        my ($out, $) = run-rakupp($ROOT.add('t/fixtures/chat-client.raku').Str, ~$port);
        ok($out.contains('CHAT-OK'), "chat: nick, join, broadcast, and /who across two clients");
        diag($out.trim) unless $out.contains('CHAT-OK');
    }
    else { ok(False, "chat: server did not start"); }
    stop-server($script);
}

section('showcase/kvstore (a key-value protocol)');
{
    my $script = $ROOT.add('showcase/kvstore/kvstore.raku').Str;
    my $port = 6300 + $*PID % 200; # per-run port: a leaked server from a
                                   # previous run can't poison this run's INCR
    if start-server($script, $port) {
        # one connection, a sequence of commands, each reply read in turn.
        # NB replies are read per LINE through a buffer — two back-to-back
        # commands can coalesce into one recv, and a raw recv here would eat
        # the next command's reply (the INCR check flapped on exactly that).
        my $s = IO::Socket::INET.new(:host('127.0.0.1'), :port($port));
        $s.recv;                                   # greeting
        my $rbuf = '';
        sub cmd(Str $c --> Str) {
            $s.print("$c\r\n");
            until $rbuf.contains("\n") {
                my $r = $s.recv // '';
                last if $r eq '';
                $rbuf ~= $r;
            }
            my $i = $rbuf.index("\n");
            with $i { my $line = $rbuf.substr(0, $i); $rbuf = $rbuf.substr($i + 1); return $line.trim }
            my $line = $rbuf; $rbuf = ''; $line.trim
        }
        ok(cmd('SET name ada') eq 'OK',            "kvstore: SET replies OK");
        ok(cmd('GET name') eq 'ada',               "kvstore: GET returns the value");
        ok(cmd('SET g "hello world"') eq 'OK' && cmd('GET g') eq 'hello world',
                                                   "kvstore: quoted values keep their spaces");
        cmd('INCR hits');
        is(cmd('INCR hits'), '2',                  "kvstore: INCR counts up");
        ok(cmd('EXISTS name') eq '1' && cmd('EXISTS nope') eq '0',
                                                   "kvstore: EXISTS reports presence");
        ok(cmd('DEL name') eq '1' && cmd('GET name') eq '(nil)',
                                                   "kvstore: DEL removes the key");
        $s.print("QUIT\r\n"); $s.close;
    }
    else { ok(False, "kvstore: server did not start"); }
    stop-server($script);
}

section('showcase/rakus (a static HTTP file server)');
{
    my $script = $ROOT.add('showcase/rakus/rakus.raku').Str;
    my $port = 8493;
    if start-server($script, $port) {
        my $home = http($port, "GET / HTTP/1.0\r\nHost: x\r\n\r\n");
        ok($home.contains('200') && $home.contains('rakus is serving'), "rakus: serves index.html at /");

        my $css = http($port, "GET /style.css HTTP/1.0\r\nHost: x\r\n\r\n");
        ok($css.contains('200') && $css.contains('text/css'), "rakus: serves a file with the right Content-Type");

        my $list = http($port, "GET /files/ HTTP/1.0\r\nHost: x\r\n\r\n");
        ok($list.contains('200') && $list.contains('Index of /files/') && $list.contains('data.json'),
                                                   "rakus: auto directory listing when there's no index");

        my $miss = http($port, "GET /nope HTTP/1.0\r\nHost: x\r\n\r\n");
        ok($miss.contains('404'),                  "rakus: 404 for a missing path");

        my $redir = http($port, "GET /files HTTP/1.0\r\nHost: x\r\n\r\n");
        ok($redir.contains('301') && $redir.contains('Location: /files/'),
                                                   "rakus: redirects a dir without a trailing slash");
    }
    else { ok(False, "rakus: server did not start"); }
    stop-server($script);
}

# ---- once-broken regressions ------------------------------------------
# t/regression/ holds minimal repros of bugs a rakupp change INTRODUCED and
# a later commit fixed (each file's header says what broke and what fixed
# it). Auto-discovered: a case passes iff it exits 0 with `PASS` as its
# last stdout line. Add new cases as plain files; nothing to register.
section('t/regression (once broken, must stay fixed)');
for dir($ROOT.add('t/regression')).grep(*.Str.ends-with('.raku')).sort -> $f {
    # `#?requires Foo::Bar` — a case that needs a module which is not part of this
    # repo (the Cro dists, CBOR::Simple) declares it, and we probe for it and SKIP
    # when it is absent. Two cases used to lean on a missing `use` being silently
    # ignored instead, which meant they passed on every machine that did NOT have
    # the module — testing nothing, indistinguishably from testing everything. Now
    # that a failed load is fatal, a declared requirement is the honest way to say
    # "not applicable here".
    my $needs = $f.slurp.lines.grep(*.starts-with('#?requires ')).map(*.substr(11).trim);
    my $missing = $needs.first({
        my $probe = run($*EXECUTABLE, '-e', "use $_;", :out, :err);
        $probe.out.slurp(:close); $probe.err.slurp(:close);
        $probe.exitcode != 0;
    });
    with $missing {
        skip("regression: {$f.basename} (needs $missing)");
        next;
    }
    # capture stderr too: a failing case prints WHICH check failed on stderr via
    # `note`, and run-rakupp discards it — so a CI failure used to say only
    # "exit=0 last-line='FAIL'", which is not enough to act on. (Learned the hard
    # way from a macOS-only failure of the blocking-receive case.)
    my $p = run($*EXECUTABLE, $f.Str, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    my $exit = $p.exitcode;
    my $last = $out.lines.tail // '';
    ok($exit == 0 && $last eq 'PASS', "regression: {$f.basename}");
    if $exit != 0 || $last ne 'PASS' {
        diag("exit=$exit last-line='$last'");
        diag("stderr: $_") for $err.lines;
    }
}

# ---- native codegen coverage -------------------------------------------
# Every example and bench kernel must stay NATIVELY compilable: `--cpp` exits
# 0 when the transpiler covers the program, 5 when `--exe` would fall back to
# bundling. This pins the codegen's coverage so a change can't silently knock
# a program back onto the interpreter bundle.
section('native codegen coverage (--exe compiles these natively)');
for <examples tools/bench tools/optbench> -> $dir {
    my @fellback;
    for dir($ROOT.add($dir)).grep(*.Str.ends-with('.raku')).sort -> $f {
        my $p = run($*EXECUTABLE, '--cpp', $f.Str, :!out, :!err);
        @fellback.push($f.basename) if $p.exitcode != 0;
    }
    ok(!@fellback, "$dir: every program transpiles natively");
    diag("fell back (or parse error): {@fellback.join(', ')}") if @fellback;
}
# And one full end-to-end native build: transpile + C++-compile + run + golden.
{
    my $bin = $*TMPDIR.add("rakupp-suite-exe-$*PID").Str;
    my $p = run($*EXECUTABLE, '--exe', $ROOT.add('examples/fibonacci.raku').Str, '-o', $bin, :out, :err);
    $p.out.slurp(:close);
    my $msg = $p.err.slurp(:close);   # "Compiled (native) …" is reported on stderr
    ok($p.exitcode == 0 && $msg.contains('(native)'), "--exe builds fibonacci as a native binary");
    $p = run($bin, :out);
    my $got = $p.out.slurp(:close);
    ok($got eq $EXP.add('fibonacci.out').IO.slurp, "the native fibonacci binary matches the golden");
    try unlink $bin;
}
# Native parity: deep recursion (real main-thread stack), CATCH .message on
# builtin errors, block-final if/else value, Order-comparator sort — each of
# these once behaved differently under --exe than under the interpreter.
{
    my $bin = $*TMPDIR.add("rakupp-suite-parity-$*PID").Str;
    my $p = run($*EXECUTABLE, '--exe', $ROOT.add('t/fixtures/native-parity.raku').Str, '-o', $bin, :out, :err);
    $p.out.slurp(:close);
    my $msg = $p.err.slurp(:close);
    ok($p.exitcode == 0 && $msg.contains('(native)'), "--exe builds the native-parity fixture natively");
    $p = run($bin, :out);
    my $got = $p.out.slurp(:close);
    my $want = q:to/END/;
        deep: 30000
        caught: No such method 'nosuchmethod' for invocant of type 'Int'
        pick: one two many
        sort: 1,2,5,9
        END
    ok($got eq $want, "the native binary matches the interpreter on the parity probes");
    diag("got: $got") if $got ne $want;
    try unlink $bin;
}

# ---- compile modes carry their modules ---------------------------------
# All three compile modes must produce a SELF-SUFFICIENT binary: it has to run
# with its module tree gone from the machine. Each mode reached this differently
# and only --exe was ever checked, so --bundle shipped for a while embedding the
# mainline alone and failing the moment a `use`d module moved.
#
# The fixture uses a module that itself `use`s another, so this also pins that
# the bundler follows the graph rather than the program's own `use` lines, and
# it exercises an exported sub, class and operator — not merely "the file was
# found". The module tree is moved aside for the run, which is the only way to
# prove the binary is not quietly reading it.
section('compile modes embed their modules (run with the module tree removed)');
{
    my $lib   = $ROOT.add('t/fixtures/modlib');
    my $prog  = $ROOT.add('t/fixtures/uses-modules.raku');
    my $want  = "outer(inner)\nshape-7\n34\n";
    my $hide  = $*TMPDIR.add("rakupp-modlib-hidden-$*PID");

    my %bin;
    for <bundle aot exe> -> $mode {
        my $bin = $*TMPDIR.add("rakupp-suite-mod-$mode-$*PID").Str;
        my $p = run($*EXECUTABLE, "--$mode", $prog.Str, '-I', $lib.Str, '-o', $bin, :out, :err);
        $p.out.slurp(:close); my $err = $p.err.slurp(:close);
        ok($p.exitcode == 0, "--$mode builds a program that uses modules");
        diag("--$mode: $err") if $p.exitcode != 0;
        %bin{$mode} = $bin;
    }

    # the only honest test: take the modules away
    rename($lib, $hide);
    LEAVE { rename($hide, $lib) if $hide.e }
    for <bundle aot exe> -> $mode {
        my $p = run(%bin{$mode}, :out, :err);
        my $got = $p.out.slurp(:close); $p.err.slurp(:close);
        ok($got eq $want, "--$mode binary runs with its module tree deleted");
        diag("--$mode got: {$got.raku}") if $got ne $want;
    }
    # …and the interpreter must NOT: otherwise the modules are still reachable
    # and the three checks above proved nothing.
    {
        my $p = run($*EXECUTABLE, '-I', $lib.Str, $prog.Str, :out, :err);
        my $got = $p.out.slurp(:close); $p.err.slurp(:close);
        ok($got ne $want, "control: the interpreter cannot run it without the modules");
    }
    try unlink %bin{$_} for <bundle aot exe>;
}

# ---- a module export vs a same-named built-in --------------------------
# The interpreter resolves a call through the environment BEFORE the builtin
# table, so an `is export`ed `sub val` wins over the built-in `val` (and a
# non-exported one deliberately does not — loadModule's publish carve-out).
# `--exe` resolved calls by NAME at compile time and emitted a cached builtin
# pointer for anything in the table, so the env lookup never happened and the
# binary printed the built-in's answer where the interpreter printed the
# module's. -O is checked too: its direct named-builtin calls (`lc` is one)
# bypass even that pointer. Rakudo agrees with the expected output.
section('an exported module sub shadows a built-in (every compile mode)');
{
    my $lib  = $ROOT.add('t/fixtures/shadowlib');
    my $prog = $ROOT.add('t/fixtures/shadows-builtin.raku');
    my $want = "export-wins\nab\nprivate-lc(X)\n";

    my $p = run($*EXECUTABLE, '-I', $lib.Str, $prog.Str, :out, :err);
    my $got = $p.out.slurp(:close); $p.err.slurp(:close);
    is($got, $want, 'interpreter: the export wins, the private sub does not');

    for ('bundle',), ('aot',), ('exe',), ('exe', '-O') -> @mode {
        my $desc = @mode.join(' ');
        my $bin  = $*TMPDIR.add("rakupp-suite-shadow-{@mode.join('')}-$*PID").Str;
        my $c = run($*EXECUTABLE, "--@mode[0]", |@mode[1..*], $prog.Str, '-I', $lib.Str, '-o', $bin, :out, :err);
        $c.out.slurp(:close); my $err = $c.err.slurp(:close);
        unless $c.exitcode == 0 { ok(False, "--$desc builds the shadowing program"); diag("--$desc: $err"); next }
        my $r = run($bin, :out, :err);   # the binary carries the module: no -I
        my $out = $r.out.slurp(:close); $r.err.slurp(:close);
        is($out, $want, "--$desc agrees with the interpreter on built-in shadowing");
        try unlink $bin;
    }
}

# ---- module loading & the precomp cache --------------------------------
# Smoke coverage for the module system itself, and specifically for the parsed-
# AST cache's INVALIDATION. Every bug found in that cache so far — entries
# accumulating per edit, a stale entry surviving a rakupp rebuild, one script
# run from two directories sharing an entry — was in deciding WHETHER to reuse
# an entry, never in the serializer. Round-tripping ASTs proves nothing about
# any of them, so these drive the real decisions instead.
#
# The cache is pointed at a scratch directory: the suite must not read, write or
# empty the developer's own.
section('module loading and the precompiled-AST cache');
{
    my $work  = $*TMPDIR.add("rakupp-modsmoke-$*PID");
    my $cache = $work.add('cache');
    my $lib   = $work.add('lib');
    mkdir $lib.add('Deep');
    LEAVE { try { for $work.dir(:!all) { } }; }   # best-effort; $*TMPDIR is transient

    $lib.add('Deep/Leaf.rakumod').spurt: q:to/END/;
        unit module Deep::Leaf;
        sub leaf() is export { 'leaf' }
        END
    $lib.add('Mid.rakumod').spurt: q:to/END/;
        unit module Mid;
        use Deep::Leaf;
        sub mid() is export { 'mid(' ~ leaf() ~ ')' }
        END
    my $prog = $work.add('p.raku');
    $prog.spurt: q:to/END/;
        use Mid;
        say mid();
        END

    # the cache fans out one directory level, so this has to recurse
    sub all-files($d) {
        return () unless $d.e;
        my @out;
        for $d.dir -> $e { $e.d ?? (@out.append(all-files($e))) !! @out.push($e) }
        @out
    }
    sub entry-count() { +all-files($cache).grep(*.Str.ends-with('.ast')) }

    # A properly MERGED environment. `:env(%*ENV, K => v)` builds a list, not a
    # hash, so the override is silently dropped and the run uses the real cache —
    # which would make this section both meaningless and destructive, since it
    # calls --precomp-clean.
    #
    # Caching is OFF by default, so every run here opts in explicitly. That also
    # keeps the suite from depending on whatever the developer has configured.
    sub env-with(%extra) {
        my %e = %*ENV;
        %e<RAKUPP_PRECOMP_DIR>     = $cache.Str;
        %e<RAKUPP_PRECOMP_MODULES> = '1';
        %e<RAKUPP_PRECOMP_FILES>   = '1';
        %e<RAKUPP_CONFIG>          = $work.add('rakupp.config').Str;  # never the real one
        %e{.key} = .value for %extra;
        %e
    }
    # run with the scratch cache; returns stdout
    sub cached-run(*@extra) {
        my $p = run($*EXECUTABLE, '-I', $lib.Str, $prog.Str, |@extra, :out, :!err,
                    :env(env-with({})));
        $p.out.slurp(:close)
    }

    # THE DEFAULTS, with nothing configured: nothing cached at all. rakupp does
    # not put files on a user's disk until asked. `modules` is the switch likely
    # to become a default later — on the measurements it is a clear win — so this
    # counts what was written rather than merely asserting the program ran, and
    # will say exactly what changed when that day comes.
    {
        my $virgin = $work.add('virgin-cache');
        my %e = %*ENV;
        %e<RAKUPP_PRECOMP_DIR> = $virgin.Str;
        %e<RAKUPP_CONFIG>      = $work.add('none.config').Str;   # no settings at all
        %e<RAKUPP_PRECOMP_MODULES>:delete;
        %e<RAKUPP_PRECOMP_FILES>:delete;
        %e<RAKUPP_NO_PRECOMP>:delete;
        my $p = run($*EXECUTABLE, '-I', $lib.Str, $prog.Str, :out, :!err, :env(%e));
        is($p.out.slurp(:close), "mid(leaf)\n", 'runs with the default settings');
        my @wrote = $virgin.e ?? all-files($virgin).grep(*.Str.ends-with('.ast')) !! ();
        ok(+@wrote == 0, "nothing is cached until asked (wrote {+@wrote} entries)");

        # …and RAKUPP_NO_PRECOMP still silences everything
        my $silent = $work.add('silent-cache');
        %e<RAKUPP_PRECOMP_DIR> = $silent.Str;
        %e<RAKUPP_NO_PRECOMP>  = '1';
        run($*EXECUTABLE, '-I', $lib.Str, $prog.Str, :!out, :!err, :env(%e));
        my $n = $silent.e ?? +all-files($silent) !! 0;
        ok($n == 0, "RAKUPP_NO_PRECOMP=1 writes nothing at all (wrote $n)");
    }

    is(cached-run(), "mid(leaf)\n", 'nested modules load (cold cache)');
    is(cached-run(), "mid(leaf)\n", 'nested modules load (warm cache)');
    ok(entry-count() == 3, "one cache entry per source file (got {entry-count()}, want 3)");

    # an edit REPLACES its entry — it must not add one, or a module under
    # development strands a new file on every save
    $lib.add('Mid.rakumod').spurt: q:to/END/;
        unit module Mid;
        use Deep::Leaf;
        sub mid() is export { 'MID2(' ~ leaf() ~ ')' }
        END
    is(cached-run(), "MID2(leaf)\n", 'editing a module invalidates its entry');
    ok(entry-count() == 3, "an edit replaces an entry, never adds one (got {entry-count()})");

    # timestamps are not consulted: content is. Backdate an edit far into the
    # past — the mtime+size scheme this deliberately avoids would miss it.
    my $mid = $lib.add('Mid.rakumod');
    $mid.spurt: q:to/END/;
        unit module Mid;
        use Deep::Leaf;
        sub mid() is export { 'MID3(' ~ leaf() ~ ')' }
        END
    run('touch', '-t', '197001010000', $mid.Str, :!out, :!err);
    is(cached-run(), "MID3(leaf)\n", 'an edit backdated to 1970 is still picked up');

    # the cache must never change behaviour
    my $p = run($*EXECUTABLE, '-I', $lib.Str, $prog.Str, :out, :!err,
                :env(env-with({ RAKUPP_NO_PRECOMP => '1' })));
    is($p.out.slurp(:close), "MID3(leaf)\n", 'RAKUPP_NO_PRECOMP=1 gives the same answer');

    # --precomp-info lists sources; --precomp-clean empties it and leaves no
    # empty directories behind
    my $info = run($*EXECUTABLE, '--precomp-info', :out, :!err,
                   :env(env-with({}))).out.slurp(:close);
    ok($info.contains('Mid.rakumod') && $info.contains('Deep/Leaf.rakumod'),
       '--precomp-info names the sources it cached');

    # A DELETED source leaves an entry that will never be read or rewritten —
    # a checkout thrown away, a module version zef replaced, a per-run temp
    # directory. Those used to pile up forever and be reported as merely
    # "stale", i.e. as though the next run would deal with them.
    $lib.add('Gone.rakumod').spurt: q:to/END/;
        unit module Gone;
        sub gone() is export { 'gone' }
        END
    my $gone-prog = $work.add('g.raku');
    $gone-prog.spurt("use Gone;\nsay gone();\n");
    run($*EXECUTABLE, '-I', $lib.Str, $gone-prog.Str, :!out, :!err, :env(env-with({})));
    $lib.add('Gone.rakumod').unlink;
    my $orphan-info = run($*EXECUTABLE, '--precomp-info', :out, :!err,
                          :env(env-with({}))).out.slurp(:close);
    my $gone-line = $orphan-info.lines.first(*.contains('Gone.rakumod')) // '';
    ok($gone-line.starts-with('  x '),
       "a deleted source marks its entry orphaned (line: '{$gone-line.trim}')");
    ok($orphan-info.contains('orphaned') && $orphan-info.contains('the source file is gone'),
       '--precomp-info says orphans are never rewritten');

    run($*EXECUTABLE, '--precomp-clean', :!out, :!err,
        :env(env-with({})));
    ok(entry-count() == 0, '--precomp-clean empties the cache');
    my $left = $cache.e ?? +$cache.dir.grep(*.d) !! 0;
    ok($left == 0, "--precomp-clean removes its directories too (left $left)");

    # The same script in two directories can `use` DIFFERENT modules, because
    # `.` and `lib` are in the search path. Sharing one entry between them once
    # replayed one directory's parse in the other.
    my $x = $work.add('x'); my $y = $work.add('y');
    mkdir $x; mkdir $y; mkdir $x.add('lib'); mkdir $y.add('lib');
    # NB: q:to/END/, not a "…" string — a double-quoted Raku string interpolates
    # `{ … }`, which silently eats a sub body.
    $x.add('lib/Same.rakumod').spurt: q:to/END/;
        unit module Same;
        sub who() is export { 'X' }
        END
    $y.add('lib/Same.rakumod').spurt: q:to/END/;
        unit module Same;
        sub who() is export { 'Y' }
        END
    my $shared = $work.add('shared.raku');
    $shared.spurt: q:to/END/;
        use Same;
        say who();
        END
    sub in-dir($d) {
        my $p = run($*EXECUTABLE, '../shared.raku', :out, :!err, :cwd($d.Str),
                    :env(env-with({})));
        $p.out.slurp(:close).trim
    }
    my ($x1, $y1, $x2) = in-dir($x), in-dir($y), in-dir($x);
    ok($x1 eq 'X' && $y1 eq 'Y' && $x2 eq 'X',
       "the same script keeps its own meaning per directory (got $x1/$y1/$x2)");

    # the serializer itself, over a file with a bit of everything
    my $rt = run-rakupp('--ast-roundtrip', $ROOT.add('t/fixtures/native-parity.raku').Str);
    ok($rt[1] == 0, 'the AST survives a serialize/deserialize round trip');
}

# ---- the CLI surface ---------------------------------------------------
# Goldens for every command-line spelling that works today, written BEFORE
# the v3 option-parser refactor (docs/dev/plans/CLI-PLAN.md, step 1) so the
# refactor has something to be measured against. Each pin is a behavior to
# PRESERVE; the position-dependence bugs the refactor fixes are deliberately
# not pinned here.
section('the CLI surface (goldens for the v3 parser refactor)');
{
    # Like run-rakupp, but stderr is part of the contract here.
    sub run-rakupp-err(*@args) {
        my $p = run($*EXECUTABLE, |@args, :out, :err);
        ($p.out.slurp(:close), $p.err.slurp(:close), $p.exitcode)
    }
    my $work = $*TMPDIR.add("rakupp-cli-$*PID");
    mkdir $work;

    # -e, both spellings, and where the program's own args begin
    is(run-rakupp('-e', 'say 42')[0], "42\n", '-e CODE');
    is(run-rakupp('-esay 42')[0], "42\n", '-eCODE (glued)');
    is(run-rakupp('-e', 'say @*ARGS.join(",")', 'a', 'b')[0], "a,b\n",
       'args after the -e code reach @*ARGS');
    is(run-rakupp('-e', 'say @*ARGS.join(",")', '--', '-x')[0], "--,-x\n",
       'everything after the code is program args, -- included');

    # the perl line-loop clusters
    my $lines = $work.add('lines.txt'); $lines.spurt("a\nb\n");
    my $more  = $work.add('more.txt');  $more.spurt("c\n");
    is(run-rakupp('-n', '-e', 'say $_.uc', $lines.Str)[0], "A\nB\n", '-n -e loops over lines');
    is(run-rakupp('-ne', 'say $_.uc', $lines.Str)[0], "A\nB\n", '-ne cluster');
    is(run-rakupp('-pe', '$_ = $_ ~ "!"', $lines.Str)[0], "a!\nb!\n", '-pe prints $_ after the body');
    is(run-rakupp('-ne', 'say $_', $lines.Str, $more.Str)[0], "a\nb\nc\n", '-n merges multiple files');

    # the program arriving on stdin, both spellings
    {
        my $p = run($*EXECUTABLE, '-', :in, :out, :!err);
        $p.in.print("say 7\n"); $p.in.close;
        is($p.out.slurp(:close), "7\n", 'explicit - reads the program from stdin');
    }
    {
        my $p = run($*EXECUTABLE, :in, :out, :!err);
        $p.in.print("say 8\n"); $p.in.close;
        is($p.out.slurp(:close), "8\n", 'bare rakupp with stdin redirected runs it as a program');
    }

    # -I, both spellings
    my $mlib = $work.add('mlib'); mkdir $mlib;
    $mlib.add('CliM.rakumod').spurt: q:to/END/;
        unit module CliM;
        sub cli-m() is export { 'from-module' }
        END
    my $useprog = $work.add('use.raku');
    $useprog.spurt("use CliM;\nsay cli-m();\n");
    is(run-rakupp('-I', $mlib.Str, $useprog.Str)[0], "from-module\n", '-I PATH (separate)');
    is(run-rakupp("-I{$mlib.Str}", $useprog.Str)[0], "from-module\n", '-IPATH (attached)');

    # a script's flags are its own business
    my $argsee = $work.add('argsee.raku');
    $argsee.spurt("say \@*ARGS.join(',');\n");
    is(run-rakupp($argsee.Str, '--lint', '-h')[0], "--lint,-h\n",
       'flags after the program file pass through untouched');

    # --doc runs the program, then renders its pod
    my $podp = $work.add('pod.raku');
    $podp.spurt("=begin pod\nHello Pod\n=end pod\nsay 'ran';\n");
    is(run-rakupp('--doc', $podp.Str)[0], "ran\nHello Pod\n", '--doc renders POD after running');

    # help / version / ffi-info
    my ($ho, $hx) = run-rakupp('--help');
    ok($hx == 0 && $ho.contains('Usage:') && $ho.contains('--exe'), '--help prints usage, exit 0');
    ok(run-rakupp('-h')[0] eq $ho, '-h is --help');
    my ($vo, $vx) = run-rakupp('--version');
    ok($vx == 0 && $vo.starts-with('Raku++ (rakupp)'), '--version identifies itself');
    ok(run-rakupp('-V')[0] eq $vo, '-V is --version');
    my ($fo, $fx) = run-rakupp('--ffi-info');
    ok($fx == 0 && $fo.chars > 0, '--ffi-info answers');

    # the single-dash long-option courtesy
    {
        my ($o, $e, $x) = run-rakupp-err('-version');
        ok($x == 0 && $o.starts-with('Raku++ (rakupp)')
                   && $e.contains("treating '-version' as '--version'"),
           '-version is accepted as --version, with a note');
    }

    # -c: parse only
    is(run-rakupp('-c', '-e', 'say 42')[0], "Syntax OK\n", '-c -e, clean');
    my $cleanf = $work.add('clean.raku'); $cleanf.spurt("say 1;\n");
    is(run-rakupp('-c', $cleanf.Str)[0], "Syntax OK\n", '-c FILE, clean');
    {
        my ($o, $e, $x) = run-rakupp-err('-c', '-e', 'my $x = ;');
        ok($x == 2 && $e.contains('===SORRY!==='), '-c on a parse error: SORRY to stderr, exit 2');
    }

    # --lint: findings to stdout, summary to stderr, -q drops the summary
    {
        my ($o, $e, $x) = run-rakupp-err('--lint', '-e', 'my $x = 1;');
        ok($x == 1 && $o.contains('[unused-variable]') && $e.contains('1 warning'),
           '--lint warns, exit 1, summary on stderr');
        ($o, $e, $x) = run-rakupp-err('--lint', '-e', 'my $x = 1;', '-q');
        ok($x == 1 && $o.contains('[unused-variable]') && !$e.contains('warning'),
           '--lint -q keeps findings, drops the summary');
        ($o, $e, $x) = run-rakupp-err('--lint', '-e', 'say 1');
        ok($x == 0, '--lint on a clean program exits 0');
    }

    # --ast and its alias print the same tree
    my ($ao, $ax) = run-rakupp('--ast', '-e', 'say 1');
    ok($ax == 0 && $ao.chars > 0, '--ast prints a tree');
    ok(run-rakupp('--dump-ast', '-e', 'say 1')[0] eq $ao, '--dump-ast is the same tree');

    # --cpp emits C++
    my ($go, $gx) = run-rakupp('--cpp', '-e', 'say 1');
    ok($gx == 0 && $go.contains('#include'), '--cpp emits C++');

    # --highlight family: html default, ansi variant, flags compose either way
    my ($so, $sx) = run-rakupp('--highlight', '-e', 'say 42');
    ok($sx == 0 && $so.contains('<span'), '--highlight emits HTML by default');
    my $ansi = run-rakupp('--highlight', '--ansi', '-e', 'say 42')[0];
    ok($ansi.contains(27.chr), '--highlight --ansi emits ANSI');
    ok(run-rakupp('--ansi', '--highlight', '-e', 'say 42')[0] eq $ansi,
       'highlight flags compose in any order');
    ok(run-rakupp('--ansi', '-e', 'say 42')[0] eq $ansi, 'bare --ansi implies --highlight');

    # a mode with no source is a usage error, exit 4
    for <--exe --aot --bundle --ast --lint -c> -> $m {
        my ($o, $e, $x) = run-rakupp-err($m);
        ok($x == 4 && ($e.contains('Usage') || $o.contains('Usage')),
           "$m with no source: usage error, exit 4");
    }

    # the unknown-option banner (Rakudo-compatible: exit 0), and a missing file
    {
        my ($o, $e, $x) = run-rakupp-err('-Z');
        ok($x == 0 && $e.contains('Illegal option -Z'), 'unknown option: banner, exit 0');
        ($o, $e, $x) = run-rakupp-err('/no/such/file.raku');
        ok($x == 1 && $e.contains('Could not open'), 'missing file: error, exit 1');
        ($o, $e, $x) = run-rakupp-err('--precomp-modules=bogus');
        ok($x == 4 && $e.contains('Usage'), '--precomp-modules=bogus: usage error, exit 4');
    }

    # -M / -m — load a module before the program (v3 CLI step 3)
    {
        my @I = ('-I', $mlib.Str);
        is(run-rakupp(|@I, '-M', 'CliM', '-e', 'say cli-m()')[0], "from-module\n",
           '-M MODULE (separate)');
        is(run-rakupp(|@I, '-MCliM', '-e', 'say cli-m()')[0], "from-module\n",
           '-MMODULE (glued)');
        is(run-rakupp(|@I, '-m', 'CliM', '-e', 'say cli-m()')[0], "from-module\n",
           '-m is the Perl-style alias');
        $mlib.add('CliN.rakumod').spurt: q:to/END/;
            unit module CliN;
            sub cli-n() is export { 'second' }
            END
        is(run-rakupp(|@I, '-M', 'CliM', '-M', 'CliN', '-e', 'say cli-m() ~ cli-n()')[0],
           "from-modulesecond\n", '-M is repeatable');
        is(run-rakupp('-c', |@I, '-M', 'CliM', '-e', 'say cli-m()')[0], "Syntax OK\n",
           '-c composes with -I and -M');
        my $ml = $work.add('mlines.txt'); $ml.spurt("x\ny\n");
        is(run-rakupp(|@I, '-M', 'CliM', '-ne', 'say cli-m()', $ml.Str)[0],
           "from-module\nfrom-module\n", '-M sits outside the -n loop');
        my ($o, $e, $x) = run-rakupp-err('--ast', '-M', 'CliM', '-e', 'say 1');
        ok($x == 0 && $e.contains('Illegal option -M'),
           '--ast is a source tool: -M is illegal there');
        # line numbers must not shift: a parse error on line 2 still says line 2
        # (the use-prefix joins the program's own first line)
        my $errf = $work.add('lineno.raku');
        $errf.spurt("my \$x = 1;\nmy \$y = ;\n");
        ($o, $e, $x) = run-rakupp-err(|@I, '-M', 'CliM', $errf.Str);
        ok($e.contains('line 2'), '-M does not shift error line numbers');
    }
}

# ---- summary ----------------------------------------------------------
note "";
say "1..$count";
note $fails == 0 ?? "# all $count checks passed"
                 !! "# $fails of $count checks FAILED";
exit($fails ?? 1 !! 0);
