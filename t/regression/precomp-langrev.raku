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
# best effort — Rakudo leaves a .precomp TREE behind, which .unlink cannot take
LEAVE { try { my sub rm($d) { for $d.dir { $_.d ?? rm($_) !! .unlink }; $d.rmdir }; rm($dir) } }

# `use v6.*` means "the newest revision this compiler implements", which is why
# lizmat writes it with that comment — and the module's ROUTINES run under it,
# which is what the `.AST` check below proves and what this file is really about.
#
# `$*RAKU.version` is NOT a probe for that. It is one object for the process and
# reports the MAIN unit's revision: a module written `use v6.*` and loaded from a
# 6.d program answers 6.d, on Rakudo 2026.08 and now here. This check asserted
# 6.e and so did not pass under the oracle at all — corrected 2026-09-16 when
# App::ModuleSnap turned up defaulting a parameter to `$*RAKU.version` inside
# exactly such a module and storing a revision its own suite then rejected.
$dir.add('SixE.rakumod').spurt(q:to/MOD/);
    use v6.*;
    unit module SixE;
    our sub rev() is export { ~$*RAKU.version }
    MOD

# Three runs: cold cache, then warm, then warm again. All three must agree.
for 1..3 -> $run {
    my $p = run($*EXECUTABLE, '-e', "use lib '$dir'; use SixE; print rev()", :out, :err);
    my $got = $p.out.slurp(:close); $p.err.slurp(:close);
    @fail.push("run $run: \$*RAKU.version inside the module is {$got.raku}, want \"6.d\" — the MAIN unit's")
        unless $got eq '6.d';
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
