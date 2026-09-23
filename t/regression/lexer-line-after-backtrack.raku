# Regression: the lexer's line counter ran ahead after a LOOKAHEAD that
# backtracked across a line.
#
# Only `advance()` moves `line_`, so a scan that walks forward and then puts
# `pos_` back on its own leaves the counter counting the newlines it re-reads.
# `tryRuleDecl` is the one that showed: every `token`, `rule` or `regex` starts
# a rule-declaration attempt that skips whitespace — newlines included — hunting
# for the `{`, and those three words are perfectly ordinary WORDS inside a
# `< … >` list. roast's S02-literals/pairs.t names all three in one multi-line
# list of keywords, and a syntax error on its line 199 was reported at 201.
# Backtracks go through `mark()`/`rewind()` now (src/Lexer.h), which puts
# `line_` and `col_` back with `pos_`.
#
# Every line number below was checked against Rakudo 2026.08.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku}, want {$want.raku}" }
}

# Where does the engine SAY the error is? The programs below carry one
# deliberate syntax error, and the answer has to be the line it is on.
sub error-line(@body) {
    my $f = $*TMPDIR.add("lexline-{$*PID}.raku");
    $f.spurt(@body.join("\n") ~ "\n");
    my $p = run($*EXECUTABLE, '-c', $f.absolute, :out, :err);
    my $txt = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    $f.unlink;
    $txt ~~ /'line ' (\d+)/ ?? +$0 !! Nil
}

my $broken = 'my $x = ;';      # "Confused (got ';')" wherever it lands

# ---- the case that was broken: declarator words inside a word list -------
ck error-line(['my @w = <', '    alpha', '    token', '    rule', '    regex',
               '    omega', '>;', 'say @w.elems;', $broken]),
   9, 'three rule-declarator words in a multi-line list';

ck error-line(['my @w = <', '    token', '>;', $broken]),
   4, 'one of them is enough';

ck error-line(['my @w = <', '    rule', '    constant', '>;', 'say @w.elems;', $broken]),
   6, 'and `rule` beside a word that is not a declarator';

# ---- neighbours that were already right and must stay so ----------------
ck error-line(['my @w = <', '    alpha', '    omega', '>;', $broken]),
   5, 'a word list with no declarator word in it';

ck error-line(['=begin pod', '=begin item', 'text', '=end item', 'more',
               '=end pod', $broken]),
   7, 'a POD block with a nested block inside it';

ck error-line(['my @a = 1,2;', 'my @b = @a', '    »+»', '    @a;', $broken]),
   5, 'a hyper operator split over lines';

ck error-line(['my $s = q:to/END/;', 'body', 'END', $broken]),
   4, 'a heredoc';

ck error-line(['token foo { \d+ }', 'say 1;', $broken]),
   3, 'a real rule declaration, which consumes what it scans';

# ---- and the runtime side: Token::line reaches a backtrace --------------
my $f = $*TMPDIR.add("lexline-rt-{$*PID}.raku");
$f.spurt(qq:to/END/);
    my @w = <
        token
        rule
    >;
    die "boom";
    END
my $p = run($*EXECUTABLE, $f.absolute, :out, :err);
my $err = $p.out.slurp(:close) ~ $p.err.slurp(:close);
$f.unlink;
ck ($err ~~ /'line ' (\d+)/ ?? +$0 !! Nil), 5,
   'a runtime backtrace after such a list names the right line';

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
