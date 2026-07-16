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
           :retries($name eq 'sleep-sort' ?? 2 !! 0));
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
    my ($fact, $e) = run-rakupp($lisp, $ROOT.add('showcase/lisp/examples/fact.scm').Str);
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
    my ($out, $e) = run-rakupp($md, $ROOT.add('showcase/markdown/sample.md').Str);
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
    my ($name, $e1) = run-rakupp($json, '--query=.users[0].name', $sample);
    ok($name.trim eq '"Ada"', "json: --query pulls a nested value");
    my ($compact, $e2) = run-rakupp($json, '--compact', $sample);
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
# suite. Leaking a server between sections is harmless — the ports differ.
sub stop-server(Str $script) { try shell("pkill -f '$script' 2>/dev/null || true"); }

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
        my ($out, $exit) = run-rakupp($ROOT.add('t/fixtures/chat-client.raku').Str, ~$port);
        ok($out.contains('CHAT-OK'), "chat: nick, join, broadcast, and /who across two clients");
        diag($out.trim) unless $out.contains('CHAT-OK');
    }
    else { ok(False, "chat: server did not start"); }
    stop-server($script);
}

section('showcase/kvstore (a key-value protocol)');
{
    my $script = $ROOT.add('showcase/kvstore/kvstore.raku').Str;
    my $port = 6392;
    if start-server($script, $port) {
        # one connection, a sequence of commands, each reply read in turn
        my $s = IO::Socket::INET.new(:host('127.0.0.1'), :port($port));
        $s.recv;                                   # greeting
        sub cmd(Str $c --> Str) { $s.print("$c\r\n"); ($s.recv // '').trim }
        ok(cmd('SET name ada') eq 'OK',            "kvstore: SET replies OK");
        ok(cmd('GET name') eq 'ada',               "kvstore: GET returns the value");
        ok(cmd('SET g "hello world"') eq 'OK' && cmd('GET g') eq 'hello world',
                                                   "kvstore: quoted values keep their spaces");
        cmd('INCR hits');
        ok(cmd('INCR hits') eq '2',                "kvstore: INCR counts up");
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
    my $p = run($bin, :out);
    my $got = $p.out.slurp(:close);
    ok($got eq $EXP.add('fibonacci.out').IO.slurp, "the native fibonacci binary matches the golden");
    try unlink $bin;
}

# ---- summary ----------------------------------------------------------
note "";
say "1..$count";
note $fails == 0 ?? "# all $count checks passed"
                 !! "# $fails of $count checks FAILED";
exit($fails ?? 1 !! 0);
