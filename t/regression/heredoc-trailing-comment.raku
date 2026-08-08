# BROKE: anything between a heredoc's introducer and the end of that line —
# `q:to/A/;   # a note`, or merely trailing spaces — made the heredoc lose its
# FIRST body line, silently and with a clean exit code. tokenize() filled the
# pending body only when it saw the newline at the very top of its loop, but
# skipWhitespaceAndComments() ran after that check and consumed the comment AND
# the newline, so the fill happened one line late: line 1 of the body leaked
# into the token stream as code and the string started at line 2.
# Found by showcase/python (`q:to/CONFIG/;  # …`) and by the raku.online front
# page; both engines agree on the correct result, so the divergence was ours.
# FIXED: the skipper stops on a newline while a heredoc is pending, and
# tokenize() re-checks for the fill after skipping.

my $with-comment = q:to/A/;    # a trailing comment
    one
    two
    A
die "heredoc lost a line to a trailing comment: {$with-comment.raku}"
    unless $with-comment eq "one\ntwo\n";

my $with-spaces = q:to/B/;
    one
    two
    B
die "heredoc lost a line to trailing whitespace: {$with-spaces.raku}"
    unless $with-spaces eq "one\ntwo\n";

my $plain = q:to/C/;
    one
    two
    C
die "plain heredoc broken: {$plain.raku}" unless $plain eq "one\ntwo\n";

# two on one line, with a comment after both — each keeps its own body
my ($x, $y) = q:to/X/, q:to/Y/;    # both bodies follow, in order
    first
    X
    second
    Y
die "paired heredocs broken: {$x.raku}/{$y.raku}"
    unless $x eq "first\n" && $y eq "second\n";

# an interpolating heredoc behind a comment still interpolates
my $n = 2;
my $interp = qq:to/D/;   # sum below
    sum = { $n + 1 }
    D
die "interpolating heredoc broken: {$interp.raku}" unless $interp eq "sum = 3\n";

# the real-world shape: a grammar parsing a heredoc'd config
grammar INI {
    token TOP     { <section>+ }
    token section { '[' <name> ']' \n <pair>* }
    token name    { <-[\]]>+ }
    token pair    { <key> '=' <value> \n? }
    token key     { \w+ }
    token value   { \N+ }
}
my $config = q:to/CONFIG/;   # the line that used to vanish
    [server]
    host=example.com
    CONFIG
my $m = INI.parse($config);
die "heredoc'd config did not parse" unless $m;
die "first section lost: {$m<section>[0]<name>}" unless ~$m<section>[0]<name> eq 'server';

say 'PASS';
