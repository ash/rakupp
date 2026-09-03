# Regression: two parser tolerances from the 2026-09-03 top-100 chunk work.
# Both match Rakudo 2026.08 exactly (bug-for-bug where the module's source is
# itself malformed but Rakudo runs it).

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; note "not ok - $what" unless $cond }

# --- B3: a short-circuit infix glued to `(` in TERM position -------------------
# `&&(EXPR)` / `||(EXPR)` at statement start is the value of that
# parenthesised expression, the operator dropped — as Rakudo reads it (with a
# SPACE Rakudo errors too). TOML::NQP:255 writes `return @part if COND;` then a
# `&&(more);` line; LLM::DWIM rides in on TOML, and both converted.
sub tml($x) {
    return 'hit' if $x == 5;
               &&($x > 0
               || $x < -9);
    'miss'
}
ok(tml(5) eq 'hit', 'the modifier return still fires');
ok(tml(3) eq 'miss', '…and the orphaned && line is a harmless term');
ok((do { &&(1, 2, 3) }).elems == 3, '&&(a,b,c) drops the operator, keeps the list');
ok((do { ||(7) }).elems == 1, '||(x) drops the operator too');

# --- B2: a heredoc terminator on a CRLF line -----------------------------------
# The terminator line ends in '\r' on a CRLF source; stripped, `TEXT\r` matches
# `TEXT` instead of running the heredoc away to EOF. Text::Markdown ships its
# tests CRLF. EVAL a CRLF-lined program to exercise the lexer path.
my $crlf = "my \$t = q:to/END/;\r\nhello\r\nworld\r\nEND\r\nsay \$t.lines.elems;\r\n";
my $out = q{};
{
    my $tmp = $*TMPDIR.add("rakupp-crlf-{$*PID}.raku");
    $tmp.spurt($crlf);
    my $p = run($*EXECUTABLE, $tmp.Str, :out);
    $out = $p.out.slurp(:close).trim;
    $tmp.unlink;
}
ok($out eq '2', "a heredoc closes on a CRLF terminator line (got '$out')");

say $fails ?? "FAIL ($fails)" !! "PASS";
