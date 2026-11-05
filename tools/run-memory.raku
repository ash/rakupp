#!/usr/bin/env rakupp
# Memory table — bytes per element on Raku++ and Rakudo (V6-PLAN P0: the table
# behind V6's third number, "memory per element at or below Rakudo's, on every
# row").
#
# Each row is a program that builds N values and keeps every one of them live
# until it prints. Its figure is the run's peak RSS above the same engine's
# empty program, divided by N. Peak RSS does not depend on the machine's load,
# so unlike the timing gates this one can be run on a busy machine.
#
#     ./build/rakupp tools/run-memory.raku
#     ./build/rakupp tools/run-memory.raku --only=objects,int-array --tsv=mem.tsv
#
# Options (also fine under Rakudo):
#     --only=name,name   measure just these rows
#     --tsv=PATH         write a machine-readable TSV: `# key=value` header
#                        lines, then one row per program with min and max
#                        bytes per element per engine
#     --runs=N           runs per row and engine, minimum taken (default 3)
#     --no-rakudo        Raku++ only
#
# Override the binaries via environment:
#     RAKUPP=/path/to/rakupp RAKUDO=rakudo ./build/rakupp tools/run-memory.raku
#
# Rakudo's figure moves between runs with the collector's timing (the
# 2026-09-26 survey read 312–526 B for one row), so the table prints the
# minimum and the TSV keeps the maximum beside it.

my $repo = $*PROGRAM.absolute.IO.parent.parent;
use lib $?FILE.IO.parent.add('lib').Str;
use Gate;

my $RAKUDO = %*ENV<RAKUDO> // 'rakudo';
my $tsv-path = '';
my @only;
my $runs = 3;
my $want-rakudo = True;
for @*ARGS -> $a {
    if    $a.starts-with('--tsv=')  { $tsv-path = $a.substr(6) }
    elsif $a.starts-with('--only=') { @only = $a.substr(7).split(',')>>.trim }
    elsif $a.starts-with('--runs=') { $runs = +$a.substr(7) }
    elsif $a eq '--no-rakudo'       { $want-rakudo = False }
    else { note "run-memory: unknown argument $a"; exit 2 }
}

my %PICK = pick-rakupp($repo);
require-native(%PICK, :tool<run-memory>,
    :consequence('A translated binary maps a second copy of the runtime, which inflates every row.'));
my $RAKUPP = %PICK<path>;
note provenance-line('run-memory', %PICK);

# `raku` may be rakupp on this machine, so the Rakudo lane runs only a binary
# that says it is Rakudo (as run-bench does).
if $want-rakudo {
    my $p = try run($RAKUDO, '--version', :out, :err);
    my $b = $p ?? $p.out.slurp(:close) ~ $p.err.slurp(:close) !! '';
    unless $b.contains('Rakudo') {
        note "run-memory: no Rakudo lane — $RAKUDO is not Rakudo; set RAKUDO=/path/to/rakudo";
        $want-rakudo = False;
    }
}

# The rows of V6-PLAN's memory table, in its order. Each program keeps what it
# builds and prints a count, which is checked against N so that a program that
# dies early cannot read as a small figure. `@N@` is replaced by N.
my @rows =
    %( :name<objects>,   :n(200_000),   :note('two-attribute objects'),
       :code('class P { has $.a; has $.b }; my @a; @a.push(P.new(a => $_, b => $_)) for ^@N@; say @a.elems') ),
    %( :name<hashes>,    :n(200_000),   :note('two-key hashes %(a => …, b => …)'),
       :code('my @a; @a.push(%(a => $_, b => $_)) for ^@N@; say @a.elems') ),
    %( :name<int-array>, :n(1_000_000), :note('my int @a, grown by push'),
       :code('my int @a; @a.push($_) for ^@N@; say @a.elems') ),
    # The same array built from a list in one step. On 2026-11-05 only this
    # form was packed (PACKED-ARRAY-PLAN): an empty `my int @a` grown by push
    # or by index stayed an array of Values.
    %( :name<int-array-list>, :n(1_000_000), :note('my int @a = ^N'),
       :code('my int @a = ^@N@; say @a.elems') ),
    %( :name<int-pairs>, :n(1_000_000), :note('Int-keyed Pairs'),
       :code('my @a; @a.push($_ => $_) for ^@N@; say @a.elems') ),
    %( :name<rats>,      :n(1_000_000), :note('Rats'),
       :code('my @a; @a.push(($_ + 1) / 7) for ^@N@; say @a.elems') ),
    %( :name<ints>,      :n(1_000_000), :note('Ints in an array'),
       :code('my @a; @a.push($_) for ^@N@; say @a.elems') ),
    %( :name<nums>,      :n(1_000_000), :note('Nums in an array'),
       :code('my @a; @a.push($_ + 0.5e0) for ^@N@; say @a.elems') ),
    %( :name<strs>,      :n(1_000_000), :note('short Strs'),
       :code('my @a; @a.push("s$_") for ^@N@; say @a.elems') ),
    %( :name<str-pairs>, :n(1_000_000), :note('Str-keyed Pairs'),
       :code('my @a; @a.push("k$_" => $_) for ^@N@; say @a.elems') ),
    %( :name<arrays>,    :n(1_000_000), :note('[$_, $_ + 1]'),
       :code('my @a; @a.push([$_, $_ + 1]) for ^@N@; say @a.elems') ),
    %( :name<int-str-hash>, :n(1_000_000), :note('a hash, Int → Str'),
       :code('my %h; %h{$_} = "v$_" for ^@N@; say %h.elems') );
@rows = @rows.grep({ .<name> (elem) @only }) if @only;
unless @rows {
    note "run-memory: --only matched no rows";
    exit 2;
}

# Peak RSS in bytes for ONE run of @cmd, read from /usr/bin/time, and the
# program's stdout. BSD `-l` reports bytes, GNU `-v` kilobytes: each is matched
# by its own wording, because the wrong unit is a 1024x error that still looks
# like a plausible figure (run-bench's rusage-run explains the same choice).
sub peak-rss(@cmd --> List) {
    my $gnu = !$*DISTRO.name.lc.contains('macos' | 'darwin');
    my $p = run('/usr/bin/time', $gnu ?? '-v' !! '-l', |@cmd, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    return (Nil, $out) unless $p.exitcode == 0;
    return (+$<b>, $out)        if $err ~~ / $<b>=[\d+] \s+ 'maximum resident set size' /;
    return (+$<k> * 1024, $out) if $err ~~ / 'Maximum resident set size (kbytes):' \s* $<k>=[\d+] /;
    (Nil, $out)
}

my $tmp = $*TMPDIR.add("run-memory-{$*PID}");
mkdir $tmp;
END { try { .unlink for $tmp.dir; $tmp.rmdir } }

my %engines = rakupp => $RAKUPP;
%engines<rakudo> = $RAKUDO if $want-rakudo;
my @lanes = %engines.keys.sort.reverse;   # rakupp first

# The floor each row is measured above: an empty program on the same engine.
my $empty = $tmp.add('empty.raku');
$empty.spurt("say 0;\n");
my %floor;
for @lanes -> $e {
    my @b = (^$runs).map({ peak-rss([%engines{$e}, ~$empty])[0] }).grep(*.defined);
    unless @b { note "run-memory: $e could not run an empty program"; exit 2 }
    %floor{$e} = @b.min;
}

my @results;
for @rows -> %r {
    my $f = $tmp.add("%r<name>.raku");
    $f.spurt(%r<code>.subst('@N@', %r<n>, :g) ~ "\n");
    my %per;
    for @lanes -> $e {
        my @b;
        for ^$runs {
            my ($rss, $out) = peak-rss([%engines{$e}, ~$f]);
            if !$rss.defined || $out.trim ne ~%r<n> {
                note "run-memory: %r<name> on $e did not print %r<n> — "
                   ~ ($out.trim ?? "it printed «{$out.trim.substr(0, 60)}»" !! 'it failed');
                @b = ();
                last;
            }
            @b.push: ($rss - %floor{$e}) / %r<n>;
        }
        %per{$e} = @b ?? { min => @b.min, max => @b.max } !! Nil;
    }
    @results.push: %( |%r, :%per );
}

sub fmt($v) { $v.defined ?? sprintf('%7.0f', $v) !! '    n/a' }
say "# peak RSS above an empty program, bytes per element — min of $runs runs";
say "# empty program: " ~ @lanes.map({ "$_ {(%floor{$_} / 1024 / 1024).round(0.1)} MB" }).join(', ');
say sprintf('%-13s %9s  %7s', 'row', 'N', 'rakupp') ~ ($want-rakudo ?? sprintf('  %7s  %6s', 'rakudo', 'ratio') !! '') ~ '  what';
for @results -> %r {
    my $a = %r<per><rakupp>; my $b = %r<per><rakudo>;
    my $line = sprintf('%-13s %9s  %s', %r<name>, %r<n>, fmt($a ?? $a<min> !! Nil));
    if $want-rakudo {
        my $ratio = ($a && $b && $b<min> > 0) ?? sprintf('%5.2f×', $a<min> / $b<min>) !! '   n/a';
        $line ~= "  {fmt($b ?? $b<min> !! Nil)}  $ratio";
    }
    say "$line  %r<note>";
}

if $tsv-path {
    my @t;
    @t.push: "# tool=run-memory";
    @t.push: "# rakupp={$RAKUPP} ({binary-version($RAKUPP)})";
    @t.push: "# rakudo={$RAKUDO}" if $want-rakudo;
    @t.push: "# runs=$runs";
    @t.push: "# floor-bytes=" ~ @lanes.map({ "$_:%floor{$_}" }).join(',');
    @t.push: (<row n>, @lanes.map({ "{$_}_min", "{$_}_max" })).flat.join("\t");
    for @results -> %r {
        @t.push: (%r<name>, %r<n>, @lanes.map({
            my $v = %r<per>{$_};
            $v ?? ($v<min>.round(0.1), $v<max>.round(0.1)) !! <n/a n/a>
        })).flat.join("\t");
    }
    $tsv-path.IO.spurt(@t.join("\n") ~ "\n");
    note "run-memory: wrote $tsv-path";
}
