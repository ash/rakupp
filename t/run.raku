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
sub golden(@args, Str $goldenfile, Str $desc) {
    my ($out, $exit) = run-rakupp(|@args);
    if $exit != 0 { ok(False, $desc); diag("non-zero exit: $exit"); return }
    unless $goldenfile.IO.e { ok(False, $desc); diag("missing golden: $goldenfile"); return }
    my $want = $goldenfile.IO.slurp;
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
    golden([$ROOT.add("examples/$name.raku").Str], $EXP.add("$name.out").Str, "example: $name");
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

# ---- summary ----------------------------------------------------------
note "";
say "1..$count";
note $fails == 0 ?? "# all $count checks passed"
                 !! "# $fails of $count checks FAILED";
exit($fails ?? 1 !! 0);
