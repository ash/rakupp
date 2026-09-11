#!/usr/bin/env raku
# Run Rakudo's own RakuAST construction tests here, and publish the counts.
#
#     rakupp tools/rakuast-t12.raku [--tsv=FILE] [--only=SUBSTR]
#
# WHY THIS IS NOT VENDORED. The round-trip property only ever feeds a renderer
# trees the view builder made; real consumers construct with `.new`, leave
# optional children unset and let defaults play. Rakudo's t/12-rakuast is that
# corpus, pre-oracled upstream — but this repo does not copy upstream suites, it
# references them: Roast is an external checkout behind `ROAST=` with zero files
# tracked, and RosettaCode programs are fetched into `rc-cache/` and never
# tracked (see .gitignore). This tool is the second shape. The repo carries the
# PIN, the file list and the measured counts — facts about upstream, not
# upstream's code — and a fetched file is reused forever.
#
# What is asserted here is P2c's requirement and no more: every file must
# **throw clearly or render, never crash**. The pass counts are a published
# baseline that improves with P3 (`.EVAL`) and P1 (`.AST`), not a gate — each
# file's `ast-ok` checks four things per case (`.DEPARSE`, `EVAL($ast)`,
# `EVAL($deparsed)` and an `EVAL(EVAL $ast.raku)` round trip), and only the
# first is in P2c's scope.

my $PIN   = '00fca760bb90c69b66a7229d60d61606728b5b71';   # rakudo/rakudo main, 2026-09-11
my $CACHE = $?FILE.IO.parent.parent.add("rc-cache/rakuast-t12/{$PIN.substr(0,12)}");

# The CONSTRUCTION-side files, the ones that build trees with `.new` rather than
# parsing them. The rest of the 47-file suite is about parsing, RakuDoc and the
# highlighter, which no step of this campaign has reached.
my @FILES = <
    literals.rakutest name.rakutest operators.rakutest call-name.rakutest
    call-method.rakutest var.rakutest block.rakutest sub.rakutest
    signature.rakutest statement.rakutest statement-mods.rakutest
    strings.rakutest terms.rakutest circumfix.rakutest postfix.rakutest
    pair.rakutest
>;

# `eval.rakutest` is on the plan's list of construction-side files and is NOT
# one — reading it is what settled that. It drives rakudo's own precompilation
# harness (`use lib <t/packages/Test-Helpers>`, `is-run`, RAKUDO_RAKUAST in the
# environment) and tests `Str.AST` inside a precompiling module, so it cannot
# run outside a rakudo checkout whatever this engine can do. Left out rather
# than counted as a failure it can never stop being.

my $tsv-arg = @*ARGS.first(*.starts-with('--tsv='));
my $only    = do with @*ARGS.first(*.starts-with('--only=')) { .substr(7) } else { Str };

# ---- fetch, once, and verify what came back ----------------------------
$CACHE.mkdir;
my @missing;
for @FILES -> $f {
    my $dest = $CACHE.add($f);
    next if $dest.e && $dest.s > 0;
    my $url = "https://raw.githubusercontent.com/rakudo/rakudo/$PIN/t/12-rakuast/$f";
    my $p = run('/usr/bin/curl', '-sS', '-m', '60', '-o', $dest.Str, $url, :out, :err);
    $p.out.slurp(:close); my $err = $p.err.slurp(:close);
    # A probe must be able to fail: an empty file, a GitHub error page or a
    # file with no RakuAST in it is NOT a test we can report a count for.
    # Silently running such a thing would publish a zero that looks like a
    # result. (Same rule the cross-engine probes carry.)
    unless $dest.e && $dest.s > 0 && $dest.slurp.contains("RakuAST") {
        @missing.push("$f: " ~ ($err.trim || 'fetched nothing that looks like the suite'));
        $dest.unlink if $dest.e;
    }
}
if @missing {
    note "rakuast-t12: could not fetch {+@missing} of {+@FILES} files at $PIN:";
    note "  $_" for @missing;
    note "";
    note "The cache is {$CACHE}; a fetched file is reused forever, so this needs";
    note "the network only once. Nothing is vendored on purpose — see the header.";
    exit 2;
}

# ---- run each file, and classify -----------------------------------------
# macOS has no timeout(1), and Proc::Async's broken promise has escaped `try`
# here before, so the wrapper is a shell script over synchronous `run`.
my $TO = $CACHE.add('timeout.sh');
$TO.spurt: Q:to/SH/;
#!/bin/sh
secs=$1; shift
"$@" </dev/null &
pid=$!
( sleep "$secs"; kill -9 "$pid" 2>/dev/null ) & killer=$!
wait "$pid"; rc=$?
kill "$killer" 2>/dev/null
exit $rc
SH
run('/bin/chmod', '+x', $TO.Str, :out, :err).out.slurp(:close);

my @rows;
for @FILES -> $f {
    next if $only.defined && !$f.contains($only);
    my $p = run($TO.Str, '60', $*EXECUTABLE.Str, $CACHE.add($f).Str, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    my $rc  = $p.exitcode;

    my $planned = do with $out.lines.first({ / ^ '1..' \d+ $ / }) { .substr(3).Int } else { 0 };
    my $ok      = +$out.lines.grep({ .starts-with('ok ') });
    my $notok   = +$out.lines.grep({ .starts-with('not ok ') });
    # `kill -9` reports 137; a C++ abort or segfault reports 134/139. Those are
    # the crash the phase forbids — a clean non-zero exit is a THROW, which is
    # what "throw clearly" means and is allowed.
    my $status = $rc == 137            ?? 'timeout'
              !! $rc == 134 || $rc == 139 || $rc == 138 || $rc == 136 ?? 'CRASH'
              !! $rc == 0              ?? 'ran'
              !!                          'threw';
    my $first = ($err.lines.first({ .trim.chars }) // $out.lines.first({ .starts-with('not ok') }) // '').trim;
    @rows.push: [$f, $status, $planned, $ok, $notok, $first.substr(0, 160)];
}

my $out-lines = join "\n",
    "file\tstatus\tplan\tok\tnot-ok\tfirst-error",
    @rows.map(*.join("\t"));
with $tsv-arg { $_.substr(6).IO.spurt($out-lines ~ "\n") } else { say $out-lines }

my %by-status;
%by-status{.[1]}++ for @rows;
note "";
note "rakuast-t12 \@ {$PIN.substr(0,12)} — {+@rows} files: "
   ~ %by-status.sort(*.key).map({ "{.key} {.value}" }).join(', ');
note "assertions: {@rows.map(*.[3]).sum} ok, {@rows.map(*.[4]).sum} not ok, "
   ~ "of {@rows.map(*.[2]).sum} planned";
# The phase's actual requirement, and the only thing this tool FAILS on.
my $crashes = @rows.grep(*.[1] eq 'CRASH');
if $crashes {
    note "";
    note "CRASHED (P2c forbids this — a shape must throw clearly, not abort):";
    note "  {.[0]}: {.[5]}" for $crashes;
    exit 1;
}
note "no crashes — every file either ran or threw clearly";
