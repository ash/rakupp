# A regex literal where its VALUE is taken is the Regex object, compiled as
# interpreted: `/…/` and `rx/…/` are values, `m//` is always a match against
# `$_`, and a condition or a statement still matches the topic.
#
# Compiled natively (--exe, with -O and without), a regex literal was a match
# against `$_` in every position but those codegen had marked as values
# (exArg: an argument, an assignment, `&&`/`||`, a list). So
#
#   * a Pair's key was the Match: `'a b c'.trans(/\s/ => '_')` changed
#     nothing, and `(/b/ => 1).key` was a Match;
#   * a ternary's branch was the Match, or Nil when it failed:
#     `<ab cd>.grep(True ?? /c/ !! /a/)` kept both, matched against a Match;
#   * `/x/ xx 2` was two Nils;
#   * and the other way about, `m//` in a value position was the Regex:
#     `my $m = m/c/` stored the Regex, not the match.
#
# A ternary as a condition (`if False ?? /q/ !! /c/`) or as a statement takes
# its branches apart, so the regex there still matches. Rakudo sinks a
# statement's ternary into its branches; the interpreter does not, so that
# last line is held for the compiled builds alone (TODO-FOLLOW-UPS.md).
#
# Where a regex is asked whether it holds on the spot — under `?`, `!`,
# `so`, `not`, either side of `&&`/`||` — the answer is that match too. A
# Regex value's truth is a match against `$_`, which the runtime reads from
# the interpreter's scope, not from the topic a compiled loop holds
# natively: `for <xa yb ab> { say $_ if /a/ && /b/ }` printed xa and yb
# before, too.
#
# t/exe/run.raku cannot see any of these: it compares -O with plain --exe,
# and both had them. So this file compiles its program both ways and holds
# each, and the interpreter, to the lines Rakudo 2026.09 prints.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-regex-literal-values-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    $_ = 'abc';
    sub pick($c) { $c ?? /a/ !! /b/ }
    say (/b/ => 1).key.^name;
    say (m/b/ => 1).key.^name;
    say 'a b c'.trans(/\s/ => '_');
    say 'a1b2'.trans(/\d/ => '#', /a/ => 'A');
    say (True ?? /y/ !! /z/).^name;
    say (False ?? /y/ !! rx/z/).^name;
    say (True ?? m/b/ !! /z/).^name;
    say (False ?? /y/ !! True ?? /z/ !! 1).^name;
    say <ab cd>.grep(True ?? /c/ !! /a/).raku;
    say ('xbx' ~~ pick(False)).Str;
    say (/x/ xx 2).map(*.^name).join(',');
    my $m = m/c/;
    say $m.^name, ' ', $m.Str;
    say [m/a/].map(*.^name).join(',');
    say (True ?? /b/ !! /q/) ?? 'cond: y' !! 'cond: n';
    if False ?? /q/ !! /c/ { say 'if: y' } else { say 'if: n' }
    say ?(True ?? /z/ !! /b/), ' ', !(True ?? /z/ !! /b/);
    for <xa yb> { say "for: $_" if True ?? /b/ !! /a/ }
    for <xa yb> { say "so: $_ ", so(True ?? /b/ !! /a/) }
    given 'xyz' { when True ?? /y/ !! /q/ { say 'when: y' }; default { say 'when: n' } }
    for <xa yb ab> { say "and: $_" if /a/ && /b/ }
    for <xa yb ab> { say "or: $_" if /x/ || /y/ }
    for <xa yb> { say "tern-and: $_ ", ((True ?? /b/ !! /a/) && 'y').^name }
    for <xa yb> { say "not: $_ ", not(/a/), ' ', !/b/ }
    say <ab cd xy>.grep({ True ?? /c/ !! /a/ }).raku;
    say <ab cd xy>.grep(-> $x { $x ne 'q' && (True ?? /x/ !! /a/) }).raku;
    sub g($s) { given $s { return True ?? /b/ !! /q/ } }
    say g('zz').^name;
    $/ = Nil;
    False ?? /q/ !! /c/;
    say 'sunk: ', ($/ // 'none').Str;
    END

my @want =
    'Regex',              # a Pair's key (compiled: Match)
    'Match',              # …an m// one
    'a_b_c',              # .trans by a regex key (compiled: "a b c")
    'A#b#',
    'Regex',              # a ternary's branch (compiled: Nil)
    'Regex',              # …an rx// one
    'Match',              # …an m// one
    'Regex',              # …a ternary's
    '("cd",).Seq',        # .grep by a ternary (compiled: both)
    'b',                  # a sub's last statement
    'Regex,Regex',        # `xx` (compiled: Nil,Nil)
    'Match c',            # m// assigned (compiled: Regex)
    'Match',              # …in a list
    'cond: y',            # a ternary as a condition matches
    'if: y',
    'False True',         # …under ? and !
    'for: yb',            # …against the loop's topic
    'so: xa False',
    'so: yb True',
    'when: y',            # …as a `when` matcher
    'and: ab',            # `&&` of two regexes in a loop (before: xa and yb,
    'or: xa',             # …judged against the program's `$_`)
    'or: yb',
    'tern-and: xa Regex', # `&&` hands on the Regex, which did not match
    'tern-and: yb Str',
    'not: xa False True',
    'not: yb True False',
    '("cd",).Seq',        # a block's last statement, matched by .grep
    '("xy",).Seq',        # (before: none)
    'Regex',              # `return` of a ternary (before: Nil)
    'sunk: c';            # …and as a statement (the interpreter: none)
my $sunk = @want.end;

sub held($what, $out, :$skip = -1) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        next if $k == $skip;
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add("values.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close), :skip($sunk)); $interp.err.slurp(:close);

for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("values-{+@mode}.bin");
    my $c = run($bin, |@mode, $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    # bundled, the program would run interpreted, and test nothing here
    @fail.push("{@mode} bundled the program: $cerr") unless $cerr ~~ /'Compiled (native)'/;
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
