#!/usr/bin/env rakupp
# FastRoast's fattened Roast, part by part, through tools/run-roast.raku.
#
# FastRoast (github.com/ash/fastroast) runs the very same test suite with
# mangled characters. Its fat/ directory holds one tree per transform —
# strings-v1.mixed/, names-v1.mixed/, ops-v1/ — and each tree holds the files
# of Roast's spectest.data that transform changed, at their Roast paths: the
# Roast file as it is, `#?` directives included, with only the transform's
# edits applied. This runs every tree as run-roast runs Roast, with Roast
# itself beside them as the baseline (the part `roast`), under one engine or
# several, and compares the outcomes file by file.
#
# Nothing here touches the fudge directives. A tree's file reaches run-roast
# the way Roast's own file does, so run-roast's rules apply to it unchanged:
# rakupp's lexer applies the #?rakudo directives itself, and a foreign engine
# gets Roast's `fudge` first (run-roast's --fudge, default rakudo.moar).
#
# A part cannot be ROAST by itself. Its tree has the .t files and nothing else:
# about 300 of them find their helpers through their own location —
# `use lib $*PROGRAM.parent(2).add("packages/Test-Helpers")` — and others read
# fixtures that sit beside them. So each part runs from an overlay: the Roast
# checkout copied without .git, the part's tree laid over it, and a
# spectest.data that is Roast's own cut down to the part's files — the same
# lines, markers included. Only files Roast's spectest.data lists ever run, and
# --skip-marker keeps out of a part what it keeps out of a Roast run. Roast's
# other tests stay in the overlay as fixtures. The overlay is restored with
# rsync before every engine's run (an unchanged one costs a second or two), so
# what one run's tests wrote beside themselves is gone before the next.
#
# Each run's own output goes to OVERLAY/lists/PART.ENGINE.log, with --failed
# on, and each file's outcome is read from the run: PASS from the --list file,
# and from the --failed block `part 13/19`, `TIME 3/10`, `noTAP` or `LOST`. A
# fattened test can fail on every engine — a sort over `mixed`-palette strings
# is no longer sorted — so a part's pass count says little alone. What this
# prints after the tally:
#
#   - per engine, the files that fully pass in Roast but not fattened, given
#     the baseline: what the fat cost that engine;
#   - given two or more engines, the files whose outcome differs between the
#     first engine and each other one, each marked `fat` when the engines agree
#     on the plain Roast file and `base` when they disagree there too (the
#     ordinary conformance gap, which the fat did not cause).
#
# For test-by-test verdicts and a shrunk reproducer, use FastRoast's own
# `fastroast run`.
#
# Usage:
#   build/rakupp tools/run-fatroast.raku [OPTION ...] [PATTERN ...]
#   build/rakupp tools/run-fatroast.raku --help
#
# Examples:
#   nice build/rakupp tools/run-fatroast.raku -j2
#   nice build/rakupp tools/run-fatroast.raku -j2 --engine=build/rakupp,rakudo
#   build/rakupp tools/run-fatroast.raku --parts=roast,ops-v1 S03-operators
#   build/rakupp tools/run-fatroast.raku --parts=strings-v1 S32-str

my $RUN-ROAST = $?FILE.IO.parent.add('run-roast.raku').absolute;
my $HOME      = %*ENV<HOME> // '.';
my $FASTROAST = %*ENV<FASTROAST> // "$HOME/fastroast";
my $OVERLAY   = %*ENV<FATROAST_OVERLAY> // "$HOME/roast-fat";
my $ROAST     = (%*ENV<ROAST> // "$HOME/roast").IO.absolute;
my $BASE      = 'roast';     # the baseline part: Roast as it is
my @want-parts;
my @want-engines;
my %skip-marker;
my @pass;        # handed to run-roast as they came: options and PATTERNs
my @patterns;    # the PATTERNs alone, to select what a part will run
my $VERBOSE = False;
my $HELP = False;
for @*ARGS -> $a {
    if $a ~~ /^ '--parts=' (.+) $/ {
        @want-parts.append: (~$0).split(',').map(*.trim).grep(*.chars);
    }
    elsif $a ~~ /^ '--engine=' (.+) $/ {
        @want-engines.append: (~$0).split(',').map(*.trim).grep(*.chars);
    }
    elsif $a ~~ /^ '--fastroast=' (.+) $/ {
        $FASTROAST = ~$0;
    }
    elsif $a ~~ /^ '--overlay=' (.+) $/ {
        $OVERLAY = ~$0;
    }
    elsif $a eq '-v' || $a eq '--verbose' {
        $VERBOSE = True;
    }
    elsif $a eq '-h' || $a eq '--help' {
        $HELP = True;
    }
    elsif $a ~~ /^ '--skip-marker=' (.+) $/ {
        %skip-marker{$_} = True for (~$0).split(',').map(*.trim).grep(*.chars);
        @pass.push($a);
    }
    elsif $a eq '--failed' {
        # always on: the outcomes of the files that do not fully pass are read
        # from its block
    }
    elsif $a.starts-with('--list=') || $a.starts-with('--failed=') {
        note "run-fatroast: $a — the lists are this script's, one per part and engine under OVERLAY/lists";
        exit 2;
    }
    elsif $a.starts-with('--times=') && $a ne '--times=' {
        note "run-fatroast: $a — one file would be read and rewritten by every part and engine in turn";
        exit 2;
    }
    elsif $a eq '--all' || $a.starts-with('--suite=') {
        note "run-fatroast: $a — it would not run the part (--all runs the whole overlay, --suite another suite)";
        exit 2;
    }
    else {
        @pass.push($a);
        @patterns.push($a) unless $a.starts-with('-');
    }
}
$FASTROAST = $FASTROAST.IO.absolute;
$OVERLAY   = $OVERLAY.IO.absolute;
my $FAT    = $FASTROAST.IO.add('fat');

# fat/'s trees, one directory per transform and palette (`strings-v1.mixed`,
# `ops-v1`), in the order fat/README.md's table lists them. A fat case is the
# transform, everything before the first dot: `strings-v1` is every palette's
# tree of it. What each tree rewrites is its row of that table (`| [`ops-v1/`]
# (ops-v1/) | … |`), so a tree FastRoast adds shows up in --help in its own words.
sub fat-about(--> List) {
    my $readme = $FAT.add('README.md');
    return () unless $readme.e;
    $readme.lines.map(-> $l {
        $l ~~ /^ '| [`' (<-[`]>+?) '/`]' <-[|]>* '|' \s* (<-[|]>+?) \s* '|' /
            ?? (~$0 => (~$1).subst('`', '', :g)) !! Empty
    }).List
}
sub fat-trees(--> List) {
    my @order = fat-about().map(*.key);
    dir($FAT).grep(*.d).map(*.basename).sort({ (@order.first($_, :k) // Inf), $_ }).List
}
sub case-of(Str $tree --> Str) { $tree.split('.')[0] }

# --help's list of what --parts takes: the baseline, then each fat case with
# its file count and what it rewrites; a case with several palettes lists each
# palette's tree under it.
sub parts-help(--> Str) {
    return "Fat cases\n  (no $FAT: point --fastroast or \$FASTROAST at a FastRoast checkout)\n"
        unless $FAT.d;
    my %about = fat-about();
    my @trees = fat-trees();
    my %n = @trees.map({ $_ => t-files($FAT.add($_)) });
    my @rows = ($BASE, %n.values.map(*.Slip).unique.elems,
                'Roast as it is, the baseline: every file the cases change'),;
    for @trees.map(&case-of).unique -> $case {
        my @t = @trees.grep({ case-of($_) eq $case });
        if @t == 1 {
            @rows.push: ($case, %n{@t[0]}.elems, %about{@t[0]} // '');
        }
        else {
            @rows.push: ($case, @t.map({ %n{$_}.Slip }).unique.elems, 'one tree per palette:');
            @rows.push: ("  $_", %n{$_}.elems, %about{$_} // '') for @t;
        }
    }
    my $w = (|@rows.map(*[0].chars), 4).max;
    my $out = "Fat cases, for --parts=CASE[,CASE] (default: all of them, roast first)\n";
    for @rows -> ($name, $files, $about) {
        my $lead = sprintf("  %-{$w}s  %5d  ", $name, $files);
        my $line = $lead;
        for $about.words -> $word {
            if $line.chars > $lead.chars && $line.chars + 1 + $word.chars > 78 {
                $out ~= $line ~ "\n";
                $line = ' ' x $lead.chars;
            }
            $line ~= ($line.chars > $lead.chars ?? ' ' !! '') ~ $word;
        }
        $out ~= $line.trim-trailing ~ "\n";
    }
    $out ~ "A tree's own name (strings-v1.mixed) picks one palette of a case. Narrow\n"
         ~ "further with PATTERNs, substrings of a file's Roast path: S32-str,\n"
         ~ "S03-operators/set.\n"
}

if $HELP {
    print qq:to/END/;
    Usage: build/rakupp tools/run-fatroast.raku [OPTION ...] [PATTERN ...]

    Runs every tree of FastRoast's fattened Roast through tools/run-roast.raku,
    each from an overlay of the Roast checkout, with Roast itself as the
    baseline part `roast`, and compares the outcomes file by file: per engine,
    the files that pass in Roast and not fattened; given two engines, the files
    whose outcome differs, marked fat or base. The trees keep Roast's #?
    directives, and run-roast applies them by its usual rules.

      fastroast  $FASTROAST
      roast      $ROAST
      overlays   $OVERLAY

    Options
      --parts=C[,C]      only these fat cases (below) and `roast`, the
                         baseline (default: all of them, roast first)
      --engine=E[,E]     run each part under each engine, a path or a name on
                         PATH; the first is the one the others are compared
                         with (default: the engine running this file)
      --fastroast=DIR    the FastRoast checkout (\$FASTROAST,
                         default \$HOME/fastroast)
      --overlay=DIR      where the overlays, logs and lists go
                         (\$FATROAST_OVERLAY, default \$HOME/roast-fat)
      -v, --verbose      show each run-roast's output as well as logging it
      -h, --help         print this and exit

    END
    print parts-help();
    print qq:to/END/;

    Anything else goes to run-roast as it is: -j=N, --skip-marker=WORD,
    --fudge=IMPL, `--times=` (no file), PATTERNs. Only files Roast's
    spectest.data lists run. --list, --failed=FILE, --times=FILE, --all and
    --suite are refused: the lists are this script's, one times file cannot
    serve every part and engine, and the other two would not run the part.

    Environment
      ROAST              the Roast checkout the overlays copy (default
                         \$HOME/roast); fat/README.md names the revision the
                         trees were made from

    Output, in OVERLAY/lists/
      PART.ENGINE.log            run-roast's output for that run
      PART.ENGINE.list           its fully-passing files (+ .meta)
      PART.ENGINE.lost           files fully passing in Roast, not in PART
      PART.A-vs-B.diff           files whose outcome differs, fat or base
    END
    exit 0;
}

unless $FAT.d {
    note "run-fatroast: no $FAT — point --fastroast (or \$FASTROAST) at a FastRoast checkout";
    exit 2;
}
unless $ROAST.IO.add('spectest.data').e {
    note "run-fatroast: $ROAST is not a Roast checkout — set ROAST";
    exit 2;
}

# Roast's spectest.data, read the way run-roast reads it: a path, then
# optionally `#` and marker words. Each path keeps its whole line, so an
# overlay's list carries the same markers.
my %LINE;       # path -> its line in Roast's spectest.data
my %MARKERS;    # path -> its marker words
for $ROAST.IO.add('spectest.data').lines -> $line {
    my $l = $line.trim;
    next if $l eq '' || $l.starts-with('#');
    my $hash = $l.index('#');
    my $path = ($hash.defined ?? $l.substr(0, $hash) !! $l).trim;
    next if $path eq '';
    %LINE{$path}    = $line;
    %MARKERS{$path} = $hash.defined ?? $l.substr($hash + 1).words.List !! ();
}

# The parts: the baseline, then fat/'s trees. --parts names fat cases
# (strings-v1: every palette's tree of it) or single trees (strings-v1.mixed);
# whatever order it gives, the baseline runs first.
my @trees = fat-trees();
my @parts = $BASE, |@trees;
if @want-parts {
    my %want;
    for @want-parts -> $w {
        my @hit = $w eq $BASE ?? ($BASE,) !! @trees.grep({ $_ eq $w || case-of($_) eq $w });
        unless @hit {
            note "run-fatroast: no fat case '$w' (there are: "
               ~ "{($BASE, |@trees.map(&case-of).unique).join(', ')}; --help lists them)";
            exit 2;
        }
        %want{$_} = True for @hit;
    }
    @parts = @parts.grep({ %want{$_} });
}

# The overlay takes packages/ and the fixtures from $ROAST and the tests from
# fat/, so the two had better be the same revision of Roast.
sub roast-revision(--> Str) {
    my $p = run('git', '-C', $ROAST, 'rev-parse', 'HEAD', :out, :err);
    my $r = $p.out.slurp(:close).trim;
    $p.err.slurp(:close);
    $r
}
my $README    = $FAT.add('README.md');
my $MADE-FROM = $README.e && $README.slurp ~~ / 'Roast `' (<xdigit>+) '`' / ?? ~$0 !! '';
my $ROAST-REV = roast-revision();
if $MADE-FROM && $ROAST-REV && !$ROAST-REV.starts-with($MADE-FROM) {
    note "run-fatroast: fat/ was made from Roast $MADE-FROM, but $ROAST is at {$ROAST-REV.substr(0, 9)}: "
       ~ "the overlays pair those tests with this checkout's helpers and fixtures";
}

# Engines are named by their basename in the lists and the summary; a second
# engine with the same basename gets a number.
my @engines;
my @given = @want-engines || [$*EXECUTABLE.absolute];
for @given -> $e {
    my $path = $e.contains('/') ?? $e.IO.absolute !! $e;
    my $name = $path.IO.basename;
    my $n = 1;
    $name = $path.IO.basename ~ '-' ~ ++$n while @engines.first(*<name> eq $name);
    @engines.push: %( :$path, :$name );
}

my $LISTS = $OVERLAY.IO.add('lists');
$LISTS.mkdir;

sub t-files(IO::Path $root --> List) {
    my $prefix = $root.absolute.chars + 1;
    my @out;
    my @todo = $root;
    while @todo {
        for dir(@todo.shift) -> $p {
            if $p.d {
                @todo.push($p);
            }
            elsif $p.extension eq 't' {
                @out.push($p.absolute.substr($prefix));
            }
        }
    }
    @out.sort.List
}

# What run-roast will run of a part's files: the ones its list keeps after
# --skip-marker, matched by a PATTERN if any is given.
sub selected(@files --> List) {
    @files.grep(-> $f {
        !%MARKERS{$f}.first({ %skip-marker{$_} }).defined
        && (!@patterns || @patterns.first({ $f.contains($_) }).defined)
    }).List
}

sub rsync(*@args) {
    my $p = run('rsync', '-a', |@args, :err);
    my $err = $p.err.slurp(:close);
    unless $p.exitcode == 0 {
        note "run-fatroast: rsync {@args.join(' ')} failed:\n$err";
        exit 1;
    }
}

sub build-overlay(Str $part, @files --> IO::Path) {
    my $o = $OVERLAY.IO.add($part);
    $o.mkdir;
    rsync('--delete', '--exclude=.git', "$ROAST/", "$o/");
    rsync($FAT.add($part).absolute ~ '/', "$o/") unless $part eq $BASE;
    $o.add('spectest.data').spurt(@files.map({ %LINE{$_} }).join("\n") ~ "\n");
    $o
}

# A run's outcome per file: PASS from its --list file, the rest from the
# --failed block at the end of its log (`  [part]  13/19  S32-str/uc.t`). The
# per-file lines before the summary look the same but leave out the no-TAP
# files, so only the block is read.
sub outcomes(IO::Path $list, IO::Path $log --> Hash) {
    my %o;
    %o{$_} = 'PASS' for $list.lines.grep(*.chars);
    my $in = False;
    for $log.slurp(:enc<utf8-c8>).lines -> $l {
        if $l.starts-with('Files not fully passing (') { $in = True; next }
        next unless $in;
        if $l ~~ /^ \s+ '[' (part || TIME || noTAP || LOST) ']' \s+ (\S+) \s+ (\S+) \s* $/ {
            %o{~$2} = (~$0 eq 'noTAP' || ~$0 eq 'LOST') ?? ~$0 !! "$0 $1";
        }
    }
    %o
}

# run-roast asks git for the checkout's revision. The overlay has no .git, and
# a git that walked up out of it would report whatever repository holds the
# overlay directory instead, so it is told not to.
my %env = %*ENV;
%env<GIT_CEILING_DIRECTORIES> = $OVERLAY;

sub run-part(Str $part, %e, @files --> Hash) {
    my $o    = build-overlay($part, @files);
    my $list = $LISTS.add("{$part}.{%e<name>}.list");
    my $log  = $LISTS.add("{$part}.{%e<name>}.log");
    unlink $list if $list.e;    # a list left from an earlier run would read as this one's
    %env<ROAST> = $o.absolute;
    say "run-fatroast: $part under %e<path> (log: $log)";
    my $started = now;
    my $sh = $VERBOSE ?? 'log=$1; shift; "$@" 2>&1 | tee "$log"'
                      !! 'log=$1; shift; exec "$@" > "$log" 2>&1';
    run('/bin/sh', '-c', $sh, 'sh', ~$log, %e<path>, $RUN-ROAST, |@pass, "--list=$list", '--failed', :env(%env));
    unless $list.e {
        note "run-fatroast: run-roast wrote no list for $part under %e<path> — see $log";
        return Hash;
    }
    my %o = outcomes($list, $log);
    my %n = %o.values.map(*.words[0]).Bag;
    say sprintf('run-fatroast:   %d files: %s (%.0f s)', %o.elems,
                <PASS part TIME noTAP LOST>.grep({ %n{$_} }).map({ "{%n{$_}} {.lc}" }).join(', ') || 'none ran',
                now - $started);
    %o
}

my %files;      # part -> the files this run selects of it
my %out;        # part -> engine name -> path -> outcome (absent: the run broke)
my $BROKEN = False;
for @parts -> $part {
    my @all = $part eq $BASE
        ?? (@trees.grep(* ∈ @parts) || @trees).map({ |t-files($FAT.add($_)) }).unique.sort
        !! t-files($FAT.add($part));
    my @listed = @all.grep({ %LINE{$_}:exists });
    my @run    = selected(@listed);
    %files{$part} = @run;
    say "";
    say "run-fatroast: $part — {@run.elems} of {@all.elems} files"
      ~ (@all > @listed ?? " ({@all - @listed} not in Roast's spectest.data, not run)" !! '');
    next unless @run;
    for @engines -> %e {
        with run-part($part, %e, @listed) -> %o {
            %out{$part}{%e<name>} = %(@run.map(-> $f { $f => (%o{$f} // '?') }));
        }
        else {
            $BROKEN = True;
        }
    }
}

# ---- summary
sub pass-count($part, $name) {
    my $o = %out{$part}{$name};
    $o.defined ?? $o.values.grep('PASS').elems !! '—'
}
my $w  = (|@parts.map(*.chars), 4).max;
my $cw = (|@engines.map(*<name>.chars), 6).max;
say "";
say "run-fatroast: fully-passing files, by part (Roast {$MADE-FROM || $ROAST-REV.substr(0, 9) || '?'}; logs and lists in $LISTS)";
say sprintf("  %-{$w}s  %6s", 'part', 'files') ~ @engines.map({ sprintf("  %{$cw}s", .<name>) }).join;
for @parts -> $part {
    say sprintf("  %-{$w}s  %6d", $part, %files{$part}.elems)
      ~ @engines.map(-> %e { sprintf("  %{$cw}s", pass-count($part, %e<name>)) }).join;
}

# What the fat cost each engine: fully passing in Roast, not fattened.
my @fattened = @parts.grep(* ne $BASE);
if %out{$BASE} && @fattened {
    say "";
    say "run-fatroast: files that fully pass in Roast but not fattened";
    say sprintf("  %-{$w}s", 'part') ~ @engines.map({ sprintf("  %{$cw}s", .<name>) }).join;
    for @fattened -> $part {
        my @cells;
        for @engines -> %e {
            my $b = %out{$BASE}{%e<name>};
            my $f = %out{$part}{%e<name>};
            my $lost = $LISTS.add("{$part}.{%e<name>}.lost");
            unlink $lost if $lost.e;
            unless $b.defined && $f.defined {
                @cells.push: '—';
                next;
            }
            my @l = $f.keys.grep({ $f{$_} ne 'PASS' && ($b{$_} // '') eq 'PASS' }).sort;
            $lost.spurt(@l.map({ "$_\t{$f{$_}}" }).join("\n") ~ (@l ?? "\n" !! ''));
            @cells.push: @l.elems;
        }
        say sprintf("  %-{$w}s", $part) ~ @cells.map({ sprintf("  %{$cw}s", $_) }).join;
    }
}

# Where the engines differ, file by file, and whether the plain file does too.
if @engines > 1 {
    my $a = @engines[0]<name>;
    for @engines[1..*] -> %e {
        my $b = %e<name>;
        say "";
        say "run-fatroast: files whose outcome differs, $a vs $b";
        for @parts -> $part {
            my $diff = $LISTS.add("{$part}.{$a}-vs-{$b}.diff");
            unlink $diff if $diff.e;
            my $oa = %out{$part}{$a};
            my $ob = %out{$part}{$b};
            next unless $oa.defined && $ob.defined;
            my $ba = %out{$BASE}{$a};
            my $bb = %out{$BASE}{$b};
            my @rows;
            for %files{$part}.grep({ $oa{$_} ne $ob{$_} }) -> $f {
                my $class = $part eq $BASE                         ?? 'base'
                         !! !($ba.defined && $bb.defined)           ?? '?'
                         !! ($ba{$f} // '?') eq ($bb{$f} // '?')    ?? 'fat'
                         !!                                            'base';
                @rows.push: [$class, $f, $oa{$f}, $ob{$f}];
            }
            $diff.spurt(("class\tfile\t$a\t$b", |@rows.map(*.join("\t"))).join("\n") ~ "\n") if @rows;
            my %c = @rows.map(*[0]).Bag;
            say sprintf("  %-{$w}s  %d differ", $part, @rows.elems)
              ~ ($part ne $BASE && @rows ?? ': ' ~ <fat base ?>.grep({ %c{$_} }).map({ "{%c{$_}} $_" }).join(', ') !! '')
              ~ (@rows ?? "  -> $diff" !! '');
            my @show = @rows.sort({ .[0] ne 'fat', .[1] });
            for @show.head(20) -> $r {
                say sprintf("      %-4s  %s  (%s %s, %s %s)", $r[0], $r[1], $a, $r[2], $b, $r[3]);
            }
            say "      …and {@show.elems - 20} more" if @show > 20;
        }
    }
}
exit($BROKEN ?? 1 !! 0);
