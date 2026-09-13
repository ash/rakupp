# Regression: an `is rw` parameter passed on to another `is rw` parameter, and
# the frame in between (2026-09-13).
#
# 6b2a171 made a chain of `is rw` parameters alias the ORIGINAL container, so a
# closure writing the innermost one after every frame returned could reach the
# caller (IO::Capture::Simple). It did that by pointing the inner link past the
# intermediate frame — and the intermediate frame's own copy went stale:
# `c1($ip is rw) { c2($ip); $ip++ }` incremented the caller's variable once,
# not twice, because c1 never saw c2's write. The Forth showcase's recursive
# parser threads `$ip is rw` through every `: … ;` and `if … then`, so it lost
# its place after the first definition and died of "stack underflow".
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box has pointed at rakupp. Green on both.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the frame in between must see the inner write --------------------------
{
    sub c2($ip is rw) { $ip++ }
    sub c1($ip is rw) { c2($ip); $ip++ }
    my $n = 0; c1($n);
    ck($n, 2, 'one hop: the caller of the caller counts both increments');
}
{
    sub a($ip is rw, $d) { if $d > 0 { a($ip, $d - 1) }; $ip++ }
    my $n = 0; a($n, 2);
    ck($n, 3, 'a recursion threading one rw counter counts every level');
}
{
    sub inner3($p is rw) { $p = "inner" }
    sub mid3($p is rw)   { inner3($p); $p ~= "+mid" }
    sub outer3($p is rw) { mid3($p); $p ~= "+outer" }
    my $s = "start"; outer3($s);
    ck($s, "inner+mid+outer", 'three hops: every frame builds on the one below it');
}
{
    # a callback made in the outer frame reads the outer's parameter while the
    # inner is still running — the inner's write must already be visible
    sub tell($pos is rw, &cb) { $pos = 7; cb() }
    sub outer-cb($pos is rw) { tell($pos, { $pos }) }
    my $p = 0;
    ck(outer-cb($p), 7, 'a closure over the outer parameter sees the inner write mid-call');
    ck($p, 7, '…and the caller sees it after');
}

# --- the Forth parser's shape: a recursive descent on a shared cursor --------
{
    my @toks = <: sq dup * ; 5 sq .>;
    sub parse(@t, $ip is rw, @stop) {
        my @nodes;
        while $ip < @t.elems {
            my $tok = @t[$ip];
            last if $tok (elem) @stop;
            $ip++;
            if $tok eq ':' {
                my $name = @t[$ip++];
                my $body = parse(@t, $ip, [';']);
                $ip++;                                # consume `;`
                @nodes.push: "def:$name=" ~ $body.join(',');
            }
            else { @nodes.push: $tok }
        }
        @nodes;
    }
    my $ip = 0;
    my @got = parse(@toks, $ip, []);
    ck(@got.join(' '), "def:sq=dup,* 5 sq .",
       'a recursive parser resumes after the definition it descended into');
    ck($ip, 8, '…and the cursor ends at the token count');
}

# --- what 6b2a171 fixed must stay fixed: writes after the frames returned ----
{
    sub inner($y is rw) { return sub { $y = "inner-wrote" } }
    sub outer($x is rw) { return inner($x) }
    my $v = "initial";
    my &w = outer($v);
    w();
    ck($v, "inner-wrote", 'a closure two rw hops away still reaches the original after both returned');
}
{
    sub inner-app($y is rw) { return sub ($s) { $y ~= $s } }
    sub outer-app($x is rw) { $x = ""; return inner-app($x) }
    my $v;
    my &w = outer-app($v);
    w("a"); w("b");
    ck($v, "ab", '…and keeps writing through on every call');
}

say $fails ?? "FAILED $fails" !! "PASS";
exit($fails ?? 1 !! 0);
