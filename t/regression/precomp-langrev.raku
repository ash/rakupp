# Regression: a cached module keeps its OWN language revision.
#
# The AST cache blob carries a unit's statements and none of its scalar fields,
# so `Program::langRev` was lost on the way out and the module was hoisted under
# the IMPORTER's revision. A module whose header says `use v6.e.PREVIEW` — or
# `use v6.*`, which is the same thing — therefore ran its own routines with 6.d
# semantics from the SECOND run onward, and the first run was fine. Silent, and
# only after the cache warmed.
#
# Found in Needle::Compile 0.0.12, whose header is
#
#     use v6.*;  # Until 6.e is default
#
# and whose `.AST` call was refused as experimental on run 2 and never on run 1.
# Recovered in `deserializeAst` from the statements, where the version pragma is
# an ordinary `use` — the same recovery `usesRakuAst` needed, one field over.
#
# Contract: exit 0 + last line PASS.
my @fail;
my $dir = $*TMPDIR.add("precomp-langrev-{$*PID}");
$dir.mkdir;
LEAVE { .unlink for $dir.dir; $dir.rmdir }

# `use v6.*` means "the newest revision this compiler implements" — 6.e here and
# on Rakudo, which is why lizmat writes it with that comment.
$dir.add('SixE.rakumod').spurt(q:to/MOD/);
    use v6.*;
    unit module SixE;
    our sub rev() is export { ~$*RAKU.version }
    MOD

# Three runs: cold cache, then warm, then warm again. All three must agree.
for 1..3 -> $run {
    my $p = run($*EXECUTABLE, '-e', "use lib '$dir'; use SixE; print rev()", :out, :err);
    my $got = $p.out.slurp(:close); $p.err.slurp(:close);
    @fail.push("run $run: the module's own revision is {$got.raku}, want \"6.e\"")
        unless $got eq '6.e';
}

# …and the thing that made it visible: a 6.e-only surface reached from inside
# such a module, which the gate refuses under 6.d.
$dir.add('SixEAst.rakumod').spurt(q:to/MOD/);
    use v6.*;
    unit module SixEAst;
    our sub kind() is export { q[say 1].AST.^name }
    MOD
for 1..3 -> $run {
    my $p = run($*EXECUTABLE, '-e', "use lib '$dir'; use SixEAst; print kind()", :out, :err);
    my $got = ($p.out.slurp(:close) ~ $p.err.slurp(:close)).lines.head // '';
    @fail.push("run $run: `.AST` inside a 6.e module answered {$got.raku}")
        unless $got eq 'RakuAST::StatementList';
}

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
