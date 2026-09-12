# Regression: two general bugs found working Anton Antonov's ecosystem
# (2026-09-12). Both were blockers with real reach — IO::Capture::Simple carries
# 70 not-yet-green dists across the whole ecosystem, Clipboard 7 of his.
#
# Expectations checked against Rakudo 2026.08 via /opt/homebrew/bin/raku — NOT
# the bare name `raku`, which on this box is a symlink that has pointed at
# rakupp. Green on both engines.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- an `is rw` parameter passed ON stays an alias of the ORIGINAL ---------
# IO::Capture::Simple's capture_on($out is rw) hands $out to
# capture_stdout_on($x is rw), which installs a $*OUT whose .print closes over
# it. The inner link pointed at the OUTER frame's copy, and outer's own
# copy-back had already run by the time the closure wrote — so the caller never
# saw a thing. Collapse the hop at bind time.
{
    sub inner($y is rw) { return sub { $y = "inner-wrote" } }
    sub outer($x is rw) { return inner($x) }
    my $v = "initial";
    my &w = outer($v);
    w();
    ck($v, "inner-wrote", 'an rw param passed on aliases the original container');
}
{
    sub setup($x is rw) { $x = ""; return sub ($s) { $x ~= $s } }
    my $v;
    my &w = setup($v);
    w("a"); w("b");
    ck($v, "ab", 'a closure over an rw param keeps writing through after the return');
}
{   # control: a write DURING the call still lands, as it always did
    sub during($x is rw) { $x = "during" }
    my $v = "initial"; during($v);
    ck($v, "during", 'a write before the return is unaffected');
}

# --- `$x ~~ "text"` is Str.ACCEPTS: what the topic SAYS it is --------------
# Clipboard picks its backend with `when $_ ~~ 'macos'`, so on macOS it fell
# through to the Linux branch and shelled out to xclip.
{
    class P { method Str { "pval" } }
    ck((P.new ~~ 'pval'),  True,  'an object smartmatches a string by its .Str');
    ck((P.new ~~ 'other'), False, '…and a different string still fails');
    ck((P.new eq 'pval'),  True,  'the same question through eq');
}
{
    # $*DISTRO / $*KERNEL / $*VM are tagged hashes whose .Str is a method
    ck(($*DISTRO ~~ $*DISTRO.Str), True, '$*DISTRO smartmatches its own .Str');
    ck(($*KERNEL ~~ $*KERNEL.Str), True, '…and so does $*KERNEL');
    ck(($*VM     ~~ $*VM.Str),     True, '…and $*VM');
    ck(($*DISTRO ~~ 'definitely-not-a-distro'), False, 'a wrong name still answers False');
}
{   # control: plain strings are untouched
    ck(('abc' ~~ 'abc'), True,  'a plain string still matches itself');
    ck(('abc' ~~ 'abd'), False, '…and not a different one');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
