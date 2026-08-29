# BROKE: a heredoc's terminator is delimited like any other quote — `q:to«END»`,
# `q:to｢END｣`, `q:to「END」`, `q:to♥END♥` — and whitespace may sit between the
# introducer and that delimiter, which is what makes `q :to '' ` legal: it names
# the EMPTY terminator, and a blank line ends the body. rakupp reached the `:to`
# handling only along the ASCII-delimiter path, so every Unicode pair and every
# spaced form lexed as a call to an undeclared `q`; and where one did parse, the
# empty terminator was still dropped, because the marker string doubled as the
# "a heredoc is pending" flag. Reported as issue #31, with a quine that needs
# both halves at once.
#
# FIXED: the Unicode-delimiter and «» branches route through the same
# form-decided tail as the ASCII path — which also gave `qw«a b»` its word list
# and `qx«…»` its shell run, both of which had been answering with a plain
# string; whitespace before a quote or Unicode delimiter is skipped once an
# adverb has been seen; and a separate flag marks the pending heredoc.
#
# The line that did NOT move: without an adverb, `q 'x'` stays a call to a
# declared `sub q`, exactly as the adjacent `q('x')` does — see
# paren-is-not-a-quote-delimiter.raku. A one-pass lexer cannot know what is
# declared, and the adverb is what makes the quote form unambiguous.
#
# Contract: exit 0 + last line PASS.

my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eq $want;
}

# the delimiter pairs, all naming the same terminator
my $slash = q:to/END/;
    one
    END
check $slash, "one\n", 'q:to/END/';

my $guillemet = q:to«END»;
    one
    END
check $guillemet, "one\n", 'q:to«END»';

my $corner = q:to｢END｣;
    one
    END
check $corner, "one\n", 'q:to｢END｣';

my $cjk = q:to「END」;
    one
    END
check $cjk, "one\n", 'q:to「END」';

my $heart = q:to♥END♥;
    one
    END
check $heart, "one\n", 'q:to♥END♥ (any codepoint pairs with itself)';

my $bracket = q:to[END];
    one
    END
check $bracket, "one\n", 'q:to[END]';

# whitespace between the introducer and the delimiter
my $spaced = q :to 'END';
    one
    END
check $spaced, "one\n", "q :to 'END'";

my $spaced-uni = q :to «END»;
    one
    END
check $spaced-uni, "one\n", 'q :to «END»';

# the empty terminator: a blank line ends the body (no dedent — the closing
# line carries no indentation to strip)
my $blank = q :to '';
one
two

check $blank, "one\ntwo\n", "q :to '' is terminated by a blank line";

# the form still decides what the body becomes
my $n = 2;
my $interp = qq:to«E»;
    sum = { $n + 1 }
    E
check $interp, "sum = 3\n", 'qq:to«E» interpolates';

my $raw = Q:to«E»;
    a\nb
    E
check $raw, "a\\nb\n", 'Q:to«E» keeps every backslash';

# …and the same tail is what makes these two forms answer with more than a
# plain string when a Unicode delimiter carries them
check qw«a b».elems.Str,   '2',  'qw«a b» is a word list';
check qw♥a b♥.elems.Str,   '2',  'qw♥a b♥ is a word list';
check q:w«a b».elems.Str,  '2',  'q:w«a b» is a word list';
check qx«echo hi».trim,    'hi', 'qx«echo hi» runs the command';

sub out-of(Str $code) {
    my $p = run($*EXECUTABLE, '-e', $code, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // ''
}

# issue #31's quine: the heredoc body is the second line, and `x 2` repeats it
my $quine = q:to/SRC/;
say q :to '' x 2;
say q :to '' x 2;

SRC
my $proc = run($*EXECUTABLE, '-e', $quine, :out, :err);
my $printed = $proc.out.slurp(:close);
$proc.err.slurp(:close);
check $printed, $quine, "issue #31: `q :to '' x 2` quine prints its own source";

# no adverb, no quote: a declared routine keeps its name
check out-of('sub q($x) { "R:$x" }; say q "a"'), 'R:a',
      'a spaced `q "…"` with no adverb is still a call to a declared q';
check out-of('sub q($x) { "R:$x" }; say q(\'b\')'), 'R:b',
      '…as is the adjacent paren form';

if @fail {
    note "FAILED: @fail.join('; ')";
    say 'FAIL';
}
else {
    say 'PASS';
}
