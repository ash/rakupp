# Regression: two things about where output goes, found together.
#
# 1. CONCURRENT WRITES. Every runtime write to the process's own streams now
#    takes one lock (rtOutMutex in Builtins.cpp). std::cout already guaranteed
#    the BYTES of a single `<<` would not interleave — libc++ writes through the
#    FILE*, which locks — but the stream's own state word is touched by every
#    sentry without synchronisation, so ThreadSanitizer reported a data race on
#    ANY program printing from two threads. Since v3.0.0 made parallel the
#    default, that is any program at all. A real lock rather than a suppression,
#    so the report goes away honestly and the NEXT race stays visible.
#
# 2. `printf` IGNORED `$*OUT`. It wrote std::cout directly, so
#    `my $*OUT = open(…); printf(…)` printed to the terminal while `say` on the
#    next line went to the file. Both the sub and the `.printf` method now go
#    through ioEmit, which is what honours the rebinding — and takes the lock.
#
# Contract: exit 0 + last line PASS.
my @fail;

# --- 1. concurrent output is whole-write atomic ------------------------------
# Each thread says one long uniform line; any line that is not 200 copies of a
# single character means two writes interleaved.
{
    my $tmp = $*TMPDIR.add("rakupp-concurrent-output-{$*PID}.txt");
    {
        my $fh = $tmp.open(:w);
        my $*OUT = $fh;
        await ^12 .map: -> $i { start { say ($i.base(16).lc x 200) } }
        $fh.close;
    }
    my @lines = $tmp.lines;
    @fail.push("expected 12 lines, got {@lines.elems}") unless @lines.elems == 12;
    for @lines -> $l {
        @fail.push("interleaved line: {$l.chars} chars, {$l.comb.unique.elems} distinct")
            unless $l.chars == 200 && $l.comb.unique.elems == 1;
    }
    $tmp.unlink;
}

# --- 2. printf honours a rebound $*OUT, in order -----------------------------
{
    my $tmp = $*TMPDIR.add("rakupp-printf-out-{$*PID}.txt");
    {
        my $fh = $tmp.open(:w);
        my $*OUT = $fh;
        say "one";
        "%s\n".printf("two-method");     # the .printf METHOD (invocant is the format)
        printf("%s\n", "three-builtin"); # the printf SUB
        say "four";
        $fh.close;
    }
    my $got = $tmp.slurp;
    my $want = "one\ntwo-method\nthree-builtin\nfour\n";
    @fail.push("printf/\$*OUT: got {$got.raku}, want {$want.raku}") unless $got eq $want;
    $tmp.unlink;
}

# --- and printing still works at all -----------------------------------------
{
    my @r = await ^8 .map: { start { 7 } };
    @fail.push("threads broke: {@r.raku}") unless @r.elems == 8 && all(@r.map: * == 7);
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
