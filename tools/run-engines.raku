#!/usr/bin/env raku
# Engine table — every kernel in tools/optbench/ run by every engine this repo
# has, side by side:
#
#   * rakupp      the interpreter, nothing turned on
#   * --cnp       copy-and-patch tier-up: the interpreter compiles its own hot
#                 loops from stencils carried inside the binary (docs/guide/JIT.md)
#   * --exe       transpiled to C++ and compiled, optimizer OFF
#   * --exe -O    the same, with the `-O` codegen passes ON
#   * rakudo      Rakudo/MoarVM, the outside reference
#
# tools/run-optbench.raku already answers one question about these kernels —
# what `-O` buys over plain `--exe`. This answers the other one: where each
# kernel actually lands across every way this project can run it, and what
# Rakudo does with the same program. The kernels are slow BY DESIGN (millions of
# iterations of one shape each), which is what makes the columns comparable at
# all: the loop dominates, so the numbers are about the loop.
#
# Dogfooded — run it with Raku++ (it also runs under Rakudo):
#
#     ./build/rakupp tools/run-engines.raku
#
# Usage:
#     tools/run-engines.raku [kernel ...]       # only these kernels
#     --engines=rakupp,cnp,exe,exeO,rakudo      # only these columns
#     --runs=N                                  # 1 warm-up + N-1 measured (default 4)
#     --no-check                                # skip the output-agreement gate
#
# Override binaries via env: RAKUPP=/path/to/rakupp RAKUDO=/path/to/rakudo

my $tools = $*PROGRAM.absolute.IO.parent;
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
my $repo  = $tools.parent;
my $bench = $tools.add('optbench');

# ---------------------------------------------------------------- arguments
my @filter;
my @want = <rakupp cnp exe exeO rakudo>;
my $RUNS  = (%*ENV<RUNS> // 4).Int;
my $check = True;
for @*ARGS -> $a {
    if    $a eq '--no-check'          { $check = False }
    elsif $a ~~ /^ '--runs=' (\d+) $/ { $RUNS = +$0 }
    elsif $a ~~ /^ '--engines=' (.+) $/ {
        @want = (~$0).split(',').map(*.trim).grep(*.chars);
        my @known = <rakupp cnp exe exeO rakudo>;
        for @want -> $e {
            unless $e (elem) @known {
                note "run-engines: unknown engine '$e'; known: @known.join(', ')";
                exit 2;
            }
        }
    }
    elsif $a.starts-with('-') { note "run-engines: unknown option $a"; exit 2 }
    else { @filter.push($a) }
}
$RUNS = 2 if $RUNS < 2;   # one warm-up is always discarded

# ------------------------------------------------------------ which binaries
# Same rule as every other measuring tool here: an explicit RAKUPP is honoured
# exactly, otherwise the search filters by ARCHITECTURE rather than taking the
# first path that exists. A translated binary runs fine at a uniform 1.7-2x, so
# nothing crashes and every number is simply wrong in the same direction.
my %PICK = pick-rakupp($repo);
require-native(%PICK, :tool<run-engines>,
    :consequence('Every column would be wrong in the same direction — nothing would crash.'));
my $RAKUPP = %PICK<path>;
note provenance-line('run-engines', %PICK);

# Which binary is Rakudo, and IS it Rakudo? On the machine of record `raku` is a
# symlink to build/rakupp, so a tool that reaches for `raku` measures this
# project against itself and prints a column of 1.00× that means nothing. Ask
# each candidate what it is before believing it.
sub banner(Str $bin --> Str) {
    my $p = try run($bin, '--version', :out, :err);
    return '' without $p;
    my $t = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    $p.exitcode == 0 ?? $t.lines.first(*.chars) // '' !! ''
}
my ($RAKUDO, $RAKUDO-BANNER, $rakudo-why);
{
    my $env = %*ENV<RAKUDO>;
    my @cand = $env.defined ?? ($env,) !! <rakudo raku>;
    for @cand -> $c {
        my $b = banner($c);
        next unless $b.chars;
        if $b.contains('Rakudo') { $RAKUDO = $c; $RAKUDO-BANNER = $b.trim; last }
        $rakudo-why //= "$c is not Rakudo — it says «{$b.trim}»";
    }
    without $RAKUDO {
        $rakudo-why //= "no Rakudo found (tried {@cand.join(', ')})";
    }
}
if 'rakudo' (elem) @want {
    if $RAKUDO { note "run-engines: rakudo {$RAKUDO} ({$RAKUDO-BANNER})" }
    else {
        note "run-engines: dropping the rakudo column — $rakudo-why";
        note "             Set RAKUDO=/path/to/rakudo to measure against it.";
        @want = @want.grep(* ne 'rakudo');
    }
}

# ------------------------------------------------------------------- kernels
# Read the directory rather than carrying a hand-written list. run-optbench
# carries one (each kernel there gets a hand-written note saying which pass it
# showcases) and has to cross-check it against disk every run, because a file
# dropped into tools/optbench/ was otherwise never run and nothing said so.
# A REPORTING tool has no reason to be able to drift: whatever is in the
# directory is what the table is about, and the note is the first sentence of
# the kernel's own comment header.
sub note-of(IO::Path $f --> Str) {
    my @head;
    for $f.lines -> $l {
        last unless $l.starts-with('#');
        @head.push($l.subst(/^ '#' \s? /, ''));
    }
    my $t = @head.join(' ').trim;
    $t ~~ /^ $<s>=[ .*? <[.!?]> ] [\s | $]/ ?? ~$<s> !! ($t || '(no description)');
}
my @kernels = $bench.dir.grep(*.extension eq 'raku').sort(*.basename).map: {
    %( name => .basename.subst(/'.raku' $/, ''), path => .Str, note => note-of($_) )
};
@kernels = @kernels.grep({ .<name> (elem) @filter }) if @filter;
unless @kernels {
    note "run-engines: no kernels matched {@filter.join(', ')} in $bench";
    exit 2;
}

# ------------------------------------------------------------------ plumbing

#| Best (minimum) wall-clock over the measured runs, in milliseconds. The
#| minimum, not the mean: a slower run had something else on the machine in it.
sub measure(@cmd --> Numeric) {
    my @t;
    for ^$RUNS -> $i {
        my $t0 = now;
        my $p = run(|@cmd, :out, :err);
        $p.out.slurp(:close); $p.err.slurp(:close);
        @t.push((now - $t0) * 1000) if $i > 0;
    }
    @t.min;
}

#| A command's stdout, trimmed — or Str:U when it exits non-zero, so a crash is
#| flagged rather than silently compared as empty output.
sub output-of(@cmd --> Str) {
    my $p = run(|@cmd, :out, :err);
    my $o = $p.out.slurp(:close); $p.err.slurp(:close);
    $p.exitcode == 0 ?? $o.trim !! Str;
}

#| Compile with the given extra flags; True on success. The seconds spent here
#| are added up and reported: they are a real cost of the two `--exe` columns
#| and appear nowhere in them, whereas --cnp's kernel builds (~15 µs each) are
#| inside its number.
my $compile-secs = 0;
sub compile(Str $path, Str $out, *@flags --> Bool) {
    my $t0 = now;
    my $p = run($RAKUPP, '--exe', |@flags, $path, '-o', $out, :out, :err);
    $p.out.slurp(:close); $p.err.slurp(:close);
    $compile-secs += now - $t0;
    $p.exitcode == 0;
}

#| What --cnp did with this program: it prints one line to STDERR under
#| `--cnp=stats`. Without it a 1.00× cnp column is unreadable — it means either
#| "the kernel bought nothing" or "there was no eligible loop to build", and
#| those are completely different answers.
sub cnp-stats(Str $path --> Hash) {
    my $p = run($RAKUPP, '--cnp=stats', $path, :out, :err);
    $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    my %s;
    if $e ~~ /'[cnp]' \s+ 'examined' \s+ (\d+) \s* ',' \s* 'eligible' \s+ (\d+) \s* ','
              \s* 'compiled' \s+ (\d+) \s* ',' \s* 'failed' \s+ (\d+) \s* ','
              \s* 'kernels entered' \s+ (\d+) / {
        %s = examined => +$0, eligible => +$1, compiled => +$2, failed => +$3, entered => +$4;
    }
    %s
}
sub cnp-word(%s --> Str) {
    return '—'        unless %s;
    return 'no loop'  if %s<examined> == 0;
    return 'refused'  if %s<compiled> == 0;
    return "{%s<compiled>} of {%s<examined>}" ~ (%s<entered> == 0 ?? '!' !! '');
}

my %CMD =
    rakupp => -> $path, %exe { [$RAKUPP, $path] },
    cnp    => -> $path, %exe { [$RAKUPP, '--cnp', $path] },
    exe    => -> $path, %exe { [%exe<base>] },
    exeO   => -> $path, %exe { [%exe<opt>] },
    rakudo => -> $path, %exe { [$RAKUDO, $path] };
my %LABEL = rakupp => 'rakupp', cnp => '--cnp', exe => '--exe',
            exeO => '--exe -O', rakudo => 'rakudo';

# The floor under every row: a program that does nothing, run the same five
# ways. Process start, runtime init and one `say` are in every cell of the
# table below, so the fast kernels are mostly this.
my $empty = $*TMPDIR.add("rakupp-engines-{$*PID}-empty.raku");
$empty.spurt("say 1;\n");
my %floor;
{
    my %exe = base => $*TMPDIR.add("rakupp-engines-{$*PID}-empty-base").Str,
              opt  => $*TMPDIR.add("rakupp-engines-{$*PID}-empty-O").Str;
    my $ok = True;
    $ok &&= compile($empty.Str, %exe<base>)      if 'exe'  (elem) @want;
    $ok &&= compile($empty.Str, %exe<opt>, '-O') if 'exeO' (elem) @want;
    for @want -> $e {
        next if !$ok && ($e eq 'exe' || $e eq 'exeO');
        %floor{$e} = measure(%CMD{$e}($empty.Str, %exe));
    }
    .IO.unlink for %exe<base>, %exe<opt>;
    $empty.unlink;
}

# --------------------------------------------------------------- measurement
my (@rows, @bad-rows);
for @kernels -> %k {
    # PID-scoped: two runs of this at once would otherwise compile over each
    # other's binaries and each measure the other's build.
    my %exe = base => $*TMPDIR.add("rakupp-engines-{$*PID}-%k<name>-base").Str,
              opt  => $*TMPDIR.add("rakupp-engines-{$*PID}-%k<name>-O").Str;
    my $compiled = True;
    $compiled &&= compile(%k<path>, %exe<base>)       if 'exe'  (elem) @want;
    $compiled &&= compile(%k<path>, %exe<opt>, '-O')  if 'exeO' (elem) @want;
    my @cols = @want.grep({ $compiled || ($_ ne 'exe' && $_ ne 'exeO') });

    # The agreement gate. Timings across engines mean nothing until the engines
    # are running the same program to the same answer, and --cnp especially —
    # it is a second code generator entering the loop partway through.
    my (@bad, %out);
    if $check {
        %out{$_} = output-of(%CMD{$_}(%k<path>, %exe)) for @cols;
        my $ref-key = %out<rakudo>.defined ?? 'rakudo' !! 'rakupp';
        my $ref     = %out{$ref-key};
        for @cols -> $e {
            without %out{$e} {
                @bad.push("%LABEL{$e} did not run");
                next;
            }
            @bad.push("%LABEL{$e} ≠ %LABEL{$ref-key}")
                if $ref.defined && $e ne $ref-key && %out{$e} ne $ref;
        }
    }

    my %t;
    unless @bad {
        %t{$_} = measure(%CMD{$_}(%k<path>, %exe)) for @cols;
    }
    my %cnp = ('cnp' (elem) @cols) ?? cnp-stats(%k<path>) !! %();
    .IO.unlink for %exe<base>, %exe<opt>;

    if @bad {
        @bad-rows.push: %( name => %k<name>, why => @bad.join('; ') );
        next;
    }
    @rows.push: %( name => %k<name>, note => %k<note>, :%t, :%cnp );
}

# -------------------------------------------------------------------- tables
sub ms($v) { $v.defined ?? sprintf('%.1fms', $v) !! 'n/a' }
sub rat($v) { $v.defined ?? sprintf('%.2f×', $v) !! 'n/a' }

my @show = @want.grep({ $_ ne 'rakudo' || $RAKUDO });
my $W = 11;

my $measured = $RUNS - 1;
my $with-cnp = 'cnp' (elem) @show;
my $w1 = 13 + $W * @show.elems + ($with-cnp ?? 14 !! 0);
say '';
say "wall clock, fastest of $measured run{$measured == 1 ?? '' !! 's'} after a discarded warm-up";
print sprintf('%-13s', 'kernel');
print sprintf("%{$W}s", %LABEL{$_}) for @show;
say $with-cnp ?? sprintf('%14s', 'cnp kernels') !! '';
say '-' x $w1;
for @rows -> %r {
    print sprintf('%-13s', %r<name>);
    print sprintf("%{$W}s", ms(%r<t>{$_})) for @show;
    say $with-cnp ?? sprintf('%14s', cnp-word(%r<cnp>)) !! '';
}
say '-' x $w1;
print sprintf('%-13s', 'say 1');
print sprintf("%{$W}s", ms(%floor{$_})) for @show;
say $with-cnp ?? sprintf('%14s', '') !! '';
say 'the last row is a program that does nothing: process start and runtime init are';
say 'in every cell above it, so the short rows are mostly that.';

# Ratios. Normalised to the rakupp interpreter, because that is the thing every
# other column is an alternative TO — including Rakudo, which is why its column
# is here rather than being the baseline.
if 'rakupp' (elem) @show && @show > 1 {
    my @r = @show.grep(* ne 'rakupp');
    say '';
    say 'the same, relative to the rakupp interpreter (higher = faster than it)';
    print sprintf('%-13s', 'kernel');
    print sprintf("%{$W}s", %LABEL{$_}) for @r;
    say '   what the kernel is';
    say '-' x (13 + $W * @r.elems + 3 + 46);
    for @rows -> %r {
        print sprintf('%-13s', %r<name>);
        for @r -> $e {
            my $v = (%r<t>{$e}.defined && %r<t><rakupp>.defined && %r<t>{$e} > 0)
                    ?? %r<t><rakupp> / %r<t>{$e} !! Numeric;
            print sprintf("%{$W}s", rat($v));
        }
        say '   ' ~ (%r<note>.chars > 46 ?? %r<note>.substr(0, 45) ~ '…' !! %r<note>);
    }
    print sprintf('%-13s', 'geom. mean');
    for @r -> $e {
        my @v = @rows.map({ (.<t>{$e}.defined && .<t><rakupp>.defined && .<t>{$e} > 0)
                            ?? .<t><rakupp> / .<t>{$e} !! Nil }).grep(*.defined);
        print sprintf("%{$W}s", @v ?? rat(exp(@v.map({ log($_) }).sum / @v.elems)) !! 'n/a');
    }
    say '';
}

say '';
if $with-cnp {
    say 'cnp kernels: compiled of examined loops; "refused" = a hot loop was seen and turned';
    say '             down, "no loop" = nothing countable (a `for` is not a `while`), and a';
    say '             trailing ! = built but never entered. `--cnp=verbose` says why.';
}
if $compile-secs > 0 {
    my $n = @show.grep({ $_ eq 'exe' || $_ eq 'exeO' }).elems;
    printf "the %s --exe column%s cost %.1fs of compiling that appears in %s;\n",
           ($n == 1 ?? 'one' !! 'two'), ($n == 1 ?? '' !! 's'), $compile-secs,
           ($n == 1 ?? 'nowhere in it' !! 'neither of them');
    say '--cnp builds its kernels inside the run it is timing.' if 'cnp' (elem) @show;
}
if @bad-rows {
    say '';
    say '⚠ OUTPUT MISMATCH — these kernels were skipped, their engines disagree:';
    for @bad-rows -> %r { say "   %r<name>: %r<why>" }
}
say '';
say "rakupp {%PICK<version>} at {%PICK<path>}"
  ~ ($RAKUDO ?? "   ·   {$RAKUDO-BANNER}" !! '');

exit 1 if @bad-rows;
