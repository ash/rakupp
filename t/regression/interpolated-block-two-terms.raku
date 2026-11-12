# Regression (#260): a malformed `{…}` block in a double-quoted string is a
# compile-time error, as Rakudo has it: `"{1 2}"` is two terms in a row, and
# `-c` refuses the file. The block's own parser kept the whole-file leniency
# (a statement may end without `;`, for a test file's TAP), so `say "{1 2}"`
# printed 2, `"{\\hsize=100pt}x"` died at run time as "Target is not
# assignable" naming the enclosing call, and a sub never called never died at
# all. An interpolated block is a snippet, as an EVAL is, and has its rule;
# its error's `^` stands under the term in the string, not after the string.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub compile(Str $code) {
    my $p = run $*EXECUTABLE, '-c', '-e', $code, :out, :err;
    ($p.exitcode, $p.out.slurp(:close), $p.err.slurp(:close))
}

for 'say "{1 2}";',
    'my $g = "x"; $g.IO.spurt("{\\\\hsize=100pt}x");',
    'sub unused { "{\\\\hsize=100pt}x" }; say "ok";',
    "say qq:to/END/;\n  \{ 1\n    2 }\n  END",
    'say "a" ~ "b {1 2} c";' -> $code {
    my ($rc, $out, $err) = compile($code);
    @fail.push("refused at compile time: {$code.raku} (rc $rc, {$out.raku})")
        unless $rc != 0 && $out !~~ /'Syntax OK'/;
    @fail.push("names the fault: {$code.raku}: {$err.lines.head(3).join(' | ')}")
        unless $err ~~ /'Two terms in a row' | 'Missing block'/;
}

# the excerpt's `^` is under the second term, not after the string
if $*EXECUTABLE.basename.starts-with('rakupp') {
    my ($, $, $err) = compile('say "{1 2}";');
    my @l = $err.lines;
    my $at = @l.first(*.contains('say "{1 2}"'), :k);
    @fail.push("the caret: {@l.join(' | ')}")
        unless $at.defined && @l[$at + 1].index('^') == @l[$at].index('2}');
}

for 'say "{ my $y = 2; $y * 3 }";', 'say "{}"; say "{ }"; say "{;}";',
    'my %h = a => 1; say "%h{"a"} {%h<a> + 1}";', "say qq:to/END/;\n  \{ 1;\n    2 }\n  END",
    'say "{2²}";', 'say "{ .uc for <a b> }";' -> $code {
    my ($rc, $out, $err) = compile($code);
    @fail.push("still compiles: {$code.raku}: {$err.lines.head(3).join(' | ')}") unless $rc == 0;
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
