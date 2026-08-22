# MAIN command-line conventions in NATIVE --exe binaries — the compiled face
# of main-args-conventions.raku. The compiled invoke used to skip the whole
# protocol: a short alias key (`:r(:$string)` answering -r) never bound, a
# stray positional ran MAIN instead of failing dispatch, `--foo abc` pairing
# and --help did not exist, and an unknown option was silently ignored. Now
# the binary embeds its MAIN signatures (bodies detached, via the AstSerial
# module-cache serializer) and RT.runCompiledMain feeds them through the SAME
# mainProtocol the interpreter uses — so every expectation below is the
# sibling file's oracle-verified output, produced by a compiled binary.
#
# Each compile asserts "(native)": if a probe ever falls back to the bundled
# interpreter, this file stops guarding native codegen and says so.

my $v = run($*EXECUTABLE, '--version', :out, :err);
my $banner = $v.out.slurp(:close); $v.err.slurp(:close);
unless $banner.contains('rakupp') {
    # only rakupp has --exe; under Rakudo there is nothing this file can test
    note 'main-args-exe: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("main-args-exe-$*PID");
mkdir $work;

my $fails = 0;
sub check(Str $desc, $got, $want) {
    if $got eq $want {
        say "ok - $desc";
    }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT [{$got}] WANT [{$want}]";
    }
}

my @made;
sub compile(Str $name, Str $code --> Str) {
    my $src = $work.add($name ~ '.raku');
    $src.spurt($code);
    my $bin = $work.add($name);
    my $p = run($*EXECUTABLE, '--exe', '-o', $bin.Str, $src.Str, :out, :err);
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    if $p.exitcode != 0 || !$out.contains('(native)') {
        $fails++;
        note "compile of $name did not produce a native binary:\n$out";
        return '';
    }
    @made.push($src.Str, $bin.Str);
    $bin.Str
}

# run a compiled probe; first stdout line, or 'USAGE' when dispatch failed
sub first-line(Str $bin, *@args) {
    return 'NOBIN' unless $bin;
    my $p = run($bin, |@args, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $p.exitcode == 2 ?? 'USAGE' !! ($out.lines[0] // '')
}

# --help is the one path with its own contract: usage on STDOUT, exit 0
sub help-out(Str $bin) {
    return 'NOBIN' unless $bin;
    my $p = run($bin, '--help', :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    ($p.exitcode == 0 && $out.starts-with('Usage:')) ?? 'HELP' !! "exit={$p.exitcode}"
}

my $one = compile('one', q:to/END/);
    sub MAIN($pos?, Str :$foo = 'd', Bool :r(:$string) = False) {
        say "pos={$pos // 'U'} foo=$foo s=$string"
    }
    END
check('a short alias key binds (-r)',       first-line($one, '-r', 'x'), 'pos=x foo=d s=True');
check('the long name still binds',          first-line($one, '--string', 'x'), 'pos=x foo=d s=True');
check('space-form pairing works compiled',  first-line($one, '--foo', 'abc', 'xx'), 'pos=xx foo=abc s=False');
check('stray positionals fail dispatch',    first-line($one, '--', 'a', 'b'), 'USAGE');
check('an option after a positional is literal, and fails', first-line($one, 'xx', '--foo=1'), 'USAGE');
check('a -- directly after the first positional is consumed', first-line($one, 'xx', '--'), 'pos=xx foo=d s=False');
check('a colon option binds compiled',      first-line($one, ':r', 'x'), 'pos=x foo=d s=True');
check('--help prints usage to stdout, exit 0', help-out($one), 'HELP');
check('--help after a positional is literal, exit 2', first-line($one, 'xx', '--help'), 'USAGE');

my $na = compile('na', q:to/END/);
    my %*SUB-MAIN-OPTS = :named-anywhere;
    sub MAIN($u, :@x, Bool :$c = False) { say "u=$u c=$c x=[{@x.join(',')}]" }
    END
check('named-anywhere holds compiled',      first-line($na, 'URL', '-c'), 'u=URL c=True x=[]');
check('a repeated option collects',         first-line($na, 'URL', '--x=a', '--x=b'), 'u=URL c=False x=[a,b]');

my $multi = compile('multi', q:to/END/);
    multi MAIN('add', Int $a, Int $b) { say $a + $b }
    multi MAIN('hello', Str $name) { say "hi $name" }
    END
check('multi picks by literal positional',  first-line($multi, 'add', '2', '3'), '5');
check('multi with no match prints usage',   first-line($multi, 'frob'), 'USAGE');

unlink $_ for @made;
try rmdir $work;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
