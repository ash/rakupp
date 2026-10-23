# A `<name>` that calls a lexical `my token` / `my regex` / `my rule`, in the
# patterns .comb, .split and .contains compile for themselves. Each compiled
# the pattern alone, without the resolver .subst and `~~` hand the regex
# engine, so the call matched as nothing:
#
#   * .comb asked whether `&name` was a regex routine in scope and, if so,
#     took the `~~` machinery. Compiled code declares its tokens into the
#     interpreter's table alone (RT.registerNamedRegex) and binds no `&name`,
#     so under --exe, with -O and without, `'abq xyzq aq'.comb(/ <?after
#     <two>> 'q' /)` answered all three q's and `.comb(/<two>/)` twelve empty
#     strings. The interpreter was right;
#   * .split and .contains never asked, in either engine: `'zzz'.contains(
#     /<two>/)` was True and `'zz'.split(/<two>/)` cut between every
#     character, a delimiter list's regex the same.
#
# t/exe/run.raku cannot see the first: it compares -O with plain --exe, and
# both had it. So this file compiles its program both ways and holds each,
# and the interpreter, to the lines below — Rakudo 2026.09's, but for the
# three lookbehinds at the end. Rakudo matches a subrule inside `<?after …>`
# against the reversed text (`'baq'` passes `<?after <two>> q`, `'abq'` does
# not); those lines hold the lookbehind's own definition, a <two> ending
# where the q starts, which the interpreter gives.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $bin = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-lexical-token-calls-{$*PID}");
$dir.mkdir;
LEAVE { try { .unlink for $dir.dir; $dir.rmdir } }

my $src = q:to/END/;
    my token two { 'ab' | 'xyz' }
    my regex dig { \d+ }
    my rule pr { 'a' 'b' }
    sub inner($t) { my token hd { 'h' \d }; $t.comb(/ <hd> /).join(',') }
    my $s = 'abq xyzq aq';
    say $s.comb(/ <two> /).raku;
    say 'a12b345'.comb(/ <dig> /).raku;
    say 'a12b345'.comb(/ <dig> /, 1).raku;
    say 'a b'.comb(/ <pr> /).raku;
    say inner('h1 x h2');
    say 'a12b345'.split(/ <dig> /).raku;
    say 'zz'.split(/ <two> /).raku;
    say 'xabyxyzz'.split([/ <two> /, 'y']).raku;
    say 'zzz'.contains(/ <two> /);
    say 'zzab'.contains(/ <two> /);
    say 'abzz'.contains(/ <two> /, 1);
    say $s.comb(/ <?after <two>> 'q' /).raku;
    say $s.comb(/ <!after <two>> 'q' /).raku;
    say $s.split(/ <?after <two>> 'q' /).raku;
    END

my @want =
    '("ab", "xyz").Seq',            # compiled: twelve empty strings
    '("12", "345").Seq',            # a `my regex`
    '("12",).Seq',                  # …with a limit
    '("a b",).Seq',                 # a `my rule`
    'h1,h2',                        # a token declared in a sub
    '("a", "b", "").Seq',           # both engines: a cut at every character
    '("zz",).Seq',
    '("x", "", "", "z").Seq',       # a delimiter list
    'False',                        # both engines: True
    'True',
    'False',                        # …from a position
    '("q", "q").Seq',               # compiled: three q's (Rakudo: none)
    '("q",).Seq',                   # (Rakudo: three)
    '("ab", " xyz", " aq").Seq';    # (Rakudo: the whole string)

sub held($what, $out) {
    my @got = $out.lines;
    for ^max(+@want, +@got) -> $k {
        @fail.push("$what, line {$k + 1}: got {(@got[$k] // '(nothing)').raku}, want {(@want[$k] // '(nothing)').raku}")
            if (@got[$k] // '') ne (@want[$k] // '');
    }
}

my $f = $dir.add("calls.raku");
$f.spurt($src);
my $interp = run($bin, $f.absolute, :out, :err);
held('interpreted', $interp.out.slurp(:close)); $interp.err.slurp(:close);

for (('--exe', '-O'), ('--exe',)) -> @mode {
    my $exe = $dir.add("calls-{+@mode}.bin");
    my $c = run($bin, |@mode, '-q', $f.absolute, '-o', $exe.absolute, :out, :err);
    $c.out.slurp(:close); my $cerr = $c.err.slurp(:close);
    if !$exe.e { @fail.push("{@mode} did not compile: $cerr"); next }
    my $r = run($exe.absolute, :out, :err);
    held(@mode.join(' '), $r.out.slurp(:close)); $r.err.slurp(:close);
}

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
