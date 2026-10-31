# Regression: `.DEPARSE("RU")` renders the tree in the language it is handed
# (RAKUAST-PLAN P1-L10N, the other direction of `.AST("RU")`).
#
# It ignored its arguments, so every language came back as English and one
# that is not installed was no error. Every L10N dist's suite asks for it —
# `$ast.DEPARSE("RU")` has to contain `мой`, `для` and `скажи` — and had it
# under a `todo` for this engine.
#
# Three parts:
#   * WHICH keyword is asked for, where, and under which category:
#     tools/rakuast-deparse-l10n-spec.raku, against the file it produces under
#     Rakudo 2026.09 (docs/dev/findings/rakuast/deparse-l10n-2026.09.tsv), as
#     t/regression/rakuast-deparse.raku does for English;
#   * a language BY NAME, from a fixture dist written here in the shape the
#     dists' generator writes (`my constant %xlation` and a role with `xsyn`),
#     so this stays offline: its rendering, and that the rendering is the same
#     program, read back with `.AST("ZZ")` from a matching parse-side role;
#   * the arguments that are not a language.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}

# ---- part 1: the spec against the oracle ----------------------------------
my $root = $?FILE.IO.parent.parent.parent;
my $p = run($*EXECUTABLE, $root.add('tools/rakuast-deparse-l10n-spec.raku').Str, :out, :err);
my %got;
for $p.out.slurp(:close).lines -> $l {
    my $t = $l.index("\t");
    %got{$l.substr(0, $t)} = $l.substr($t + 1) if $t.defined;
}
my $err = $p.err.slurp(:close);
@fail.push("the spec file did not run: exit {$p.exitcode}\n$err") if $p.exitcode != 0;
my @want = $root.add('docs/dev/findings/rakuast/deparse-l10n-2026.09.tsv').lines;
for @want -> $l {
    my $t = $l.index("\t");
    next unless $t.defined;
    my ($label, $expect) = $l.substr(0, $t), $l.substr($t + 1);
    unless %got{$label}:exists { @fail.push("$label: no row (the case did not run here)"); next }
    check %got{$label}, $expect, "spec row $label";
}
for %got.keys.sort -> $k {
    @fail.push("$k: ran here but absent from the recorded oracle — re-run the spec under Rakudo")
        unless @want.first({ .starts-with($k ~ "\t") }).defined;
}

# ---- part 2: a language by name -------------------------------------------
my $dir = $*TMPDIR.add("rakupp-deparse-l10n-{$*PID}");
$dir.add("RakuAST/Deparse/L10N").mkdir;
$dir.add("L10N").mkdir;
# A QUOTED, hyphenated spelling (`il-mio`) and one outside ASCII (`füralle`),
# as in t/regression/rakuast-l10n.raku. `wenn` and `falls` are both `if`, one
# before a block and one after a statement, so a keyword asked under the wrong
# category comes back in English.
$dir.add("RakuAST/Deparse/L10N/ZZ.rakumod").spurt: q:to/MOD/;
    my constant %xlation = "scope-my", "il-mio", "scope-has", "hat", "core-say", "sag",
        "core-return", "zurück", "block-for", "füralle", "block-if", "wenn",
        "block-else", "sonst", "modifier-if", "falls", "infix-eq", "gleich",
        "prefix-not", "nicht", "routine-sub", "unter", "routine-method", "methode",
        "package-class", "klasse", "phaser-END", "ENDE";
    role RakuAST::Deparse::L10N::ZZ {
        method xsyn (str $prefix, str $key) {
            %xlation{"$prefix-$key"} // $key
        }
    }
    MOD
$dir.add("L10N/ZZ.rakumod").spurt: q:to/MOD/;
    role L10N::ZZ {
        use experimental :rakuast;
        token scope-my { "il-mio" }
        token scope-has { hat }
        token block-for { füralle }
        token block-if { wenn }
        token block-else { sonst }
        token modifier-if { falls }
        token infix-eq { gleich }
        token prefix-not { nicht }
        token routine-sub { unter }
        token routine-method { methode }
        token package-class { klasse }
        token phaser-END { ENDE }
        method core2ast {
            my constant %mapping = "sag", "say", "zurück", "return";
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
    sub EXPORT(|) {
        $*LANG.define_slang('MAIN', $*LANG.slang_grammar('MAIN'));
        {}
    }
    MOD

$dir.add("src.raku").spurt: q:to/SRC/;
    my @z = 1, 2, 3;
    my $s = 0;
    for @z -> $e { $s = $s + $e }
    sub sechs() { return 6 }
    class Punkt { has $.x; method zeige() { say "x=" ~ $!x } }
    if $s eq sechs() { say "summe=" ~ $s } else { say "nein" }
    say "nie" if not 1;
    Punkt.new(x => 7).zeige;
    END { say "ende" }
    SRC

# NOT named `run`: a sub of that name shadows the builtin inside itself
sub rakupp($code) {
    my $prog = $dir.add("p.raku");
    $prog.spurt: "use experimental :rakuast;\n" ~ $code;
    my $p = run($*EXECUTABLE.Str, "-I{$dir}", $prog.Str, $dir.add("src.raku").Str, :out, :err);
    my $o = $p.out.slurp(:close);
    my $e = $p.err.slurp(:close);
    ($o, $e)
}

# Rakudo 2026.09's rendering of the same source with the same role, byte for
# byte.
my ($zz, $zzerr) = rakupp('print slurp(@*ARGS[0]).AST.DEPARSE("ZZ")');
check $zz, q:to/ZZ/, 'the fixture language, by name';
    il-mio @z = 1, 2, 3;
    il-mio $s = 0;
    füralle @z -> $e {
        $s = $s + $e
    }
    unter sechs {
        zurück 6
    }
    klasse Punkt {
        hat $.x;
        methode zeige {
            sag "x=" ~ $!x
        }
    }
    wenn $s gleich sechs() {
        sag "summe=" ~ $s
    }
    sonst {
        sag "nein"
    }
    sag "nie" falls nicht 1;
    Punkt.new(x => 7).zeige;
    ENDE {
        sag "ende"
    }
    ZZ
check $zzerr, '', '…and says nothing on stderr';

# The rendering is the SAME PROGRAM: read back as the language, it runs as the
# English does. A keyword in the wrong category would not read back.
my ($en) = rakupp('slurp(@*ARGS[0]).AST.EVAL');
my ($back) = rakupp('slurp(@*ARGS[0]).AST.DEPARSE("ZZ").AST("ZZ").EVAL');
check $en, "summe=6\nx=7\nende\n", 'the English source runs';
check $back, $en, 'its ZZ rendering, read back with .AST("ZZ"), runs the same';

# `"ZZ:ver<1>"` names the same role: Rakudo cuts at the colon too
my ($ver) = rakupp('print Q[my $x = 1].AST.DEPARSE("ZZ:ver<1>")');
check $ver, "il-mio \$x = 1\n", 'a language with a version names the same role';

# A language that is not installed is an error, as it is upstream, and not the
# English it used to render.
my ($qq, $qqerr) = rakupp('print Q[my $x = 1].AST.DEPARSE("QQ")');
check $qq, '', 'a language that is not installed renders nothing';
check $qqerr.contains('Could not find RakuAST::Deparse::L10N::QQ'), True, '…and says which module';

.unlink for flat $dir.add("RakuAST/Deparse/L10N").dir, $dir.add("L10N").dir, $dir.dir.grep(*.f);
$dir.add("RakuAST/Deparse/L10N").rmdir; $dir.add("RakuAST/Deparse").rmdir;
$dir.add("RakuAST").rmdir; $dir.add("L10N").rmdir; $dir.rmdir;

# ---- part 3: what is not a language ---------------------------------------
my $ast = Q[my $x = 1].AST;
check $ast.DEPARSE, "my \$x = 1\n", 'no argument is English';
role Mine { method xsyn($p, $k) { $k eq 'my' ?? 'MINE' !! $k } }
check $ast.DEPARSE(Mine), "MINE \$x = 1\n", 'a role of a user\'s own is asked as a dist\'s is';
role Nothing { method other { } }
check $ast.DEPARSE(Nothing), "my \$x = 1\n", 'a role with no xsyn changes no keyword, as upstream';
# Rakudo takes anything else as the deparser class, and Int has no `deparse`
my $int = (try $ast.DEPARSE(Int)) // $!;
check $int.^name, 'X::Method::NotFound', 'a class with no `deparse` fails as upstream';
# Highlighting is the one thing Rakudo does here that this engine does not:
# it says so rather than rendering without it.
role Highlight { method hsyn($p, $c) { "[$c]" } }
my $hl = (try $ast.DEPARSE(Highlight)) // $!;
check $hl.^name, 'X::NYI', 'a highlighting role is refused, not ignored';

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
