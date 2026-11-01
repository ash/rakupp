# Regression: `.AST` keeps the parentheses a program writes, and a hash
# subscript's key and adverbs, so `.AST.EVAL` runs the program that was written.
#
# A written `( … )` is a Circumfix::Parentheses upstream. The view built one
# only for a list, so every other parenthesized expression came back bare, and
# precedence then read it as something else:
#
#   (1 + 2) * 3          → 1 + 2 * 3            .AST.EVAL answered 7
#   (-$y) ** 2           → -$y ** 2             -9 for 9
#   ($t ?? 10 !! 20) + 1 → $t ?? 10 !! 20 + 1   10 for 11
#   (1 .. 4).sum         → 1 .. 4.sum           a Range for 10
#   (1 + 2; 3 + 4)       → 1 + 2; 3 + 4         7 for (3, 7)
#
# The round-trip harness saw none of these — each parses — only
# `(0 max 5) min 3`, which does not. Beside them, the angle subscript rendered
# its key in quotes (`%h<a>` as `%h<"a">`, the key `"a"`), and every
# subscript adverb was dropped (`%h{"a"}:delete` deleted nothing).
#
# Two parts: the rendering, against Rakudo 2026.09's `.AST.DEPARSE` of the same
# source byte for byte; and the PROGRAM, `EVAL $src` against `$src.AST.EVAL`
# on this engine.
#
# Contract: exit 0 + last line PASS.
use experimental :rakuast;
use MONKEY-SEE-NO-EVAL;

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}

# ---- part 1: the rendering ---------------------------------------------------
my @render =
    Q[(1 + 2) * 3],
        "(1 + 2) * 3\n",
    Q[my $b = 1; 1 ~ ($b ~~ Int)],
        "my \$b = 1;\n1 ~ (\$b ~~ Int)\n",
    Q[my $x = (0 max 5) min 3; $x],
        "my \$x = (0 max 5) min 3;\n\$x\n",
    Q[my $y = -3; (-$y) ** 2],
        "my \$y = -3;\n(-\$y) ** 2\n",
    Q[my $t = 1; ($t ?? 10 !! 20) + 1],
        "my \$t = 1;\n(\$t ?? 10 !! 20) + 1\n",
    Q[my $z; ($z = 3) + 1],
        "my \$z;\n(\$z = 3) + 1\n",
    Q[(1 .. 4).sum],
        "(1 .. 4).sum\n",
    Q[my $w = 2; (1 < $w < 3) && 5],
        "my \$w = 2;\n(1 < \$w < 3) \&\& 5\n",
    Q[((1 + 2)) * 3],
        "((1 + 2)) * 3\n",
    Q[(1 + 2; 3 + 4)],
        "(1 + 2; 3 + 4)\n",
    Q[my %h = a => 1; (%h<a> // 0) + 1],
        "my \%h = a => 1;\n(\%h<a> // 0) + 1\n",
    Q[my %h = a => 1; %h<a b>],
        "my \%h = a => 1;\n\%h<a b>\n",
    Q[my %h = a => 1; %h<a>:exists; %h<a>:!exists; %h<a>:exists:kv],
        "my \%h = a => 1;\n\%h<a>:exists;\n\%h<a>:!exists;\n\%h<a>:exists:kv\n",
    Q[my %h = a => 1; %h{"a"}:delete],
        "my \%h = a => 1;\n\%h\{\"a\"}:delete\n",
    Q[my @a = 1; @a[0]:exists],
        "my \@a = 1;\n\@a[0]:exists\n",
    Q[my %h = a => 1; %h<>:k; %h{}:k],
        "my \%h = a => 1;\n\%h<>:k;\n\%h\{}:k\n",
    Q[my %h = a => 1; my $v = 1; %h<a>:exists($v)],
        "my \%h = a => 1;\nmy \$v = 1;\n\%h<a>:exists(\$v)\n",
;
for @render -> $src, $want { check $src.AST.DEPARSE, $want, "DEPARSE of {$src.raku}" }

# ---- part 2: the same program ------------------------------------------------
my @run =
    'arithmetic grouping', Q[(1 + 2) * 3],
    'a prefix grouped',    Q[my $y = -3; (-$y) ** 2],
    'a ternary grouped',   Q[my $t = 1; ($t ?? 10 !! 20) + 1],
    'a range grouped',     Q[(1 .. 4).sum],
    'a semicolon list',    Q[(1 + 2; 3 + 4).raku],
    'max then min',        Q[my $x = (0 max 5) min 3; $x],
    'smartmatch grouped',  Q[my $b = 1; 1 ~ ($b ~~ Int)],
    'an angle key',        Q[my %h = a => 1; (%h<a> // 0) + 1],
    'a word-list slice',   Q[my %h = a => 1, b => 2; %h<a b>.sum],
    ':exists',             Q[my %h = a => 1; %h<a>:exists],
    ':!exists',            Q[my %h = a => 1; %h<b>:!exists],
    ':delete',             Q[my %h = a => 1, b => 2; %h{"a"}:delete; %h.elems],
    'a zen :k',            Q[my %h = a => 1, b => 2; %h<>:k.sort.join(",")],
;
for @run -> $label, $src {
    my $direct = (try EVAL $src) // "died: {$!.message.lines[0]}";
    my $via    = (try $src.AST.EVAL) // "died: {$!.message.lines[0]}";
    check ~$via, ~$direct, "$label: .AST.EVAL runs what EVAL runs";
}

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
