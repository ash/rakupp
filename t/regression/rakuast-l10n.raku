# Regression: `.AST($lang)` is a LOCALIZED parse (RAKUAST-PLAN P1-L10N).
#
# `'mein $a = 42; wenn … { sag … }'.AST("DE")` has to parse German keywords and
# answer the same tree the English source would. The translation is not ours: it
# is read out of the `role L10N::<lang>` the dist ships — the tokens out of the
# role's rules, the routine names by calling its `core2ast` — so this case
# builds a ROLE OF ITS OWN rather than depending on an installed dist. That
# keeps it offline, and it keeps it able to fail: a broken table gives a
# different program, not a missing one.
#
# The three shapes in the fixture are the three that were got wrong while this
# was written, each of them silently:
#   * a bare word (`wenn`);
#   * a body the generator QUOTED because it is not a simple word — Italian's
#     `scope-my` is `"il-mio"`, and skipping quoted bodies dropped `my`;
#   * a body outside ASCII (`füralle`), which an identifier test written with
#     `rakuIdentStart` drops — that one took `for` out of German.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}

my $dir = $*TMPDIR.add("rakupp-l10n-{$*PID}");
$dir.add("L10N").mkdir;
$dir.add("L10N/ZZ.rakumod").spurt: q:to/MOD/;
    role L10N::ZZ {
        use experimental :rakuast;
        token scope-my { "il-mio"}
        token block-if { wenn}
        token block-for { füralle}
        token block-else { sonst}
        token infix-eq { gleich}
        token phaser-END { ENDE}
        token package-class { klasse}
        token routine-method { methode}
        token scope-has { hat}
        method core2ast {
            my constant %mapping = "sag", "say", "elemente", "elems";
            my $ast := self.ast;
            my $name := $ast ?? $ast.simple-identifier !! self.Str;
            if %mapping{$name} -> $original {
                RakuAST::Name.from-identifier($original)
            }
            else {
                $ast // RakuAST::Name.from-identifier($name)
            }
        }
    }
    MOD

# The program under test, in the fixture language. `zeige` and `Punkt` are the
# program's OWN names and must survive untranslated.
my $src = q:to/ZZ/;
    il-mio @z = 1, 2, 3;
    il-mio $s = 0;
    füralle @z -> $e { $s = $s + $e }
    klasse Punkt {
        hat $.x;
        methode zeige() { sag "x=" ~ $!x }
    }
    wenn $s gleich "6" {
        sag "summe=" ~ $s ~ " n=" ~ @z.elemente;
    }
    sonst { sag "nein" }
    Punkt.new(x => 7).zeige;
    ENDE { sag "ende" }
    ZZ

# NOT named `run`: a sub of that name shadows the builtin INSIDE ITSELF, so the
# first call recurses until the process is killed.
sub rakupp(*@args) {
    my $p = run($*EXECUTABLE.Str, "-I{$dir}", |@args, :out, :err);
    my $o = $p.out.slurp(:close);
    $p.err.slurp(:close);
    $o.trim
}
my $prog = $dir.add("p.raku");

# ---- the parse -----------------------------------------------------------
$prog.spurt: 'use experimental :rakuast; print slurp(@*ARGS[0]).AST("ZZ").DEPARSE';
$dir.add("src.zz").spurt($src);
my $deparsed = rakupp($prog.Str, $dir.add("src.zz").Str);
check $deparsed.lines[0], 'my @z = 1, 2, 3;', 'a quoted, hyphenated keyword (`il-mio`) is `my`';
check $deparsed.contains('for @z -> $e'), True, 'a keyword outside ASCII (`füralle`) is `for`';
check $deparsed.contains('say "x=" ~ $!x'), True, '`core2ast` translates the routine name';
check $deparsed.contains('if $s eq "6"'), True, 'a word infix and a block keyword';
check $deparsed.contains('END {'), True, 'a phaser keyword';
check $deparsed.contains('class Punkt'), True, 'a package declarator';
check $deparsed.contains('method zeige'), True, 'a routine declarator';
# …and the program's OWN names are left alone. This is the half that would
# silently pass if the table were applied to everything.
check $deparsed.contains('Punkt'), True, "a name the language does not know is untouched";

# ---- it runs, and it is the same program ---------------------------------
$prog.spurt: 'use experimental :rakuast; slurp(@*ARGS[0]).AST("ZZ").EVAL';
check rakupp($prog.Str, $dir.add("src.zz").Str), "summe=6 n=3\nx=7\nende", 'the translated program runs';

# ---- and the language argument is what does it ---------------------------
# Without it the same source is NOT that program — our `.AST` is Lexer+Parser
# only, so it does not throw the way Rakudo's undeclared-name check does; it
# simply keeps the German. That is the recorded divergence, and this asserts it
# rather than hiding it.
$prog.spurt: 'use experimental :rakuast; print slurp(@*ARGS[0]).AST.DEPARSE';
check rakupp($prog.Str, $dir.add("src.zz").Str).contains('il-mio'), True,
      'no language argument: the source is left in the fixture language';
# A MISSPELLED keyword is not translated either — the lookup is exact.
$dir.add("bad.zz").spurt($src.subst('wenn', 'wenxn'));
$prog.spurt: 'use experimental :rakuast; print slurp(@*ARGS[0]).AST("ZZ").DEPARSE';
check rakupp($prog.Str, $dir.add("bad.zz").Str).contains('wenxn'), True,
      'a misspelled keyword stays as it was written';

# ---- `.AST` is not behind the pragma -------------------------------------
# Measured on 2026.08: Rakudo gates the `RakuAST::` NAMES, not this method, and
# every L10N dist's own test opens with `.AST("DE")` and no `use experimental`.
$prog.spurt: 'print Q[say 1].AST.DEPARSE';
check rakupp($prog.Str), 'say 1', '`.AST` needs no pragma, as upstream';

for $dir.dir -> $e {
    if $e.d { .unlink for $e.dir; $e.rmdir }
    else    { $e.unlink }
}
$dir.rmdir;
if @fail { .say for @fail; say "FAIL"; exit 1 }
say "PASS";
