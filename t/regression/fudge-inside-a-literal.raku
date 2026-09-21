# Regression: the roast fudge rewrite reached into string literals.
#
# `applyRakudoFudge` (top of src/Lexer.cpp, run from the Lexer's constructor)
# honours roast's `#?rakudo …` directives by editing the source as TEXT, line by
# line. It had no idea where a literal began, so a heredoc carrying a line that
# looks like a directive came back with the line under it commented out and a
# `skip(…)` call spliced in front — a program's DATA rewritten, not its code.
# Rakudo prints such a heredoc verbatim: its fudge preprocessing is a separate
# tool, not part of the compiler.
#
# The pass now asks `scanSpans` which lines are the inside of a literal (the
# same scanner `--fmt` leans on) and passes those through whole. A line counts
# as data when its first byte is inside a classified span that began EARLIER,
# which is what separates a heredoc body from a comment — and a comment is what
# a directive is, so the directives themselves still work. Both halves are
# checked below; the first half is rakupp-specific (Rakudo has no fudge).
#
# The literals here are written out in full ON PURPOSE. If the rewrite ever
# reaches into them again it will rewrite THIS file, and the comparisons fail.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc\n  got:  {$got.raku}\n  want: {$want.raku}" }
}

# ---- 1. a heredoc keeps every byte -------------------------------------
my $here = qq:to/END/;
    a line
    #?rakudo skip "why"
    ok 1, "guarded";
    last line
    END
ck $here.lines.List,
   ('a line', '#?rakudo skip "why"', 'ok 1, "guarded";', 'last line'),
   'a heredoc keeps its directive-shaped lines';

# ---- 2. so does an ordinary multi-line string ---------------------------
my $str = "one
#?rakudo skip 'two'
ok 1, 'three';
four";
ck $str.lines.List,
   ('one', "#?rakudo skip 'two'", "ok 1, 'three';", 'four'),
   'a multi-line string keeps them too';

# ---- 3. …and a todo directive, which rewrites a different way -----------
my $todo = qq:to/END/;
    #?rakudo todo "nope"
    ok 1, "under a todo";
    END
ck $todo.lines.List, ('#?rakudo todo "nope"', 'ok 1, "under a todo";'),
   'the todo verb does not reach in either';

# ---- 4. the directives still do their job on real code -----------------
sub tap(@body) {
    my $f = $*TMPDIR.add("fudge-lit-{$*PID}.raku");
    $f.spurt(@body.join("\n") ~ "\n");
    my $p = run($*EXECUTABLE, $f.absolute, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $f.unlink;
    $out.lines.List
}

ck tap(['use Test;', 'plan 3;', 'ok 1, "first";',
        '#?rakudo skip "not run"', 'ok 0, "skipped";', 'ok 1, "last";']),
   ('1..3', 'ok 1 - first', 'ok 2 -  # skip not run', 'ok 3 - last'),
   'a skip directive on real code still skips';

ck tap(['use Test;', 'plan 2;', '#?rakudo todo "known"',
        'ok 0, "todo-ed";', 'ok 1, "last";']),
   ('1..2', 'not ok 1 - todo-ed # TODO known', 'ok 2 - last'),
   'a todo directive on real code still marks';

# ---- 5. a directive inside POD is inert, the one after it is not --------
ck tap(['use Test;', 'plan 1;', '=begin pod', '#?rakudo skip "in pod"',
        'ok 0, "in pod";', '=end pod', '#?rakudo skip "real"', 'ok 0, "real";']),
   ('1..1', 'ok 1 -  # skip real'),
   'POD is data, the line after the block is code';

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
