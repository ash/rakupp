# Regression: a delimited construct that never closed was ACCEPTED. The
# string-literal scanner in src/Lexer.cpp (and its dozen sibling scans: q//,
# the bracket/Unicode/guillemet quote forms, the bare `/…/` regex, `#`( … )`
# block comments, `=begin pod`, the bare `< … >` word list) all ended their
# loop on `!eof()` and then closed the token themselves, so
#
#     say "Hello";
#     say "unterminated
#
# printed both lines and exited 0 where Rakudo refuses to compile it. Found in
# the playground's Live mode, which recompiles on every keystroke and therefore
# feeds the parser half-written literals constantly — silently running them is
# the worst possible answer.
#
# Every scan now reports the runaway, in Rakudo's own two wordings: the bare
# quote forms name the CONSTRUCT ("Unable to parse expression in double
# quotes; couldn't find final '\"' (corresponding starter was at line N)"), the
# bracketed forms name the TERMINATOR ("Couldn't find terminator ] …"). Both
# quote the line the construct OPENED on, and exit 1.
#
# Rakudo rejects every `bad` case below and accepts every `good` one, so this
# file runs under `raku` too. Six wordings deliberately differ, because
# Rakudo's there are artifacts of how ITS grammar happened to fail rather than
# descriptions of the mistake: `Q(…` becomes "in argument list", `q[[…`
# becomes a combining-codepoint complaint, a bare `/…` leads with the
# metacharacter it choked on, and an unterminated `=begin` surfaces as
# "Preceding context expects a term, but found infix = instead".
# Contract: exit 0 + last line PASS.
my @fail;
my $rakupp = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-unterm-{$*PID}");
$dir.mkdir;
my $n = 0;

# Compile $src and return the diagnostic; asserts a NON-ZERO exit.
sub bad($src, $want, $why) {
    my $f = $dir.add("t{$n++}.raku");
    $f.spurt($src);
    my $p = run($rakupp, '-c', ~$f, :out, :err);
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    @fail.push("$why: exited 0, should have refused to compile") if $p.exitcode == 0;
    @fail.push("$why: wanted /$want/, got {$err.lines[0] // '(no output)'}")
        unless $err.contains($want);
}

# Compile $src and require that it still parses — the guard against a scanner
# that now cries runaway on a construct that DOES close.
sub good($src, $why) {
    my $f = $dir.add("t{$n++}.raku");
    $f.spurt($src);
    my $p = run($rakupp, '-c', ~$f, :out, :err);
    my $err = $p.err.slurp(:close);
    $p.out.slurp(:close);
    @fail.push("$why: should compile, got {$err.lines[0] // '(exit ' ~ $p.exitcode ~ ')'}")
        unless $p.exitcode == 0;
}

# --- the reported bug: bare quotes, at EOF and with more lines following ---
bad qq{say "Hello";\nsay "unterminated},
    q{Unable to parse expression in double quotes; couldn't find final '"' (corresponding starter was at line 2)},
    'double quote at EOF';
bad qq{say 'Hello';\nsay 'unterminated},
    q{Unable to parse expression in single quotes; couldn't find final "'" (corresponding starter was at line 2)},
    'single quote at EOF';
# the string swallows the rest of the file — still the same diagnosis, and the
# STARTER line is the one named (the report line moves to where input ran out)
bad qq{say "Hello";\nsay "unterminated\nsay 42;\nsay 43;\n},
    q{couldn't find final '"' (corresponding starter was at line 2)},
    'double quote with lines following';
bad qq{say 'Hello';\nsay 'unterminated\nsay 42;\n},
    q{couldn't find final "'" (corresponding starter was at line 2)},
    'single quote with lines following';
# a backslash-escaped quote does not close the string
bad qq{say "hi";\nsay "un\\"terminated\n}, q{couldn't find final '"'}, 'escaped quote is not a closer';

# and RUNNING (not just -c) the reported program must print nothing and exit 1 —
# the bug's real damage was that it ran the half-written program to completion
{
    my $f = $dir.add('run.raku');
    $f.spurt(qq{say "Hello";\nsay "unterminated});
    my $p = run($rakupp, ~$f, :out, :err);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    @fail.push("running it: exit {$p.exitcode}, want 1")            unless $p.exitcode == 1;
    @fail.push("running it: printed {$out.raku}, want nothing")     unless $out eq '';
    @fail.push("running it: no diagnostic on stderr")               unless $err.contains('===SORRY!===');
}

# --- q// qq// Q// and the bracket forms: Rakudo's terminator wording ---
bad qq{say q/unterminated;\n},   q{Couldn't find terminator / (corresponding / was at line 1)}, 'q//';
bad qq{say "hi";\nsay qq/oops;\n},  q{Couldn't find terminator / (corresponding / was at line 2)}, 'qq//';
bad qq{say "hi";\nsay q[oops;\n},   q{Couldn't find terminator ] (corresponding [ was at line 2)}, 'q[]';
bad qq{say "hi";\nsay qq\{oops;\n}, q{Couldn't find terminator } ~ '}' ~ q{ (corresponding } ~ '{' ~ q{ was at line 2)}, 'qq{}';
# `Q(` is NOT a quote: a paren carries arguments, so this is a call whose
# argument list runs off the end — the same reading Rakudo gives it ("Unable to
# parse expression in argument list; couldn't find final ')'"). It stays in this
# file because it LOOKS like the others and used to be one of them; the wording
# is ours, and describes the confusing token rather than the opener because the
# argument expression fails before the list terminator is looked for.
bad qq{say "hi";\nsay Q(oops;\n},   q{missing required term after infix}, 'Q( ) is a call, not a quote';
bad qq{say "hi";\nsay q[[oops];\n}, q{Couldn't find terminator ]] (corresponding [[ was at line 2)}, 'q[[ ]] doubled bracket';
bad qq{say "hi";\nsay qw<a b c;\n},  q{Couldn't find terminator > (corresponding < was at line 2)}, 'qw<>';
bad "say \"hi\";\nsay Q\x[AB]oops;\n", q{Couldn't find terminator }, 'guillemet Q«»';

# --- the bare `< … >` word list gets the quote-words wording ---
bad qq{say "hi";\nmy \@a = <a b c;\n},
    q{Unable to parse expression in quote words; couldn't find final '>' (corresponding starter was at line 2)},
    'bare < > word list';

# --- regex and substitution literals ---
bad qq{say "hi";\nsay rx/abc;\n},  q{Couldn't find terminator / (corresponding / was at line 2)}, 'rx//';
bad qq{say "hi";\nsay m\{abc;\n},  q{Couldn't find terminator } ~ '}', 'm{}';
bad qq{my \$s = "x";\n\$s ~~ s/abc/def;\n}, q{Malformed replacement part; couldn't find final /}, 's/// replacement';
bad qq{say "hi";\nsay "x" ~~ /abc;\n}, q{Couldn't find terminator / (corresponding / was at line 2)}, 'bare //';

# --- block comments and pod ---
bad qq{say "hi";\n#`\{ runaway\nsay 42;\n},
    q{Couldn't find terminator } ~ '}' ~ q{ (corresponding } ~ '{' ~ q{ was at line 2)}, '#`{ } comment';
bad qq{say "hi";\n#`( runaway\n}, q{Couldn't find terminator ) (corresponding ( was at line 2)}, '#`( ) comment';
bad qq{say "hi";\n=begin pod\ntext\n},
    q{Expected "=end pod" to terminate "=begin pod"; found end of file instead.}, '=begin pod';
bad qq{say "hi";\n=begin comment\ntext\n},
    q{Expected "=end comment" to terminate "=begin comment"},                    '=begin comment';
# an =end naming a DIFFERENT block does not close this one
bad qq{say "hi";\n=begin pod\n=end nope\n},
    q{Expected "=end pod" to terminate "=begin pod"},                            '=end of another block';
# the heredoc case was already right — keep it honest
bad qq{my \$t = q:to/END/;\nsome text\n}, q{Ending delimiter END not found for heredoc}, 'heredoc';

# --- and the constructs that DO close still compile ---
good qq{say "Hello";\nsay 'world';\n},                   'plain quotes';
good qq{say q/a/, qq/b/, Q[c], q[[d]], qw<e f>;\n},      'q-forms';
good qq{my \@a = <a b c>; say \@a;\n},                    'bare word list';
good qq{say "x" ~~ /a\\/b/; say rx\{y\}; my \$s = "q"; \$s ~~ s/q/r/;\n}, 'regex + subst';
good qq{say "hi"; #`\{ a comment \} say "bye";\n},        'block comment';
good qq{say "hi";\n=begin pod\ntext\n=end pod\n},         'closed pod block';
good qq{my \$t = q:to/END/;\ntext\nEND\nsay \$t;\n},       'closed heredoc';
good qq{say "a\{ 1 + 2 \}b";\n},                          'interpolated block';
good qq{say 1 < 2; say 3 > 2;\n},                        'less-than is not a word list';
good "say Q\x[AB]abc\x[BB];\n",                          'guillemet quote';
good qq{my \$t = q:to/END/;\ntext\nEND\n=begin pod\np\n=end pod\n}, 'heredoc then pod';

try { .unlink for $dir.dir; $dir.rmdir }

if @fail {
    die "unterminated-literal diagnostics broken:\n" ~ @fail.map({ "  - $_" }).join("\n");
}
say 'PASS';
