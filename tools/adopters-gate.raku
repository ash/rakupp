#!/usr/bin/env rakupp
# Run each adopter's own test suite on a candidate, the way their CI runs it.
#
#   rakupp tools/adopters-gate.raku                    # every adopter, both modes
#   RAKUPP=/path/to/rakupp rakupp tools/adopters-gate.raku
#   rakupp tools/adopters-gate.raku --only=iz4         # one adopter
#   rakupp tools/adopters-gate.raku --mode=source      # or --mode=exe
#   rakupp tools/adopters-gate.raku --ref=v5.2.0       # red files rerun on a release
#   rakupp tools/adopters-gate.raku --head             # their newest commit, not the pin
#   rakupp tools/adopters-gate.raku --dirty            # the checkout as it is, edits and all
#
# The adopters are listed in tools/adopters.list: a repository URL, a pinned
# commit, and the failures already known. Their code is never committed here;
# it is cloned into RAKUPP_ADOPTERS, or ~/.cache/rakupp-adopters, and checked
# out at the pin.
#
# Two modes, because their CI runs two things:
#   source  every t/*.rakutest as `rakupp -Ilib t/FILE` from the checkout root,
#           which is what `prove --ext .rakutest -e "rakupp -Ilib" t/` runs;
#   exe     `rakupp -I lib --aot --standalone ENTRY -o FILE`, then the same
#           suite with TEST-BIN-VAR naming that file. This is the binary they
#           ship, built by our AOT backend.
#
# RED (exit 1) is a file, or a build, that fails and is not listed as known.
# --ref=TAG|PATH reruns only the red ones on a reference binary (a release
# tag is downloaded once, as tools/battery-scan.raku does), so a red file that
# fails on the reference too is told apart from one that is ours. Exit 2 is
# "could not judge" — no git, no network, no binary, a modified checkout —
# and never a pass.
#
# A file passes when it exits 0, prints a plan, prints as many test lines as
# the plan says, and none of them is a `not ok` without `# TODO`. Each file
# runs alone, sequentially, with HOME in a scratch directory and a time cap.

use lib $?FILE.IO.parent.add('lib').Str;
use Gate;
use Battery;

my constant CAP = 300;                          # seconds per test file
my $ROOT = $?FILE.IO.parent.parent;

my ($only, $mode, $ref, $head, $dirty) = Str, 'both', Str, False, False;
for @*ARGS -> $a {
    if    $a ~~ / ^ '--only=' (.+) $ / { $only = ~$0 }
    elsif $a ~~ / ^ '--mode=' (source|exe|both) $ / { $mode = ~$0 }
    elsif $a ~~ / ^ '--ref=' (.+) $ / { $ref = ~$0 }
    elsif $a eq '--head'  { $head = True }
    elsif $a eq '--dirty' { $dirty = True }
    else { note "adopters-gate: unknown argument: $a"; exit 2 }
}

# ---------------------------------------------------------------------------
# the list
# ---------------------------------------------------------------------------

my @adopters;
my %known;                                      # name => { what => reason }
for $ROOT.add('tools/adopters.list').lines.kv -> $n, $line {
    next if $line.trim eq '' || $line.trim.starts-with('#');
    my @w = $line.words;
    given @w[0] {
        when 'adopter' {
            unless @w.elems == 6 {
                note "adopters-gate: tools/adopters.list line {$n + 1}: an adopter line has six words";
                exit 2;
            }
            @adopters.push: %( name => @w[1], url => @w[2], commit => @w[3],
                               entry => @w[4], binvar => @w[5] );
        }
        when 'known' {
            %known{@w[1]}{$_} = @w[3..*].join(' ') for @w[2].split(',');
        }
        default {
            note "adopters-gate: tools/adopters.list line {$n + 1}: unknown kind '{@w[0]}'";
            exit 2;
        }
    }
}
@adopters .= grep({ .<name> eq $only }) if $only;
unless @adopters {
    note "adopters-gate: no adopter{$only ?? " named $only" !! ''} in tools/adopters.list";
    exit 2;
}

my %PICK = pick-rakupp($ROOT);
require-native(%PICK, :tool<adopters-gate>, :verdict<INCONCLUSIVE>);
# absolute: every file runs from the adopter's checkout
my $RAKUPP = %PICK<path>.IO.absolute;
my $REF = $ref.defined
    ?? ($ref.IO.x ?? $ref.IO.absolute !! release-binary($ref, :tool<adopters-gate>))
    !! Str;

my $CACHE = (%*ENV<RAKUPP_ADOPTERS>
             // ((%*ENV<XDG_CACHE_HOME> // ((%*ENV<HOME> // '.') ~ '/.cache')) ~ '/rakupp-adopters')).IO;
my $WORK = $*TMPDIR.add("adopters-gate-{$*PID}");
$WORK.mkdir;
$WORK.add('home').mkdir;
END { rm-tree($WORK) if $WORK.e }   # not LEAVE: exit skips it

say provenance-line('adopters-gate', %PICK);
say "adopters-gate: reference {$REF} (rakupp {binary-version($REF)})" if $REF;

# ---------------------------------------------------------------------------
# checkouts
# ---------------------------------------------------------------------------

sub git(*@args, IO::Path :$dir --> Hash) {
    my $p = run('git', |($dir ?? ('-C', $dir.absolute) !! ()), |@args, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    %( ok => $p.exitcode == 0, out => $out.trim, err => $err.trim )
}

sub inconclusive(Str $why) {
    note "adopters-gate: $why";
    note "adopters-gate: INCONCLUSIVE — exit 2 is never a pass.";
    exit 2;
}

#| The checkout of one adopter at the commit to test, and that commit's short
#| name. Clones on first use; fetches only when the commit is not there yet.
sub checkout(%a --> List) {
    my $dir = $CACHE.add(%a<name>);
    unless $dir.add('.git').d {
        $CACHE.mkdir unless $CACHE.d;
        say "adopters-gate: cloning {%a<url>} into $dir";
        my $c = git('clone', '--quiet', %a<url>, $dir.absolute);
        inconclusive("could not clone {%a<url>}: {$c<err>}") unless $c<ok>;
    }
    if $dirty {
        my $h = git('rev-parse', '--short', 'HEAD', :$dir);
        return ($dir, "{$h<out>} as it is on disk");
    }
    my $st = git('status', '--porcelain', '--untracked-files=no', :$dir);
    inconclusive("$dir has local changes — commit, stash or drop them, or pass --dirty to test them")
        if $st<out>;
    my $want = %a<commit>;
    if $head {
        my $f = git('fetch', '--quiet', 'origin', :$dir);
        inconclusive("could not fetch {%a<url>}: {$f<err>}") unless $f<ok>;
        $want = 'origin/HEAD';
    }
    elsif !git('cat-file', '-e', "{$want}^\{commit\}", :$dir)<ok> {
        my $f = git('fetch', '--quiet', 'origin', :$dir);
        inconclusive("could not fetch {%a<url>}: {$f<err>}") unless $f<ok>;
        inconclusive("{%a<url>} has no commit $want") unless git('cat-file', '-e', "{$want}^\{commit\}", :$dir)<ok>;
    }
    my $co = git('-c', 'advice.detachedHead=false', 'checkout', '--quiet', '--detach', $want, :$dir);
    inconclusive("could not check out $want in $dir: {$co<err>}") unless $co<ok>;
    my $h = git('log', '-1', '--format=%h %cs', :$dir);
    ($dir, $h<out> ~ ($head ?? ' (their newest commit, not the pin)' !! ''))
}

# ---------------------------------------------------------------------------
# running a suite
# ---------------------------------------------------------------------------

#| Whether TAP output passes, and a few words on why not.
sub judge(%r --> List) {
    return (False, "time cap of {CAP} s") if %r<hung>;
    return (False, "ended by {signal-name(%r<signal>)}") if %r<signal>;
    my @lines = %r<out>.lines;
    my $plan = @lines.first(/ ^ '1..' \d+ /);
    my @tests = @lines.grep(/ ^ ['not '?] 'ok' >> /);
    my @bad = @tests.grep({ .starts-with('not ok') && !($_ ~~ /:i '#' \s* 'TODO' >> /) });
    my $planned = $plan && $plan ~~ / ^ '1..' (\d+) / ?? +$0 !! -1;
    if %r<rc> == 0 && !@bad && $plan && ($planned == 0 || @tests.elems == $planned) {
        return (True, "{@tests.elems} tests");
    }
    my $why = @bad ?? "{@bad.elems} of {@tests.elems} not ok, first: {@bad[0].substr(0, 100)}"
           !! !$plan ?? "no plan; {first-line(%r)}"
           !! @tests.elems != $planned && $planned > 0 ?? "{@tests.elems} of $planned planned tests ran; {first-line(%r)}"
           !! "exit {%r<rc>}; {first-line(%r)}";
    (False, $why)
}

#| Run every test file of a checkout on one binary. Returns file => [pass, why].
sub run-suite(IO::Path $dir, Str $bin, :%env, :@files --> Hash) {
    my %res;
    for @files -> $f {
        my %r = run-capped(($bin, '-Ilib', $f), :cap(CAP), :work($dir),
                           :env(%( HOME => $WORK.add('home').absolute, |%env )));
        %res{$f} = judge(%r).Array;
    }
    %res
}

#| Compile the standalone file. Returns (path or Str, why).
sub build-exe(IO::Path $dir, Str $bin, %a, Str $tag --> List) {
    my $out = $WORK.add("{%a<name>}-$tag");
    $out.unlink if $out.e;
    my %r = run-capped(($bin, '-I', 'lib', '--aot', '--standalone', %a<entry>, '-o', $out.absolute),
                       :cap(CAP), :work($dir), :env(%( HOME => $WORK.add('home').absolute )));
    return ($out.absolute, 'built') if %r<rc> == 0 && $out.x;
    my $why = (%r<err> ~ "\n" ~ %r<out>).lines.grep(*.trim).tail(1).head // "exit {%r<rc>}";
    (Str, $why.trim.substr(0, 160))
}

# ---------------------------------------------------------------------------
# the gate
# ---------------------------------------------------------------------------

my @red;                                        # "name mode what: why"
my @stale;                                      # known lines that pass now
my $files-run = 0;

for @adopters -> %a {
    my ($dir, $rev) = checkout(%a);
    my @files = $dir.add('t').dir.grep(*.basename.ends-with('.rakutest'))
                    .map({ 't/' ~ .basename }).sort.List;
    my %k = %known{%a<name>} // {};
    say '';
    say "== {%a<name>} at $rev — {@files.elems} test files ({%a<url>})";
    inconclusive("{%a<name>}: no t/*.rakutest in $dir") unless @files;
    say "   known: $_" for %k.values.unique;

    my @modes = $mode eq 'both' ?? <source exe> !! ($mode,);
    for @modes -> $m {
        my %env;
        my %res;
        my %need-ref;                           # what: True, for --ref
        if $m eq 'exe' {
            my ($exe, $why) = build-exe($dir, $RAKUPP, %a, 'cand');
            my $known = %k<build> // %k<*>;
            if !$exe {
                if $known { say "  exe    build fails, known" }
                else {
                    say "  exe    build ✗ $why";
                    @red.push: "{%a<name>} exe build: $why";
                    %need-ref<build> = True;
                }
                ref-check(%a, $dir, $m, %need-ref) if $REF && %need-ref;
                next;
            }
            @stale.push: "{%a<name>} build" if $known && !%k<*>;
            %env{%a<binvar>} = $exe;
        }
        %res = run-suite($dir, $RAKUPP, :%env, :@files);
        $files-run += @files.elems;
        my $pass = %res.values.grep(*[0]).elems;
        say "  {$m.fmt('%-6s')} {$pass}/{@files.elems} files pass";
        my @known-fail;
        for @files -> $f {
            my ($ok, $why) = |%res{$f};
            if $ok {
                @stale.push: "{%a<name>} $f" if %k{$f};
                next;
            }
            if %k{$f} || %k<*> { @known-fail.push: $f; next }
            say "         ✗ $f — $why";
            @red.push: "{%a<name>} $m $f: $why";
            %need-ref{$f} = True;
        }
        say "         {@known-fail.elems} fail{@known-fail.elems == 1 ?? 's' !! ''}, known: {@known-fail.map(*.subst(/ ^ 't/' /, '').subst(/ '.rakutest' $ /, '')).join(', ')}"
            if @known-fail;
        if %k<*> {
            @stale.push: "{%a<name>} * — $pass file{$pass == 1 ?? ' passes' !! 's pass'} in $m mode; narrow the line to the files that fail"
                if $pass;
        }
        ref-check(%a, $dir, $m, %need-ref) if $REF && %need-ref;
    }
}

#| Rerun what went red on the reference binary, and say which side it is on.
sub ref-check(%a, IO::Path $dir, Str $m, %need) {
    my %env;
    if $m eq 'exe' {
        my ($exe, $why) = build-exe($dir, $REF, %a, 'ref');
        unless $exe {
            say "         reference: the build fails there too ($why)";
            return;
        }
        %env{%a<binvar>} = $exe;
        say "         reference: the build succeeds there — ours" if %need<build>;
        return if %need<build>;
    }
    my @files = %need.keys.sort;
    my %res = run-suite($dir, $REF, :%env, :@files);
    for @files -> $f {
        say %res{$f}[0]
            ?? "         reference: $f passes there — ours"
            !! "         reference: $f fails there too ({%res{$f}[1]})";
    }
}

sub rm-tree(IO::Path $p) {
    if $p.d && !$p.l { rm-tree($_) for $p.dir; $p.rmdir }
    else { $p.unlink }
}

say '';
if @stale {
    say "adopters-gate: known failures that pass now — delete their lines in tools/adopters.list:";
    say "  $_" for @stale.unique;
}
if @red {
    say "adopters-gate: RED — {@red.elems} unexpected failure{@red.elems == 1 ?? '' !! 's'} "
      ~ "on rakupp {%PICK<version>}:";
    say "  $_" for @red;
    say "Rerun with --ref=<previous release> to tell ours from theirs." unless $REF;
    exit 1;
}
say "adopters-gate: GREEN — {@adopters.elems} adopter{@adopters.elems == 1 ?? '' !! 's'}, "
  ~ "$files-run file runs, nothing unexpected on rakupp {%PICK<version>}.";
exit 0;
