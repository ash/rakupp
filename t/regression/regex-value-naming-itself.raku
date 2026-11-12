# Regression (rakudo#3775): a regex value that names itself is a recursive
# pattern. `my $r; $r = / a <$r>? /` matches "aaa" in Rakudo, three calls deep.
# Raku++ expanded `<$r>` by pasting $r's source, whose own `<$r>` pasted it
# again, and the C++ stack ran out before one character was matched: SIGBUS,
# for every recursive spelling (`<$r>`, `<x=$r>`, a bare `$r`, two values
# naming each other). Now a value's reappearance inside its own expansion is
# called when the match reaches it, as Rakudo resolves `<$r>`.
#
# A rule that calls itself before consuming anything (`/ <$r> a /`,
# `token TOP { <TOP> a }`) never ends: Rakudo hangs, and Raku++ took SIGBUS.
# Raku++ now throws X::Recursion; that half is checked on Raku++ only.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub same(Str $what, $got, $want) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my $r; $r = / a <$r>? /;
same 'a <$r>? over "aaa"', ~("aaa" ~~ $r), 'aaa';
my $g; $g = / a [<$g>]? /;
same 'a [<$g>]? over "a"', ~("a" ~~ $g), 'a';
my $alt; $alt = / b | a <$alt> /;
same 'b | a <$alt> over "aab"', ~("aab" ~~ $alt), 'aab';
my $paren; $paren = / '(' <$paren>* ')' /;
same 'balanced parentheses', ~("(()(()))x" ~~ $paren), '(()(()))';
my $bare; $bare = / a $bare? /;
same 'a bare $bare', ~("aaa" ~~ $bare), 'aaa';
my ($x, $y); $x = / x <$y>? /; $y = / y <$x>? /;
same 'two values naming each other', ~("xyxyz" ~~ $x), 'xyxy';
sub mk { my $m; $m = / a <$m>? /; $m }
same 'made in a sub, matched outside it', ~("aaaa" ~~ mk()), 'aaaa';

my $al; $al = / a <x=$al>? /;
my $am = "aaa" ~~ $al;
same 'aliased: <x>', ~$am<x>, 'aa';
same 'aliased: <x><x>', ~$am<x><x>, 'a';
same 'aliased: nothing below', $am<x><x><x>.defined, False;

my @levels;
my $code; $code = / a { @levels.push(1) } <$code>? /;
"aa" ~~ $code;
same 'a code block at each level', @levels.elems, 2;

if $*RAKU.compiler.name eq 'Raku++' {
    my $left; $left = / <$left> a /;
    my $err = (try { "aaa" ~~ $left; Nil } // $!);
    same 'left recursion through <$var>', $err.^name, 'X::Recursion';
    grammar G { token TOP { <TOP> a } }
    $err = (try { G.parse("aaa"); Nil } // $!);
    same 'left recursion in a grammar', $err.^name, 'X::Recursion';
    my $after = "ok" ~~ / ok /;
    same 'matching still works afterwards', ~$after, 'ok';
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
