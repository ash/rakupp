# MAIN command-line conventions — the full oracle matrix (Rakudo 2026.07),
# reported as "rakupp demands =, Rakudo does not" and pinned here as three
# rules, each verified case by case against Rakudo before implementation:
#
#   1. SPACE-SEPARATED VALUES: before the boundary (rule 2), `--foo abc`
#      pairs into :foo<abc> iff the candidate declares :$foo with the Str
#      type. Untyped and Int/Num named params do NOT pair (`--n 42` fails
#      dispatch under Rakudo too). Consumption is unconditional — even
#      `--foo --verbose` makes foo eq "--verbose". Short and single-dash
#      spellings pair the same way (-f val).
#   2. OPTIONS END AT THE FIRST POSITIONAL (the POSIX convention): after
#      one, `--foo=x` is the literal string "--foo=x". A bare `--` is
#      consumed and ends options — which is how a positional -5 is passed.
#   3. SINGLE-DASH OPTIONS: -v, -n=3, -foo=bar are named args; the whole
#      rest of the token is the name, `=` splits off the value.
#
# Passes under both engines: every expectation IS Rakudo's output.

my $work = $*TMPDIR.add("main-args-$*PID");
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

sub script(Str $name, Str $code) {
    my $f = $work.add($name);
    $f.spurt($code);
    $f.Str
}
# run a script; returns the first line of stdout, or 'USAGE' when dispatch failed
sub first-line(Str $file, *@args) {
    my $p = run($*EXECUTABLE, $file, |@args, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $p.exitcode == 2 ?? 'USAGE' !! ($out.lines[0] // '')
}

my $a = script('a.raku', q:to/END/);
    sub MAIN(Str :$foo is required, Bool :$verbose = False) { say "foo=$foo v=$verbose" }
    END
check('--foo abc pairs (Str named)',        first-line($a, '--foo', 'abc'), 'foo=abc v=False');
check('…and composes with a Bool after',    first-line($a, '--foo', 'abc', '--verbose'), 'foo=abc v=True');
check('--foo=abc still works',              first-line($a, '--foo=abc'), 'foo=abc v=False');
check('consumption is unconditional',       first-line($a, '--foo', '--verbose'), 'foo=--verbose v=False');

my $d = script('d.raku', q:to/END/);
    sub MAIN(Int :$n is required) { say "n=$n" }
    END
check('Int named does NOT pair',            first-line($d, '--n', '42'), 'USAGE');

my $sp = script('sp.raku', q:to/END/);
    sub MAIN(Str :$foo, Bool :$verbose = False, *@rest) { say "foo=[{$foo//'U'}] v=$verbose rest=[{@rest.join(',')}]" }
    END
check('pairing works with a slurpy present', first-line($sp, '--foo', 'abc', 'xx'), 'foo=[abc] v=False rest=[xx]');
check('a positional token ends options',     first-line($sp, 'xx', '--verbose'), 'foo=[U] v=False rest=[xx,--verbose]');
check('a bare -- is consumed, all positional after', first-line($sp, '--', '--foo=a'), 'foo=[U] v=False rest=[--foo=a]');

my $o1 = script('o1.raku', q:to/END/);
    sub MAIN($pos?, Str :$foo) { say "pos=[{$pos//'U'}] foo=[{$foo//'U'}]" }
    END
check('pairing beats an optional positional', first-line($o1, '--foo', 'abc'), 'pos=[U] foo=[abc]');
check('…and a later token still fills it',    first-line($o1, '--foo', 'abc', 'xx'), 'pos=[xx] foo=[abc]');
check('a LEADING positional ends options first', first-line($o1, 'xx', '--foo', 'abc'), 'USAGE');

my $s2 = script('s2.raku', q:to/END/);
    sub MAIN(Bool :$v = False, Int :$n = 0, Str :$foo = 'd') { say "v=$v n=$n foo=$foo" }
    END
check('-v is a named option',               first-line($s2, '-v'), 'v=True n=0 foo=d');
check('-n=3 binds through single dash',     first-line($s2, '-n=3'), 'v=False n=3 foo=d');
check('--/v negates',                       first-line($s2, '--/v'), 'v=False n=0 foo=d');
check('-foo val pairs through single dash', first-line($s2, '-foo', 'val'), 'v=False n=0 foo=val');

my $neg = script('neg.raku', q:to/END/);
    sub MAIN($n) { say "n=$n" }
    END
check('-5 alone is (mis)read as an option', first-line($neg, '-5'), 'USAGE');
check('-- -5 passes the negative number',   first-line($neg, '--', '-5'), 'n=-5');

my $h2 = script('h2.raku', q:to/END/);
    multi MAIN('cmd', Str :$foo) { say "cmd foo=[{$foo//'U'}]" }
    multi MAIN(Str :$foo) { say "plain foo=[{$foo//'U'}]" }
    END
check('multi: pairing picks the fitting candidate', first-line($h2, '--foo', 'v'), 'plain foo=[v]');
check('multi: a literal positional ends options',   first-line($h2, 'cmd', '--foo', 'v'), 'USAGE');

# --- the type-sensitivity block: exactly which named params pair ----------
my $e = script('e.raku', q:to/END/);
    sub MAIN(:$u, Num :$x, Str:D :$s = "d", Str :$a, Str :$b, Str :$f) {
        say "u=[{$u//'U'}] x=[{$x//'U'}] s=$s a=[{$a//'U'}] b=[{$b//'U'}] f=[{$f//'U'}]"
    }
    END
check('an UNTYPED named does not pair',     first-line($e, '--u', 'val'), 'USAGE');
check('a Num named does not pair either',   first-line($e, '--x', '1.5'), 'USAGE');
check('Str:D pairs (the smiley is fine)',   first-line($e, '--s', 'val'),
      'u=[U] x=[U] s=val a=[U] b=[U] f=[U]');
check('two pairings in one command',        first-line($e, '--a', '1', '--b', '2'),
      'u=[U] x=[U] s=d a=[1] b=[2] f=[U]');
check('a one-letter -f pairs too',          first-line($e, '-f', 'val'),
      'u=[U] x=[U] s=d a=[U] b=[U] f=[val]');

my $b = script('b.raku', q:to/END/);
    sub MAIN(Str :$foo, Bool :$verbose = False) { say "foo=[{$foo//'U'}] v=$verbose" }
    END
check('an OPTIONAL Str named pairs like a required one', first-line($b, '--foo', 'abc'),
      'foo=[abc] v=False');

my $g2 = script('g2.raku', q:to/END/);
    sub MAIN(Str :$foo, *%named) { say "foo=[{$foo//'U'}] named={%named.raku}" }
    END
check('a *% slurpy does not disturb pairing', first-line($g2, '--foo', 'v'), 'foo=[v] named={}');

my $o3 = script('o3.raku', q:to/END/);
    sub MAIN(Str :$foo, Str :$bar) { say "foo=[{$foo//'U'}] bar=[{$bar//'U'}]" }
    END
check('a stray positional after a pairing still fails', first-line($o3, '--foo', 'a', 'b'), 'USAGE');

my $c = script('c.raku', q:to/END/);
    sub MAIN($pos, Str :$foo) { say "pos=$pos foo=[{$foo//'U'}]" }
    END
check('pairing cannot conjure a missing required positional', first-line($c, '--foo', 'abc'), 'USAGE');
check('the = form is also literal after the boundary', first-line($c, 'xx', '--foo=abc'), 'USAGE');

check('-xyz is one name, not a cluster',    first-line($s2, '-xyz'), 'USAGE');

# --- rule 4: %*SUB-MAIN-OPTS<named-anywhere> lifts the boundary -----------
# Options bind wherever they appear; `--foo abc` pairing follows them past
# the first positional; a bare `--` STILL ends option parsing.
my $na = script('na.raku', q:to/END/);
    my %*SUB-MAIN-OPTS = :named-anywhere;
    sub MAIN($pos, Str :$foo = 'd', Bool :$v = False) { say "pos=$pos foo=$foo v=$v" }
    END
check('named-anywhere: an option after a positional binds', first-line($na, 'xx', '--foo=abc'),
      'pos=xx foo=abc v=False');
check('named-anywhere: pairing follows past the boundary', first-line($na, 'xx', '--foo', 'abc', '-v'),
      'pos=xx foo=abc v=True');
check('named-anywhere: -- still ends options',             first-line($na, '--', 'xx', '--foo=abc'), 'USAGE');

# --- rule 5: a repeated option collects EVERY value -----------------------
# `--x=a --x=b` is :x(["a","b"]): it binds :@x whole, and it FAILS to bind a
# scalar Str :$x — Rakudo does not silently keep the last value, so neither
# do we.
my $rep = script('rep.raku', q:to/END/);
    sub MAIN(:@x) { say "x=[{@x.join(',')}]" }
    END
check('a repeated option collects into :@x',  first-line($rep, '--x=a', '--x=b'), 'x=[a,b]');
check('a single occurrence still binds :@x',  first-line($rep, '--x=a'), 'x=[a]');

my $rep2 = script('rep2.raku', q:to/END/);
    sub MAIN(Str :$x = 'd') { say "x=$x" }
    END
check('a repeated option does NOT bind a scalar', first-line($rep2, '--x=1', '--x=2'), 'USAGE');

unlink $work.add($_)
    for <a.raku d.raku sp.raku o1.raku s2.raku neg.raku h2.raku
         e.raku b.raku g2.raku o3.raku c.raku na.raku rep.raku rep2.raku>;
say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
