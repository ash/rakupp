#!/usr/bin/env raku
# The --target=js corpus gate (TRANSPILE-PLAN.md, gate 2).
#
# Every program in t/regression/ and examples/ is transpiled with
# `rakupp --target=js --standalone`. Each in-core program then runs under the
# JavaScript host and is compared byte for byte — stdout, stderr, exit status —
# with the SAME binary interpreting it: the interpreter is the oracle, not a
# golden. The report has three numbers:
#
#   in-core and agreeing   the one a stranger re-measures
#   refused                with the histogram of reasons — the work queue
#   disagreeing            must be 0
#
# Also checked: src/JsRuntimeSrc.cpp is current with src/js-rt/*.js, and
# every builtin the emitter accepts (kBuiltins in src/codegen/Js.cpp) exists
# in the runtime.
#
#   rakupp t/js/run.raku                 # the whole corpus
#   rakupp t/js/run.raku examples        # one directory (or files)
#   RAKUPP_JS=bun rakupp t/js/run.raku   # another host
#
# Exit 1 when anything disagrees, the embedded runtime is stale, or the
# builtin tables drift.

my $ROOT = $*PROGRAM.parent.parent.parent;
my $rakupp = $*EXECUTABLE;
my $host = %*ENV<RAKUPP_JS> // 'node';
my $tmp = $*TMPDIR.add("rakupp-js-gate-$*PID");
$tmp.mkdir;
END { for $tmp.dir { .unlink }; $tmp.rmdir if $tmp.e }

# programs that cannot be judged by comparison
my %skip =
    'examples/life.raku'  => 'random seed',
    'examples/parallel.raku' => 'threads', 'examples/sleep-sort.raku' => 'threads', 'examples/echo-server.raku' => 'sockets';

my @args = @*ARGS;
my @dirs = @args ?? @args.map(*.IO) !! ($ROOT.add('t/regression'), $ROOT.add('examples'));
my @files;
for @dirs -> $d {
    if $d.d { @files.append: $d.dir.grep({ .extension eq 'raku' }).sort(*.Str) }
    elsif $d.f { @files.push: $d }
    else { note "no such file or directory: $d"; exit 2 }
}

my $bad = 0;

# --- the embedded runtime is current ---------------------------------------
{
    my $p = run $rakupp.Str, $ROOT.add('tools/js/gen-rt-src.raku').Str, '--check', :out, :err;
    if $p.exitcode != 0 {
        say "not ok - src/JsRuntimeSrc.cpp is stale (run: rakupp tools/js/gen-rt-src.raku)";
        $bad++;
    }
    else { say "ok - embedded JS runtime is current" }
}

# --- the emitter's builtin table matches the runtime -------------------------
{
    my $cpp = $ROOT.add('src/codegen/Js.cpp').slurp;
    my $block = $cpp.substr($cpp.index('const std::set<string> kBuiltins = {'));
    $block = $block.substr(0, $block.index('};'));
    my @names = $block.comb(/ '"' <-["]>+ '"' /).map(*.substr(1, *-1));
    my $rt = $tmp.add('rt.js');
    run $rakupp.Str, $ROOT.add('tools/js/gen-rt-src.raku').Str, '--js', $rt.Str;
    my $check = $tmp.add('check.js');
    $check.spurt: $rt.slurp ~ "\nconst names = " ~ '[' ~ @names.map({ '"' ~ $_ ~ '"' }).join(',') ~ "];\n"
        ~ 'const missing = names.filter(n => typeof R[n] !== "function" && R[n] === undefined); if (missing.length) { console.log("missing: " + missing.join(" ")); process.exit(1); }' ~ "\n";
    my $p = run $host, $check.Str, :out, :err;
    if $p.exitcode != 0 {
        say "not ok - builtins the emitter accepts but the runtime lacks: " ~ $p.out.slurp(:close).trim;
        $bad++;
    }
    else { say "ok - the emitter's {@names.elems} builtins all exist in the runtime" }
}

# --- the corpus --------------------------------------------------------------
sub run-capped(*@cmd, :$cwd) {
    # stdin closed, a 30 s cap; a process-group shim so a hung child dies with the alarm
    my $out = $tmp.add('o'), my $err = $tmp.add('e');
    my $sh = 'perl -e \'alarm 30; exec @ARGV\' ' ~ @cmd.map({ "'" ~ .subst("'", "'\\''", :g) ~ "'" }).join(' ')
           ~ " < /dev/null > '$out' 2> '$err'";
    my $p = run 'sh', '-c', $sh, :cwd($cwd // $ROOT.Str);
    my $code = $p.exitcode;
    return ($code, $out.slurp, $err.slurp);
}

my (@agree, @disagree, @skipped);
my %refused;      # reason → [names]
for @files -> $f {
    my $rel = $f.relative($ROOT);
    if %skip{$rel} { @skipped.push("$rel ({%skip{$rel}})"); next }
    my $js = $tmp.add($f.basename.subst(/\.raku$/, '.js'));
    my $tr = run $rakupp.Str, '--target=js', '--standalone', '-q', $f.Str, '-o', $js.Str, :out, :err;
    if $tr.exitcode != 0 {
        my $why = $tr.err.slurp(:close).lines.first(*.starts-with('note: ')) // 'transpile failed';
        $why = $why.substr(6) if $why.starts-with('note: ');
        $why = $why.substr(0, $why.index(' — outside')) if $why.index(' — outside').defined;
        $why = $why.subst(/\s*'(line '\d+')'/, '');
        $why = $why.subst(/"'" <-[']>* "'"/, "'…'");   # bucket names together
        %refused{$why}.push($rel);
        next;
    }
    my ($xj, $oj, $ej) = run-capped($host, $js.Str);
    my ($xi, $oi, $ei) = run-capped($rakupp.Str, $f.Str);
    if $xj == $xi && $oj eq $oi && $ej eq $ei { @agree.push($rel) }
    else {
        my @why;
        @why.push("exit $xi vs $xj") if $xi != $xj;
        @why.push("stdout differs ({$oi.chars} vs {$oj.chars} chars)") if $oi ne $oj;
        @why.push("stderr differs ({$ei.chars} vs {$ej.chars} chars)") if $ei ne $ej;
        my $first = $ej.lines.first // '';
        @disagree.push("$rel: {@why.join('; ')}" ~ ($first ?? " — $first.substr(0, 100)" !! ''));
    }
}

my $n = @files.elems - @skipped.elems;
my $refused = %refused.values.map(*.elems).sum;
say "";
say "js gate ($host): {@agree.elems} in-core and agreeing of $n programs; $refused refused; {@disagree.elems} disagreeing";
say "  skipped: " ~ @skipped.join(', ') if @skipped;
if %refused {
    say "refused (the histogram is the work queue):";
    for %refused.sort({ -.value.elems, .key }) -> $p {
        say sprintf("  %4d  %s", $p.value.elems, $p.key);
    }
}
if @disagree {
    say "DISAGREEING (must be 0):";
    say "  $_" for @disagree;
    $bad++;
}
say $bad ?? "JS GATE FAILED" !! "ALL JS GATE CHECKS PASSED";
exit $bad ?? 1 !! 0;
