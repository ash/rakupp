#!/usr/bin/env rakupp
# FastRoast's fattened Roast, part by part, through tools/run-roast.raku.
#
# FastRoast (github.com/ash/fastroast) rewrites Roast's tests and keeps what it
# writes in its fat/ directory: seed/ (each file as `fudge rakudo` leaves it,
# the `#?` lines dropped) and one tree per transform — strings-v1.mixed/,
# names-v1.mixed/, ops-v1/ — each holding only the files that transform
# changed, at their Roast paths. This runs every part on run-roast's machinery,
# one after the other, under one engine or several, and tallies them.
#
# A part cannot be ROAST by itself. Its tree has the .t files and nothing else:
# about 300 of them find their helpers through their own location —
# `use lib $*PROGRAM.parent(2).add("packages/Test-Helpers")` — others read
# fixtures that sit beside them, and with no spectest.data run-roast would fall
# back to --all. So each part gets an overlay: the Roast checkout copied without
# .git, the part laid over it, and a spectest.data that lists exactly the
# part's files. Roast's own tests stay in the overlay as fixtures and are not
# run. Every run refreshes the overlays with rsync (an unchanged one costs a
# second or two), which also sweeps away what the last run's tests wrote beside
# themselves.
#
# Nothing is fudged on the way: the seeds are Rakudo's fudge output with the
# directives removed, so every engine runs the same text, Rakudo's skips and
# todos already applied, and run-roast finds no directive line to fudge.
#
# A fattened test can fail on every engine — a sort over `mixed`-palette
# strings is no longer sorted — so a part's pass count says little alone. The
# signal is where engines differ: given two --engine values, each part's lists
# of fully-passing files are diffed, and the files only one engine passes are
# printed. The lists are --list's, run-roast's reliable output (its comments say
# why the decorated per-file lines are not). A file that passes partly under
# both engines, with different counts, does not show up here; for test-by-test
# verdicts and a shrunk reproducer, use FastRoast's own `fastroast run`.
#
# Usage:
#   build/rakupp tools/fatroast.raku [OPTION ...] [PATTERN ...]
#   build/rakupp tools/fatroast.raku --help
#
# Examples:
#   nice build/rakupp tools/fatroast.raku -j2
#   nice build/rakupp tools/fatroast.raku -j2 --engine=build/rakupp,rakudo
#   build/rakupp tools/fatroast.raku --parts=ops-v1 S03-operators

my $RUN-ROAST = $?FILE.IO.parent.add('run-roast.raku').absolute;
my $HOME      = %*ENV<HOME> // '.';
my $FASTROAST = %*ENV<FASTROAST> // "$HOME/fastroast";
my $OVERLAY   = %*ENV<FATROAST_OVERLAY> // "$HOME/roast-fat";
my $ROAST     = (%*ENV<ROAST> // "$HOME/roast").IO.absolute;
my @want-parts;
my @want-engines;
my @pass;        # handed to run-roast as they came: options and PATTERNs
my @patterns;    # the PATTERNs alone, to count what a part will run
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
    elsif $a eq '-h' || $a eq '--help' {
        $HELP = True;
    }
    elsif $a.starts-with('--list=') || $a eq '--all' || $a.starts-with('--suite=') {
        # --list is this script's own (one per part and engine); --all would run
        # all of Roast in the overlay; --suite would run something else entirely
        note "fatroast: $a does not apply to a part's overlay (--help lists the options)";
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

if $HELP {
    print qq:to/END/;
    Usage: build/rakupp tools/fatroast.raku [OPTION ...] [PATTERN ...]

    Runs every part of FastRoast's fattened Roast (fat/seed and one tree per
    transform) through tools/run-roast.raku, each from an overlay of the Roast
    checkout, and tallies the fully-passing files per part and engine. Given
    two engines, it lists the files only one of them passes.

      fastroast  $FASTROAST
      roast      $ROAST
      overlays   $OVERLAY

    Options
      --parts=P[,P]      only these parts (default: every directory in fat/,
                         seed first)
      --engine=E[,E]     run each part under each engine, a path or a name on
                         PATH; the first is the one the others are diffed
                         against (default: the engine running this file)
      --fastroast=DIR    the FastRoast checkout (\$FASTROAST,
                         default \$HOME/fastroast)
      --overlay=DIR      where the overlays and lists go (\$FATROAST_OVERLAY,
                         default \$HOME/roast-fat)
      -h, --help         print this and exit

    Anything else goes to run-roast as it is: -j=N, --failed, --times=FILE,
    PATTERNs. --list, --all and --suite are refused: the lists are this
    script's, and the other two would not run the part.

    Environment
      ROAST              the Roast checkout the overlays copy (default
                         \$HOME/roast); fat/README.md names the revision the
                         trees were made from

    Output
      OVERLAY/PART/                      the overlay a part ran from
      OVERLAY/lists/PART.ENGINE.list     its fully-passing files (+ .meta)
      OVERLAY/lists/PART.A-vs-B.diff     files only one engine passes
    END
    exit 0;
}

unless $FAT.d {
    note "fatroast: no $FAT — point --fastroast (or \$FASTROAST) at a FastRoast checkout";
    exit 2;
}
unless $ROAST.IO.add('spectest.data').e {
    note "fatroast: $ROAST is not a Roast checkout — set ROAST";
    exit 2;
}

# The parts are fat/'s directories. Seed goes first: it is the baseline the
# transforms are read against.
my @known = dir($FAT).grep(*.d).map(*.basename).sort;
@known = |@known.grep('seed'), |@known.grep(* ne 'seed');
my @parts = @want-parts || @known;
with @parts.first(* ∉ @known) {
    note "fatroast: no part '$_' in $FAT (there are: {@known.join(', ')})";
    exit 2;
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
    note "fatroast: fat/ was made from Roast $MADE-FROM, but $ROAST is at {$ROAST-REV.substr(0, 9)}: "
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

sub rsync(*@args) {
    my $p = run('rsync', '-a', |@args, :err);
    my $err = $p.err.slurp(:close);
    unless $p.exitcode == 0 {
        note "fatroast: rsync {@args.join(' ')} failed:\n$err";
        exit 1;
    }
}

sub build-overlay(Str $part, @files --> IO::Path) {
    my $o = $OVERLAY.IO.add($part);
    $o.mkdir;
    rsync('--delete', '--exclude=.git', "$ROAST/", "$o/");
    rsync($FAT.add($part).absolute ~ '/', "$o/");
    $o.add('spectest.data').spurt(@files.join("\n") ~ "\n");
    $o
}

# run-roast asks git for the checkout's revision. The overlay has no .git, and
# a git that walked up out of it would report whatever repository holds the
# overlay directory instead, so it is told not to.
my %env = %*ENV;
%env<GIT_CEILING_DIRECTORIES> = $OVERLAY;

my %files;      # part -> how many of its files this run selects
my %passed;     # part -> engine name -> Set of fully-passing paths (absent: no list)
my $BROKEN = False;
for @parts -> $part {
    my @files = t-files($FAT.add($part));
    my @run = @patterns
        ?? @files.grep(-> $f { @patterns.first({ $f.contains($_) }).defined })
        !! @files;
    %files{$part} = @run.elems;
    say "";
    say "fatroast: $part — {@run.elems} of {@files.elems} files";
    next unless @run;
    my $o = build-overlay($part, @files);
    %env<ROAST> = $o.absolute;
    for @engines -> %e {
        my $list = $LISTS.add("{$part}.{%e<name>}.list");
        unlink $list if $list.e;    # a list left from an earlier run would read as this one's
        say "fatroast: $part under %e<path>";
        run(%e<path>, $RUN-ROAST, |@pass, "--list=$list", :env(%env));
        if $list.e {
            %passed{$part}{%e<name>} = $list.lines.grep(*.chars).Set;
        }
        else {
            note "fatroast: run-roast wrote no list for $part under %e<path>";
            $BROKEN = True;
        }
    }
}

# ---- summary: fully-passing files per part and engine, then the disagreements
say "";
say "fatroast: fully-passing files, by part (Roast {$MADE-FROM || $ROAST-REV.substr(0, 9) || '?'}, overlays in $OVERLAY)";
my $w = (@parts.map(*.chars).max, 4).max;
say sprintf("  %-{$w}s  %6s", 'part', 'files') ~ @engines.map({ sprintf('  %10s', .<name>) }).join;
for @parts -> $part {
    say sprintf("  %-{$w}s  %6d", $part, %files{$part})
      ~ @engines.map(-> %e {
            my $s = %passed{$part}{%e<name>};
            sprintf('  %10s', $s.defined ?? $s.elems !! '—')
        }).join;
}
if @engines > 1 {
    my $base = @engines[0]<name>;
    for @engines[1..*] -> %e {
        my $other = %e<name>;
        say "";
        say "fatroast: files only one engine fully passes, $base vs $other";
        for @parts -> $part {
            my $a = %passed{$part}{$base};
            my $b = %passed{$part}{$other};
            my $diff = $LISTS.add("{$part}.{$base}-vs-{$other}.diff");
            unlink $diff if $diff.e;
            next unless $a.defined && $b.defined;
            my @only = |($a (-) $b).keys.map({ ($base, $_) }),
                       |($b (-) $a).keys.map({ ($other, $_) });
            @only = @only.sort({ .[1] });
            $diff.spurt(@only.map({ "{.[0]}\t{.[1]}" }).join("\n") ~ (@only ?? "\n" !! ''));
            say sprintf("  %-{$w}s  %s only %d, %s only %d", $part,
                        $base, ($a (-) $b).elems, $other, ($b (-) $a).elems)
              ~ (@only ?? "  -> $diff" !! '');
            say sprintf("      %-10s %s", .[0], .[1]) for @only.head(20);
            say "      …and {@only.elems - 20} more" if @only > 20;
        }
    }
}
exit($BROKEN ?? 1 !! 0);
