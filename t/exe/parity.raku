#!/usr/bin/env rakupp
# The `--exe` parity gate: a program compiled with `--exe` must behave as the
# same program interpreted — the same stdout, the same exit status.
#
# t/exe/run.raku compares `-O` with no `-O` and only COUNTS the binaries that
# disagree with the interpreter. Nothing failed on that axis, so it drifted:
# on 2026-11-11, 143 of the 1,557 programs in t/regression/ and examples/
# answered differently compiled, among them `exe-*` files whose own comments
# said they were checked both ways. This file makes it a failure.
#
#   rakupp t/exe/parity.raku               # t/regression + examples
#   rakupp t/exe/parity.raku FILE|DIR …    # just those
#   rakupp t/exe/parity.raku -j=4          # parallel workers (default 8)
#
# A compiled binary is its own `$*EXECUTABLE`, and `$*PROGRAM` is the binary.
# So that a program which spawns "the interpreter" or finds its fixtures beside
# `$*PROGRAM` can be compared too, the binary is built next to the source and
# run with RAKUPP_EXE_INTERPRETER naming rakupp, which a compiled binary's
# `$*EXECUTABLE` then answers (src/Runtime.cpp). A program that prints its own
# `$*PROGRAM` still differs, and is listed below.
#
# What is not compared:
#   - the nondeterministic ones in %skip;
#   - a run the interpreter itself does not finish inside the bound.
# A program whose binary cannot be BUILT is a failure: `--exe` must either
# compile a program or fall back to bundling it.
#
# t/exe/parity-known.txt lists the divergences not fixed yet, one file name
# per line. A listed program that diverges is reported and passes; one that
# now agrees is reported so the line can go. Anything else that diverges fails
# the gate. The list only shrinks.
#
# Exit 1 on a new divergence or a build failure.

my $ROOT = $*PROGRAM.parent.parent.parent;
my $rakupp = $*EXECUTABLE.Str;
my $jobs = 8;
my @args;
for @*ARGS { if /^ '-j=' (\d+) $/ { $jobs = +$0 } else { @args.push: $_ } }

my %skip =
    'life.raku' => 'random', 'parallel.raku' => 'threads', 'sleep-sort.raku' => 'threads',
    'echo-server.raku' => 'sockets', 'rand.raku' => 'random',
    'data-native-random.raku' => 'prints measured randomness statistics',
    'rakupp-upgrade.raku' => 'a filesystem-dependent skip line',
    # print measured timings (ratios), which differ run to run
    'lex-shadow-filter-exact.raku' => 'prints a timing ratio',
    'lookbehind-lexical-token-window.raku' => 'prints timings',
    'closed-write-handle-registry.raku' => 'prints a measured memory growth',
    # re-runs `$*EXECUTABLE $*PROGRAM-NAME`: compiled, that hands rakupp the binary as source
    'dynamic-var-memo.raku' => 'runs itself again through $*PROGRAM-NAME',
    ;

my %known = $*PROGRAM.parent.add('parity-known.txt').lines.map(*.trim).grep({ $_ && !.starts-with('#') }).map(* => True);

my @dirs = @args ?? @args.map(*.IO) !! ($ROOT.add('t/regression'), $ROOT.add('examples'));
my @files;
for @dirs -> $d {
    if $d.d    { @files.append: $d.dir.grep({ .extension eq 'raku' }).sort(*.Str) }
    elsif $d.f { @files.push: $d }
    else       { note "no such file or directory: $d"; exit 2 }
}

my %env = %*ENV;
%env<RAKUPP_EXE_INTERPRETER> = $rakupp;

# Every run is bounded and gets a closed stdin (see t/exe/run.raku: a program
# that reads stdin blocks forever on a terminal). A run cut off answers Nil.
sub bounded($limit, *@cmd, :$cwd) {
    my $p = Proc::Async.new(|@cmd, :w);
    my ($o, $e) = '', '';
    $p.stdout.tap({ $o ~= $_ });
    $p.stderr.tap({ $e ~= $_ });
    my $done = $p.start(:cwd($cwd // $*CWD), :ENV(%env));
    $p.close-stdin;
    await Promise.anyof($done, Promise.in($limit));
    unless $done { $p.kill: SIGKILL; my $ = try await $done; return (Nil, $o, $e) }
    ((try await $done).exitcode, $o, $e)
}

sub check($given) {
    my $f = $given.absolute.IO;
    my $name = $f.basename;
    return ('skip', $name, %skip{$name}) if %skip{$name};
    my $cwd = $f.parent;
    my ($irc, $iout) = bounded(30, $rakupp, $f.basename, :$cwd);
    return ('skip', $name, 'the interpreter did not finish') without $irc;
    my $bin = $cwd.add('.' ~ $name.subst(/'.raku'$/, '') ~ ".parity-$*PID");
    LEAVE { try $bin.unlink if $bin.defined && $bin.e }
    my ($brc, $, $berr) = bounded(180, $rakupp, '--exe', '-q', $f.basename, '-o', $bin.Str, :$cwd);
    unless ($brc // -1) == 0 && $bin.e {
        return ('buildfail', $name, $berr.lines.grep(/error|fatal/).head // $berr.lines.head // 'no binary');
    }
    my ($crc, $cout, $cerr) = bounded(60, $bin.Str, :$cwd);
    return ('differ', $name, 'the binary did not finish') without $crc;
    return ('agree', $name, '') if $crc == $irc && $cout eq $iout;
    my $first = ($iout.lines Z $cout.lines).first({ .[0] ne .[1] });
    my $why = $first ?? "interp: {$first[0].substr(0, 70)} | exe: {$first[1].substr(0, 70)}"
                     !! "exit $irc vs $crc; {$iout.lines.elems} vs {$cout.lines.elems} lines; {$cerr.lines.head // ''}";
    ('differ', $name, $why)
}

# (workers pulling from one queue: `.race` over these mostly-waiting calls ran
# them about one at a time)
my @queue = @files;
my @results;
my $lock = Lock.new;
await (^$jobs).map: {
    start {
        loop {
            my $f = $lock.protect({ @queue ?? @queue.shift !! Nil });
            last without $f;
            my $r = (try check($f)) // ('differ', $f.basename, "the gate could not run it: {$!.message.lines.head}");
            $lock.protect({ @results.push: $r });
        }
    }
};

my %n;
my @new;
my @fixed;
for @results.sort(*.[1]) -> ($kind, $name, $why) {
    %n{$kind}++;
    given $kind {
        when 'differ' | 'buildfail' {
            if %known{$name} { %n<known>++ }
            else { @new.push: $name; say "not ok - $name ({$kind eq 'buildfail' ?? 'does not build' !! 'differs'}): $why" }
        }
        when 'agree' { @fixed.push: $name if %known{$name} }
    }
}
say "now agrees, take it off t/exe/parity-known.txt: $_" for @fixed;

say "";
say "agree        {%n<agree> // 0}";
say "differ       {(%n<differ> // 0) + (%n<buildfail> // 0)}   ({%n<known> // 0} listed in parity-known.txt, {+@new} new)";
say "skipped      {%n<skip> // 0}";
exit @new ?? 1 !! 0;
