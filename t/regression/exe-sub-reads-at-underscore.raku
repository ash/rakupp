# A compiled sub that reads the implicit `@_` or `%_` (2026-11-06). `--exe`
# gave a sub with no signature only the parameters its placeholders imply, and
# `@_` is no placeholder, so the C++ it wrote named `v_a_5f` — `@_` — without
# declaring it, clang stopped there, and the build failed outright: it did not
# fall back to bundling. Now such a sub has the `*@_` and `*%_` they mean (as
# its `.signature` says, and as the interpreter binds them), an anonymous
# `sub { @_ }` too; a lazy argument makes `@_` lazy, as Rakudo's is. A plain
# block that reads `@_`, and an `if` block — which the interpreter calls with
# the condition — are bundled rather than guessed at.
#
# The interpreter's own `@_` changed with it: it kept a lazy argument whole,
# where Rakudo's `@_`, like any `*@a`, is lazy then (`f(1..*)`).
#
# The first program must compile natively, with and without -O, and print
# Rakudo 2026.09's answers; the second must build — bundled, and saying so —
# and agree with the interpreter. Contract: exit 0 + last line PASS.

my $rakupp = $*RAKU.compiler.name eq 'Raku++';
unless $rakupp {
    note 'exe-sub-reads-at-underscore: not rakupp, nothing to compile';
    say 'PASS';
    exit 0;
}

my $work = $*TMPDIR.add("exe-sub-reads-at-underscore-$*PID");
mkdir $work;
my $fails = 0;

sub check(Str $desc, $got, $want) {
    if $got eq $want { say "ok - $desc" }
    else {
        $fails++;
        say "not ok - $desc";
        note "GOT  [{$got}]\nWANT [{$want}]";
    }
}

my $native = $work.add('native.raku');
$native.spurt: q:to/END/;
    sub names { @_.map(*.^name).join(',') }
    sub lazy { @_.is-lazy ~ ' ' ~ @_.head(3).join(',') }
    sub rest { $^a ~ '|' ~ @_.join(',') }
    sub opts { %_.keys.sort.join(',') }
    sub both { @_.elems ~ ' ' ~ %_.elems }
    sub total { my $t = 0; $t += $_ for @_; $t }
    my &anon = sub { @_.elems };
    my %h = a => 1, b => 2;
    my @x = 1, 2, 3;
    say names(%h), ' | ', names(@x), ' | ', names((1, (2, 3))), ' | ', names(1..3), ' | ', names(1, 2);
    say lazy(1..*), ' / ', lazy(1..3);
    say rest(1, 2, 3), ' ', opts(b => 1, a => 2), ' ', both(1, 2, z => 3), ' ', total([4, 5]), ' ', anon(@x, 4);
    say &names.signature.raku, ' ', &opts.signature.raku, ' ', &both.signature.raku, ' ', &anon.signature.raku;
    END

my $want = (
    'Pair,Pair | Int,Int,Int | Int,Int,Int | Int,Int,Int | Int,Int',
    'True 1,2,3 / False 1,2,3',
    '1|2,3 a,b 2 1 9 4',
    ':(*@_) :(*%_) :(*@_, *%_) :(*@_)',
).join('|');

my $i = run($*EXECUTABLE, $native.Str, :out, :err);
check('interpreted', $i.out.slurp(:close).lines.join('|'), $want);
$i.err.slurp(:close);

for ('', '-O') -> $o {
    my $tag = $o ?? "compiled $o" !! 'compiled';
    my $bin = $work.add('native' ~ $o);
    my $b = run($*EXECUTABLE, '--exe', |($o ?? ($o,) !! ()), '-o', $bin.Str, $native.Str, :out, :err);
    my $bout = $b.out.slurp(:close) ~ $b.err.slurp(:close);
    if $b.exitcode != 0 { $fails++; say "not ok - $tag: compiles"; note $bout; next }
    check("$tag: natively, no interpreter bundled", so($bout.contains('Compiled (native)') && !$bout.contains('bundling')), True);
    my $p = run($bin.Str, :out, :err);
    check("$tag: agrees", $p.out.slurp(:close).lines.join('|'), $want);
    $p.err.slurp(:close);
}

# A plain block's `@_` and an `if` block's are the interpreter's to decide:
# the program builds, bundled, and says what the interpreter says
my $bundled = $work.add('bundled.raku');
$bundled.spurt: q:to/END/;
    sub cond { if 7 { @_.join(',') } }
    say { @_.join('-') }(1, (2, 3)), ' ', cond();
    END
my $bi = run($*EXECUTABLE, $bundled.Str, :out, :err);
my $bwant = $bi.out.slurp(:close);
$bi.err.slurp(:close);
check('interpreted: a block takes its own @_', $bwant.trim, '1-2-3 7');
my $bbin = $work.add('bundled');
my $bb = run($*EXECUTABLE, '--exe', '-o', $bbin.Str, $bundled.Str, :out, :err);
my $bbout = $bb.out.slurp(:close) ~ $bb.err.slurp(:close);
check('a block reading @_ builds, bundled', so($bb.exitcode == 0 && $bbout.contains('bundling')), True);
if $bb.exitcode == 0 {
    my $p = run($bbin.Str, :out, :err);
    check('…and agrees with the interpreter', $p.out.slurp(:close), $bwant);
    $p.err.slurp(:close);
}

for $work.dir { .unlink }
$work.rmdir;
say $fails ?? 'FAIL' !! 'PASS';
