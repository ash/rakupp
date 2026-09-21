# Regression: `--fmt` went quiet for the rest of a file once it met a roast
# fudge directive.
#
# The Lexer's constructor rewrites `#?rakudo …` directives (a `skip` verb turns
# the statement it guards into `skip(<reason>, N); # <the statement>`). That
# rewrite keeps one line in for one line out, so `Token::line` survives it —
# but it does not keep bytes, and `Token::off` indexes the rewritten string.
# Fmt's rule R5 asks the lexer which bytes are a `,` or `=>` TOKEN and marks
# them by offset in the ORIGINAL source; from the first directive on, those
# marks landed in the wrong place. R5 lays a mark down only where the bytes
# really do spell the token, so the damage was silence rather than corruption:
# every comma after the directive kept whatever spacing it was written with.
#
# The formatter now lexes with the rewrite turned off (`honourFudge` false),
# which is what any caller addressing the source by byte needs.
#
# The directive is spelled in two pieces below ON PURPOSE. The fudge rewrite is
# a raw-text pass with no idea where a literal begins, so a `#?rakudo` line
# written inside a heredoc in THIS file would be rewritten in place — the test
# input would arrive already fudged and prove nothing.
#
# Contract: exit 0 + last line PASS.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eq $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc\n  got:  {$got.raku}\n  want: {$want.raku}" }
}

sub fmt(*@body) {
    my $f = $*TMPDIR.add("fmt-fudge-{$*PID}.raku");
    $f.spurt(@body.join("\n") ~ "\n");
    my $p = run($*EXECUTABLE, '--fmt', $f.absolute, :out, :err);
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $f.unlink;
    $out.lines
}

my $directive = '#?' ~ 'rakudo skip "why"';
my @common = 'use Test;', 'plan 2;', 'ok 1,"before";';
my @rest   = 'ok 1, "guarded";', 'ok 1,"after";', 'my %h = a=>1;';

# The two inputs differ in ONE line, and it is a comment either way.
my @f = fmt(|@common, $directive,             |@rest);
my @p = fmt(|@common, '# an ordinary comment', |@rest);

ck @f[2], 'ok 1, "before";', 'a comma BEFORE the directive is spaced';
ck @f[5], 'ok 1, "after";',  'a comma AFTER the directive is spaced too';
ck @f[6], 'my %h = a => 1;', 'and a fat arrow after it';
ck @f[3], $directive,        'the directive line comes through verbatim';
ck @f[3, 4].join("|"), ($directive, 'ok 1, "guarded";').join("|"),
   'the guarded statement is formatted, not rewritten';
ck (@f[0..2], @f[4..6]).flat.join("\n"), (@p[0..2], @p[4..6]).flat.join("\n"),
   'the directive changes nothing but its own line';

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
