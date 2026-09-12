# Regression: `--fmt` — the rules that are in, and the gates that guard them
# (FMT-PLAN steps 2-4).
#
# The formatter rewrites only the plain whitespace BETWEEN classified spans, so
# the cases that matter are the ones where a rule must NOT fire: inside a
# heredoc, a multi-line string, POD. Three gates sit inside `formatSource()`
# rather than in the CLI — the input must parse, the formatted text must be the
# same program (serialized with line numbers stripped, so moving lines is
# allowed), and formatting twice must change nothing.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $dir = $*TMPDIR.add("rakupp-fmt-{$*PID}");
$dir.mkdir;
sub fmt($src, *@args) {
    my $f = $dir.add("in.raku");
    $f.spurt($src);
    my $p = run($*EXECUTABLE.Str, '--fmt', |@args, $f.Str, :out, :err);
    my $o = $p.out.slurp(:close);
    $p.err.slurp(:close);
    ($o, $p.exitcode)
}
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}

# R2 — trailing whitespace, and R3 — exactly one newline at EOF.
check fmt("say 1;   \nsay 2;")[0], "say 1;\nsay 2;\n", "trailing whitespace and final newline";
# …and a file that is already formatted comes back unchanged.
check fmt("say 1;\n")[0], "say 1;\n", "an already-formatted file is untouched";

# R6 — three or more blank lines collapse to two; one and two are the author's.
check fmt("say 1;\n\n\n\n\nsay 2;\n")[0], "say 1;\n\n\nsay 2;\n", "a run of blanks collapses to two";
check fmt("say 1;\n\nsay 2;\n")[0], "say 1;\n\nsay 2;\n", "a single blank stays";
check fmt("say 1;\n\n\nsay 2;\n")[0], "say 1;\n\n\nsay 2;\n", "a double blank stays";

# The heredoc is the case the whole architecture exists for: its body is the
# author's text, so neither rule may reach into it — not the trailing spaces on
# its lines, not the blank run inside it.
my $hd = Q:to/SRC/;
    my $t = q:to/END/;
    body   with trailing   


    END
    say $t;   
    SRC
my ($got, $rc) = fmt($hd);
check $got, $hd.subst("say \$t;   ", "say \$t;"), "a heredoc body is left exactly as written";

# The PARSE gate: a file that does not parse is not formatted, and nothing is
# written. Exit 3, the code `--ast-roundtrip` uses for the same thing.
my ($bad, $badrc) = fmt("my \$x = ;\n");
check $badrc, 3, "a file that does not parse is refused";
check $bad, "", "…and nothing is written";

# --check writes the NAME of a file that would change and exits 1; a clean file
# exits 0 and says nothing. That is the CI form.
my ($cn, $crc) = fmt("say 1;   \n", '--check');
check $crc, 1, "--check exits 1 when there is work";
check $cn.trim.ends-with("in.raku"), True, "…and names the file";
check fmt("say 1;\n", '--check')[1], 0, "--check exits 0 on a formatted file";

# --diff shows what would change, from a built-in line diff (no git).
my ($d, $drc) = fmt("say 1;   \nsay 2;\n", '--diff');
check $drc, 1, "--diff exits 1 when there is work";
check $d.contains("-say 1;   ") && $d.contains("+say 1;"), True, "…and shows the line";

.unlink for $dir.dir;
$dir.rmdir;
if @fail { .say for @fail; say "FAIL ({+@fail})"; exit 1 }
say "PASS";
