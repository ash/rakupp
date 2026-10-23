# Regression: a call to a routine nothing declares was refused before the
# program ran, but reported as a RUN-time error at line 0:
#
#   Undefined routine 'length'
#     in block <unit> at -e line 0
#
# where Rakudo says, at compile time,
#
#   ===SORRY!=== Error while compiling -e
#   Undeclared routine:
#       length used at line 1. Did you mean 'elems', 'chars', 'codes'?
#
# checkUndeclaredCalls found the name before anything ran (nothing printed),
# then threw a plain X::Undeclared::Symbols from the first name it met, with no
# line and no compile-time marking. For the main program it now gathers every
# such name with the line of its last use, sorted, in Rakudo's words — its
# fixed hints for names other languages have (`length`, `bytes`, `break`, a
# phaser in lower case, `local`, `foreach`, `quit`, …) and otherwise the
# nearest routine names — and throws it with filename and line, which the
# top-level printer gives the ===SORRY!=== banner. A word operator rakupp also
# takes as a call is no longer suggested: `len` is not "Did you mean 'leg'?".
# An EVAL keeps its own message. Every check verified against Rakudo.
#
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;

sub child(*@args) {
    my $p = run($*EXECUTABLE, |@args, :out, :err);
    my %r = out => $p.out.slurp(:close), err => $p.err.slurp(:close);
    %r<exit> = $p.exitcode;
    %r;
}
# Refused at compile time — nothing printed — with every one of these lines.
sub refuses($label, $code, *@want) {
    my %r = child('-e', $code);
    @fail.push("$label: exit={%r<exit>} out={%r<out>.raku} err={%r<err>.trim}")
        unless %r<exit> == 1 && %r<out> eq '' && %r<err>.contains('===SORRY!=== Error while compiling -e')
               && all(@want.map({ %r<err>.contains($_) }));
}

refuses 'length', 'say "before"; say length("Riga");',
    "Undeclared routine:\n    length used at line 1. Did you mean 'elems', 'chars', 'codes'?";
refuses 'len: no suggestion', 'say "before"; say len("Riga");',
    "Undeclared routine:\n    len used at line 1\n";
refuses 'a misspelling', 'say "before"; say chars2("Riga");',
    "chars2 used at line 1. Did you mean 'chars', 'chrs'?";
refuses 'the line of the last use', "say 0;\nfoo(1);\nsay 2; foo(2);\nfoo(3);",
    "foo used at line 4";
refuses 'several names, sorted', "say 1; length(1); foo(2);\nfoo(3); local(4)",
    "Undeclared routines:\n    foo used at line 2\n    length used at line 1. Did you mean 'elems', 'chars', 'codes'?\n"
    ~ "    local used at line 2 (in Raku please use temp (or dynamic var) instead)";
refuses 'a phaser in lower case', 'say 1; begin(42)', "begin used at line 1. Did you mean 'BEGIN'?";
refuses 'quit', 'say 1; quit(1)', "quit used at line 1 (in Raku please use 'exit' instead)";
refuses 'bytes', 'say 1; bytes("x")', "bytes used at line 1. Did you mean '.encode(\$encoding).bytes'?";

# a file is named as it was given, and its line counts from there
{
    my $file = $*TMPDIR.add("undeclared-routine-$*PID.raku");
    $file.spurt("say 1;\nlength(2);\n");
    my %r = child($file.Str);
    $file.unlink;
    @fail.push("file: exit={%r<exit>} out={%r<out>.raku} err={%r<err>.trim}")
        unless %r<exit> == 1 && %r<out> eq '' && %r<err>.contains("===SORRY!=== Error while compiling $file")
               && %r<err>.contains("length used at line 2. Did you mean 'elems', 'chars', 'codes'?");
}

# what the unit declares, or the setting has, still runs
for 'my &g = sub ($x) { $x * 2 }; say g(21);', '42',
    'say chars("Riga"), elems(<a b c>);', '43',
    'say min(3, 1, 2), max(3, 1, 2);', '13' -> $code, $want {
    my %r = child('-e', $code);
    @fail.push("runs: $code: exit={%r<exit>} out={%r<out>.raku} err={%r<err>.trim}")
        unless %r<exit> == 0 && %r<out>.trim eq $want;
}

# an EVAL's undeclared routine is still X::Undeclared::Symbols
try EVAL 'say 1; length(1)';
@fail.push("EVAL: {$!.^name}") unless $!.^name eq 'X::Undeclared::Symbols';

# ---------------------------------------------------------------------------
if @fail {
    .say for @fail;
    say "FAIL ({@fail.elems})";
    exit 1;
}
say "PASS";
