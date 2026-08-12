# Regression: an unknown bare name evaluated to a MANUFACTURED stub type
# object — `say dfdf` printed "(dfdf)" (and the REPL's `\t dfdf` answered
# "(dfdf)") where Rakudo refuses at compile time with "Undeclared routine".
#
# The stub exists for FORWARD REFERENCES — a name the unit declares further
# down (rakupp executes top-to-bottom; Rakudo sees the whole unit at compile
# time). So the parser now records every type-ish name the unit declares
# (classes/grammars/roles/modules, subsets, enums + statically visible
# members), and the lenient fallback holds only for those; everything else is
# X::Undeclared::Symbols. A unit whose declarations cannot be enumerated (a
# cached/embedded Program, a computed `class ::(EXPR)` name) stays lenient.
#
# Checks run through EVAL so Rakudo's compile-time refusal lands in the same
# try as rakupp's run-time one. Every check verified against Rakudo.
#
# Contract: exit 0 + last line PASS.
use MONKEY-SEE-NO-EVAL;
my @fail;

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

sub ex-of(Str $code) {
    try EVAL $code;
    $! ?? $!.^name !! 'none'
}

# unknown names refuse — lowercase and capitalized, bare and qualified
check ex-of('dfdf'), 'X::Undeclared::Symbols', 'unknown lowercase bareword refuses';
check ex-of('say Dfdf'), 'X::Undeclared::Symbols', 'unknown capitalized name refuses';
check so(ex-of('No::Such::Pkg9') ne 'none'), True,
    'unknown qualified name refuses (Rakudo types it X::AdHoc, rakupp X::Undeclared::Symbols)';

# the LEGAL forward shapes keep working — the reason the leniency exists.
# (A bare term used before its declaration is illegal under Rakudo too —
# "Illegally post-declared type" — so that is NOT tested as working.)
check EVAL('class Stub9 {...}; my $n = Stub9.^name; class Stub9 { method hi() { "hi" } }; $n ~ Stub9.hi'),
    'Stub9hi', 'a stub-predeclared class is usable before its real body';
# (a method body naming a LATER class is illegal under Rakudo as well —
# same "Illegally post-declared type" — so rakupp's in-unit leniency is a
# strict superset of Rakudo legality: it can only accept more, never refuse
# valid Rakudo code)
check EVAL('class Early9 { method tag() { "t9" } }; class UsesEarly9 { method mk() { Early9.new.tag } }; UsesEarly9.mk'),
    't9', 'declaration-first cross-class calls still resolve';

# declared things still resolve normally
check EVAL('class Now9 {}; Now9.^name'), 'Now9', 'a declared class resolves';
check EVAL('subset Small9 of Int where * < 5; Small9.^name'), 'Small9',
    'a subset name resolves';
check EVAL('enum Hue9 <R9 G9>; R9.^name'), 'Hue9', 'an enum member resolves';

# ::('…') keeps its soft Failure (a different door, deliberately)
check EVAL('(try ::("NoSuchCls9")).defined ?? "defined" !! "undefined"'), 'undefined',
    '::(unknown) still fails softly, not a stub';

# ---------------------------------------------------------------------------
if @fail {
    .say for @fail;
    say "FAIL ({@fail.elems})";
    exit 1;
}
say "PASS";
