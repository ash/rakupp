# Regression: `no strict` escaped the scope that asked for it.
#
# The pragma is LEXICAL — it holds from the statement that writes it to the end
# of the enclosing block, and nowhere else. rakupp treated it as a switch for
# the whole run: the pre-run checker set its file-wide `standDown` flag and the
# interpreter a run-wide `noStrict_`, so ONE `no strict` anywhere — inside a
# block, inside a sub — silently unstricted every line after it. `{ no strict;
# $a = 5 }; $b = 7` compiled and ran, where Rakudo says "Variable '$b' is not
# declared".
#
# Fixed by making the flag ride the scope: DeclCheck saves and restores it in
# pushScope/popScope, and the runtime carries it on the Env that executed the
# pragma (Env::noStrict), which the check reads by walking the lexical chain —
# a walk that only ever happens on a name that already failed to resolve.
#
# Contract: exit 0 + last line PASS.
my @fail;

sub run-it(Str $code) {
    my $p = run($*EXECUTABLE, '-e', $code, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).chomp
}
sub allows(Str $code, Str $want, Str $what) {
    my $got = run-it($code);
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eq $want;
}
sub refuses(Str $code, Str $var, Str $what) {
    my $got = run-it($code);
    @fail.push("$what: got {$got.raku}, want a '$var is not declared' error")
        unless $got.contains("Variable '$var' is not declared");
}

# --- the pragma still works where it is asked for ---------------------------
allows 'no strict; $zz = 5; say $zz', '5', 'no strict at file scope';
allows '{ no strict; $zz = 5; say $zz }', '5', 'no strict inside a block';
allows 'no strict; sub f { $qq = 3; $qq }; say f()', '3',
       'an inner scope inherits the file-scope pragma';
allows '{ no strict; { $zz = 7; say $zz } }', '7', 'nested blocks inherit it';
allows 'use strict; my $x = 1; say $x', '1', 'use strict is still an accepted no-op';
allows 'no strict; { use strict; }; $gg = 3; say $gg', '3',
       'a nested use strict lapses with its own block';

# --- and stops at the closing brace, which is the bug -----------------------
refuses '{ no strict; $zz = 5 }; $ww = 7; say $ww', '$ww',
        'it does not leak past the block that asked for it';
refuses '{ no strict; $zz = 5 }; say $zz', '$zz',
        'the auto-vivified name is not visible outside that block';
refuses '{ no strict; $zz = 5 }; { $ww = 8; say $ww }', '$ww',
        'a sibling block is still strict';
refuses 'sub f { no strict; $zz = 5; $zz }; say f(); $yy = 9; say $yy', '$yy',
        'it does not leak out of a sub body';
refuses '$zz = 5; no strict; say $zz', '$zz',
        'it does not reach back before the statement that writes it';
refuses 'no strict; { use strict; $ff = 2; say $ff }', '$ff',
        'use strict turns the check back on inside a lax file';

# --- the runtime backstop scopes it too -------------------------------------
# Inside EVAL the pre-run checker never sees the code, so the interpreter's own
# undeclared-variable check is what answers — and it must scope the pragma the
# same way. `leaked` printing here is the run-wide flag coming back.
{
    my $got = run-it(q[use MONKEY-SEE-NO-EVAL;
        EVAL q<{ no strict; $zz = 5; say $zz }; try { $ww = 7; say "leaked" }; say "done">]);
    @fail.push("runtime check: got {$got.raku}, want \"5\\ndone\"") unless $got eq "5\ndone";
    # …and both directions of it: lax outside, strict back on inside.
    my $both = run-it(q[use MONKEY-SEE-NO-EVAL;
        EVAL q<no strict; { use strict; try { $ff = 1; say "leaked" }; say "inner" }; $gg = 2; say "outer $gg">]);
    @fail.push("runtime use strict: got {$both.raku}, want \"inner\\nouter 2\"")
        unless $both eq "inner\nouter 2";
}

if @fail {
    note "FAILED:\n" ~ @fail.map({ "  - $_" }).join("\n");
    exit 1;
}
say "PASS";
