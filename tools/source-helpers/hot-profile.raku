#!/usr/bin/env rakupp
# Which interpreter functions do the perf-guard kernels spend their time in?
#
#   rakupp tools/source-helpers/hot-profile.raku [--rakupp=BIN] [--seconds=2] > tools/source-helpers/hot.txt
#
# Runs every kernel of tools/perf-guard.raku, repeated to last a few seconds,
# under macOS `sample` (one process at a time, one core), and prints one line per
# function: the largest share of any kernel's samples spent IN it (self) and
# UNDER it (inclusive), then its name as the splitter's plan matches it —
# `Interpreter::eval`, `applyArith`. A lambda counts as the function it is in.
# split.raku's `hot` line reads this list: the functions over its thresholds stay
# in one file, where they inline into each other.
#
# Profile the binary built from the ORIGINAL, unsplit sources: a split changes
# what inlines into what, and so what shows up here.

sub MAIN(Str :$rakupp = 'build-arm64/rakupp', Num() :$seconds = 2e0) {
    die "`sample` is macOS-only" unless '/usr/bin/sample'.IO.e;
    my $guard = 'tools/perf-guard.raku'.IO.slurp;
    my $blk = $guard.substr($guard.index('my %kernels ='));
    $blk = $blk.substr(0, $blk.index("\n;") // $blk.index(";\n\n"));
    my $tmp = $*TMPDIR.add("hot-profile-$*PID");
    $tmp.mkdir;
    my (%incl, %self);
    for $blk.match(/ ^^ \h* (\w+) \h* '=>' \s* \' (<-[']>*) \' /, :g) -> $m {
        my ($name, $code) = ~$m[0], ~$m[1];
        # how long one run takes, so the repeat count fills the sampling window
        my $t0 = now;
        run $rakupp, '-e', $code, :out, :err;
        my $rep = max 1, ($seconds * 1.3 / max(now - $t0, 0.001)).ceiling;
        my $prog = $tmp.add("$name.raku");
        spurt $prog, "for ^$rep \{\n$code\n\}\n";
        my $proc = Proc::Async.new($rakupp, $prog.Str);
        $proc.stdout.tap(-> $ {}); $proc.stderr.tap(-> $ {});
        my $done = $proc.start;
        my $pid = await $proc.ready;
        sleep 0.3;
        my $out = $tmp.add("$name.txt");
        run '/usr/bin/sample', ~$pid, ~$seconds.Int, '-mayDie', '-file', ~$out, :out, :err;
        await $done;
        note "$name x$rep";
        my ($inc, $slf, $total) = tally($out.lines);
        next unless $total;
        %incl{.key} = max(%incl{.key} // 0, .value / $total) for $inc.pairs;
        %self{.key} = max(%self{.key} // 0, .value / $total) for $slf.pairs;
    }
    run 'rm', '-rf', ~$tmp;
    for %incl.keys.sort({ -(%self{$_} // 0), $_ }) -> $k {
        say sprintf "%6.2f%% %6.2f%%  %s", 100 * (%self{$k} // 0), 100 * %incl{$k}, $k;
    }
}

# From one `sample` report: inclusive counts (the call graph), self counts (the
# top-of-stack table) and the total under `main`.
sub tally(@l) {
    my (%inc, %slf);
    my $total = 0;
    my $where = '';
    for @l -> $line {
        if $line.starts-with('Call graph:') { $where = 'graph'; next }
        if $line.starts-with('Total number in stack') { $where = ''; next }
        if $line.starts-with('Sort by top of stack') { $where = 'top'; next }
        if $where eq 'graph' && $line ~~ /^ <[\s+!:|]>* (\d+) \s+ (.+?) \s\s '(in ' (\S+?) ')'/ {
            my ($n, $name) = +$0, ~$1;
            $total = $n if $name eq 'main' && $n > $total;
            my $key = key($name) or next;
            %inc{$key} = $n if $n > (%inc{$key} // 0);
        }
        elsif $where eq 'top' && $line ~~ /^ \s+ (.+?) \s\s '(in ' \S+? ')' \s+ (\d+)/ {
            my $key = key(~$0) or next;
            %slf{$key} += +$1;
        }
    }
    (%inc, %slf, $total)
}

# `rakupp::Interpreter::eval(rakupp::Expr*)` -> `Interpreter::eval`
sub key(Str $n) {
    return Nil unless $n.starts-with('rakupp::');
    my $s = $n.substr(8).subst(/'(anonymous namespace)::'/, '');
    my $d = 0; my $cut = $s.chars;
    for $s.comb.kv -> $i, $c { $d++ if $c eq '<'; $d-- if $c eq '>'; if $c eq '(' && $d == 0 { $cut = $i; last } }
    $s.substr(0, $cut).subst(/'::$_' \d+ .*/, '').subst(/ '<' .* '>' $/, '')
}
