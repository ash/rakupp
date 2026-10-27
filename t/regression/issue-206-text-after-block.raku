# Issue #206: two statements on one line with nothing between them but a
# block's closing `}` compiled here, as in a `given` whose `when` blocks share
# a line:
#
#     sub f($x) { given $x { when 'a' { 'A' } when 'b' { 'B' } default { 'other' } } }
#
# printed `A B other`, where Rakudo refuses to compile it. A `}` ends a
# statement by itself only at the end of its line; anywhere else the next
# statement needs a `;`. A module that passed its whole suite here failed to
# load under Rakudo. EVAL already refused it (enforceStmtSep's strict mode);
# whole files skipped the check, and class, role, grammar and module bodies
# had no check at all.
#
# Rakudo says "Strange text after block (missing semicolon or comma?)". The
# wording here is deliberately our own: it says what is missing before what,
# and why, and the excerpt puts a `^` where the next statement starts:
#
#     ===SORRY!=== Parse error at line 1: Missing ';' before 'when': a block's '}' ends a statement only at the end of a line
#           1 | sub f($x) { given $x { when 'a' { 'A' } when 'b' { 'B' } default { 'other' } } }
#             |                                         ^
#
# Rakudo rejects every `bad` case and accepts every `good` one, so this file
# runs under `rakudo` too; there the wording checks take Rakudo's words.
# Contract: exit 0 + last line PASS.
my @fail;
my $rakupp = $*RAKU.compiler.name eq 'Raku++';
my $exe = $*EXECUTABLE.absolute;
my $dir = $*TMPDIR.add("rakupp-206-{$*PID}");
$dir.mkdir;
my $n = 0;

sub compile($src, :$c = True) {
    my $f = $dir.add("t{$n++}.raku");
    $f.spurt($src);
    my $p = run($exe, |('-c' if $c), ~$f, :out, :err);
    my $err = $p.err.slurp(:close);
    my $out = $p.out.slurp(:close);
    ($p.exitcode, $out, $err, $f)
}

# refused at compile time, naming what follows the `}`
sub bad($src, $next, $why) {
    my ($code, $, $err) = compile($src);
    @fail.push("$why: exited 0, should have refused to compile") if $code == 0;
    my $want = $rakupp
        ?? "Missing ';' before '$next': a block's '}' ends a statement only at the end of a line"
        !! 'Strange text after block';
    @fail.push("$why: wanted «$want», got «{$err.lines[0] // '(no output)'}»")
        unless $err.contains($want);
}

# still compiles
sub good($src, $why) {
    my ($code, $, $err) = compile($src);
    @fail.push("$why: should compile, got «{$err.lines[0] // '(exit ' ~ $code ~ ')'}»")
        unless $code == 0;
}

# --- the issue's program, and its fix ------------------------------------
bad q:to/END/, 'when', "the issue's given/when line";
    sub f($x) { given $x { when 'a' { 'A' } when 'b' { 'B' } default { 'other' } } }
    say f('a'), ' ', f('b'), ' ', f('c');
    END
good q:to/END/, "…with a ';' after each block";
    sub f($x) { given $x { when 'a' { 'A' }; when 'b' { 'B' }; default { 'other' } } }
    say f('a'), ' ', f('b'), ' ', f('c');
    END
good "given 1 \{\n    when 1 \{ 'A' \}\n    when 2 \{ 'B' \}\n\}\n", '…or each on its own line';

# --- statements after a block's `}` ---------------------------------------
bad 'sub k { 1 } sub v { 2 }', 'sub', 'two subs';
bad 'if 1 { } say 2', 'say', 'a call after an if';
bad 'if 1 { } else { } say 1', 'say', '…after its else';
bad 'my $c = { 1 } say 2', 'say', 'after a block as a value';
bad 'my $c = sub { 1 } say 2', 'say', 'after an anonymous sub';
bad 'try { die } CATCH { }', 'CATCH', 'a CATCH after try';
bad 'class A { has $.x } A.new.say', 'A', 'after a class';
bad 'module M { } say 1', 'say', 'after a module';
bad 'my token c { a } say 1', 'say', 'after a token';
bad 'sub f { 1 } #`(x) say 2', 'say', 'past an embedded comment';
bad 'if 1 { } $x = 2', '$x', 'a variable';
bad 'for 1..2 { } .say', '.say', 'a method on $_';
bad 'if 1 { } "x y z".say', '"x y z"', 'a string, whole';
bad 'if 1 { } "a very long string literal here".say', '"a very long string …', 'a long one, cut';

# --- and inside package bodies, which had no check at all ------------------
bad 'class A { method a { 1 } method b { 2 } }', 'method', 'two methods';
bad 'class A { submethod BUILD { } method b { 2 } }', 'method', 'a submethod and a method';
bad 'role R { method a { 1 } method b { 2 } }', 'method', 'in a role';
bad 'grammar G { token a { x } token b { y } }', 'token', 'two tokens';
bad 'module M { sub a { 1 } sub b { 2 } }', 'sub', 'in a module';

# --- what a `}` may be followed by on its line ------------------------------
good 'sub f { 1 }; sub g { 2 }', "a ';'";
good 'sub f { 1 } ;; say 2', "two ';'";
good 'sub f { 1 } # comment', 'a comment';
good 'class A { method a { 1 }; method b { 2 } }', "a ';' between methods";
good "class A \{ method a \{ 1 \}\n method b \{ 2 \} \}", 'a newline between methods';
good 'grammar G { token a { x }; token b { y } }', "a ';' between tokens";
good '{ say 1 } if True', 'a statement modifier';
good '{ say 1 } for 1..2', 'a loop modifier';
good 'if 1 { } elsif 0 { } else { }', 'elsif and else';
good 'my @a = map { $_ }, 1..3', 'a comma after an argument block';
good 'say do { 1 } + 2', 'an infix after a do block';
good 'say quietly { class {} − class {} }', 'an infix after an anonymous class (Roast u2212-minus.t)';
good 'say quietly { class {} ~ "x" }', '…and another infix';
good "sub f \{ 1 \}\x0Csay 2", 'a form feed ends the line';
good "sub f \{ 1 \}\x2028say 2", 'so does LINE SEPARATOR';

# --- EVAL refused it already; the type is unchanged ------------------------
{
    EVAL 'given 1 { when 1 { 1 } when 2 { 2 } }';
    @fail.push('EVAL: compiled');
    CATCH {
        default {
            @fail.push("EVAL: threw {.^name}") unless $_ ~~ X::Syntax::Confused;
            @fail.push("EVAL: message «{.message}»")
                if $rakupp && .message ne "Missing ';' before 'when': a block's '}' ends a statement only at the end of a line";
        }
    }
}

# --- the excerpt points at it ---------------------------------------------
if $rakupp {
    my $line = q[sub f($x) { given $x { when 'a' { 'A' } when 'b' { 'B' } default { 'other' } } }];
    my ($code, $out, $err, $f) = compile("$line\nsay f('a');\n", :!c);
    @fail.push("excerpt: exit $code") unless $code == 1;
    @fail.push('excerpt: the program ran') if $out;
    my @want = "===SORRY!=== Parse error at line 1: Missing ';' before 'when': a block's '}' ends a statement only at the end of a line",
               "      1 | $line",
               '        | ' ~ ' ' x ($line.index('} when') + 2) ~ '^',
               "  at $f:1";
    @fail.push("excerpt: got\n$err") unless $err.lines.List eqv @want.List;

    # a tab stays a tab, and a character is one column however many bytes it is
    ($code, $out, $err) = compile("my \$é = 1;\tif 1 \{ \} say \$é\n", :!c);
    @fail.push("excerpt with a tab: got\n$err")
        unless ($err.lines[2] // "") eq "        | " ~ ' ' x 10 ~ "\t" ~ ' ' x 9 ~ '^';

    # …under the keyword of a `token`, whose token the lexer stamps where its
    # body ends, and past an embedded comment
    ($code, $out, $err) = compile("sub f \{ 1 \} token b \{ y \}\n", :!c);
    @fail.push("excerpt before a token: got\n$err")
        unless ($err.lines[2] // "") eq "        | " ~ ' ' x 12 ~ '^';
    ($code, $out, $err) = compile("sub f \{ 1 \} #`(x) say 2\n", :!c);
    @fail.push("excerpt past a comment: got\n$err")
        unless ($err.lines[2] // "") eq "        | " ~ ' ' x 18 ~ '^';
}

$dir.dir».unlink;
$dir.rmdir;
if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
