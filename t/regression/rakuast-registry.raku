# Regression: the RakuAST:: namespace exists, behind the pragma (RAKUAST-PLAN P0).
#
# Rakudo's RakuAST is Raku's own syntax tree reachable from Raku. A module that
# uses it does not degrade here, it hard-fails, so the names have to exist and
# have to behave: gated under 6.d, free under 6.e, carrying Rakudo's own
# ancestry so `~~ RakuAST::Expression` and `.^mro` answer.
#
# Every expectation below was MEASURED against Rakudo 2026.08 (the pinned
# oracle) — the refusal text, the visibility rule, the two linearizations. When
# the oracle moves, re-measure rather than adjusting to taste.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what:\n     got  {$got.raku}\n     want {$want.raku}") unless $got eq $want
}
sub like-check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want /$want/") unless $got.contains($want)
}

sub out(Str $code) {
    my $p = run($*EXECUTABLE, '-e', $code, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).chomp
}
# …and the same program through `--exe`: the native backend must refuse and
# resolve identically, or a compiled binary and the interpreter disagree about
# which names a program has.
sub exe-out(Str $code) {
    my $src = $*TMPDIR.add("rakuast-p0-{$*PID}.raku");
    my $bin = $*TMPDIR.add("rakuast-p0-{$*PID}.bin");
    $src.spurt($code);
    my $c = run($*EXECUTABLE, '--exe', $src.Str, '-o', $bin.Str, :out, :err);
    my $cerr = $c.out.slurp(:close) ~ $c.err.slurp(:close);
    LEAVE { .unlink for $src, $bin }
    return "COMPILE FAILED: $cerr" unless $c.exitcode == 0 && $bin.e;
    my $p = run($bin.Str, :out, :err);
    ($p.out.slurp(:close) ~ $p.err.slurp(:close)).chomp
}

# ---- the gate ----------------------------------------------------------
my $REFUSAL = "Use of RakuAST is experimental; please 'use experimental :rakuast;'";

like-check out('say RakuAST::IntLiteral.^name'), $REFUSAL,
    'a bare 6.d program is refused, with Rakudo verbatim';
check out('use experimental :rakuast; say RakuAST::IntLiteral.^name'),
    'RakuAST::IntLiteral', 'the pragma opens the namespace (spaced spelling)';
check out('use experimental:rakuast; say RakuAST::IntLiteral.^name'),
    'RakuAST::IntLiteral', 'the pragma opens the namespace (tight spelling)';
check out('use v6.e.PREVIEW; say RakuAST::IntLiteral.^name'),
    'RakuAST::IntLiteral', '6.e sees the namespace without the pragma';

# The symbolic form answers a FAILURE carrying the same exception, never a
# throw: `try ::('RakuAST::Node')` is how a module asks whether the namespace
# is there at all, and a control jump would break the probe it is written as.
check out('my $x = ::("RakuAST::Node"); say $x.^name'), 'Failure',
    '::("RakuAST::…") under 6.d answers a Failure, not a throw';
like-check out('my $x = ::("RakuAST::Node"); try { $x.gist }; say $!.^name'),
    'X::Experimental', 'and that Failure carries X::Experimental';
check out('use experimental :rakuast; my $x = ::("RakuAST::Node"); say $x.^name'),
    'RakuAST::Node', '…and resolves once the pragma is on';

# A user's own class in the namespace is THEIRS: it lives in the ordinary class
# table, is found before the registry is ever consulted, and is never gated.
# (Rakudo refuses even this under 6.d; we deliberately do not — the name was
# legal here before RakuAST existed and stays legal.)
check out('class RakuAST::Mine { has $.x }; say RakuAST::Mine.new(:x(7)).x'), '7',
    'a user class in the namespace still works, ungated';
check out('use experimental :rakuast; class RakuAST::Node { method hi { "mine" } }; say RakuAST::Node.hi'),
    'mine', '…and shadows a registry class of the same name';
# …and when the declaration comes AFTER the use. The gate deliberately runs
# last, once the pending-type build has had its chance, so this resolves under
# bare 6.d; the build reads the unit's statements, which is why it answers the
# same from a cold parse and from the AST cache.
check out('say RakuAST::Mine.greet; class RakuAST::Mine { method greet { "hi" } }'), 'hi',
    'a forward reference to a user class in the namespace still resolves';

# Nothing about touching the namespace leaks bare names into resolution — the
# whole reason the registry is not `classes_`.
like-check out('use experimental :rakuast; RakuAST::IntLiteral; say Node'),
    "Undeclared name 'Node'", 'bare Node stays unresolved';
like-check out('use experimental :rakuast; RakuAST::Statement::Expression; say Statement'),
    "Undeclared name 'Statement'", 'bare Statement stays unresolved';
like-check out('use experimental :rakuast; RakuAST::Call::Name; say Call'),
    "Undeclared name 'Call'", 'bare Call stays unresolved';

# Every OTHER experimental tag stays the silent no-op it has always been — the
# parser capture this needed must not have woken anything else up.
check out('use experimental :cached; say "ok"'), 'ok', ':cached is still a silent no-op';
check out('use experimental:pack; say "ok"'),    'ok', ':pack is still a silent no-op (tight)';
check out('use v6.e.PREVIEW; use experimental :macros; say "ok"'), 'ok',
    ':macros is still a silent no-op under 6.e';

# ---- the hierarchy -----------------------------------------------------
# Measured on Rakudo 2026.08. `.^mro` is the class, its `.^parents`, then Any
# and Mu; the chains are C3 over roles, so this list is REPRODUCED from a
# generated table, not derived.
check out('use experimental :rakuast; say RakuAST::IntLiteral.^mro.map(*.^name).join(" ")'),
    'RakuAST::IntLiteral RakuAST::Literal RakuAST::Term RakuAST::Termish RakuAST::Expression '
    ~ 'RakuAST::MayCreateBlock RakuAST::Sinkable RakuAST::CheckTime RakuAST::CaptureSource '
    ~ 'RakuAST::CompileTimeValue RakuAST::Node Any Mu',
    'IntLiteral.^mro matches the 2026.08 list';
check out('use experimental :rakuast; say RakuAST::IntLiteral.^parents.map(*.^name).join(" ")'),
    'RakuAST::Literal RakuAST::Term RakuAST::Termish RakuAST::Expression RakuAST::MayCreateBlock '
    ~ 'RakuAST::Sinkable RakuAST::CheckTime RakuAST::CaptureSource RakuAST::CompileTimeValue '
    ~ 'RakuAST::Node',
    'IntLiteral.^parents matches the 2026.08 list';
# …including the one whose ancestry is NOT its parent's tail plus a head, which
# is the case a derived chain would get wrong.
check out('use experimental :rakuast; say RakuAST::ApplyInfix.^mro.map(*.^name).join(" ")'),
    'RakuAST::ApplyInfix RakuAST::Expression RakuAST::MayCreateBlock RakuAST::Sinkable '
    ~ 'RakuAST::CheckTime RakuAST::BeginTime RakuAST::SinkPropagator RakuAST::Node '
    ~ 'RakuAST::WhateverApplicable Any Mu',
    'ApplyInfix.^mro keeps Rakudo\'s own C3 order (Node is not last)';
check out('use experimental :rakuast; say "[" ~ RakuAST::Node.^parents.map(*.^name).join("|") ~ "]"'),
    '[]', 'the root has no parents, as Rakudo says';

# The checks a walker actually writes.
check out('use experimental :rakuast; my $n = RakuAST::IntLiteral.new; '
          ~ 'say ($n ~~ RakuAST::Node, $n ~~ RakuAST::Expression, $n ~~ RakuAST::Infix).join(" ")'),
    'True True False', 'an instance type-checks against its whole chain and nothing else';
check out('use experimental :rakuast; '
          ~ 'say (RakuAST::IntLiteral ~~ RakuAST::Node, RakuAST::IntLiteral ~~ RakuAST::Infix).join(" ")'),
    'True False', 'and so does the type object';
check out('use experimental :rakuast; my $n = RakuAST::IntLiteral.new; '
          ~ 'say ($n.does(RakuAST::Node), $n.does(RakuAST::Expression), $n.does(RakuAST::Infix), '
          ~ '$n.isa(RakuAST::Node), $n.isa(RakuAST::Infix)).join(" ")'),
    'True True False True False',
    '.does and .isa read the same ancestry (Test\'s isa-ok is both)';
# `.^roles` stays EMPTY, as it is on Rakudo: the ancestors are punned classes
# there, and reproducing them as roles here would be a different tree.
check out('use experimental :rakuast; say RakuAST::IntLiteral.^roles.elems'), '0',
    '.^roles is empty, as Rakudo answers';
# The gist is Rakudo's, short name and all — `say $node` prints `(IntLiteral)`
# on both engines, not the qualified name.
check out('use experimental :rakuast; say RakuAST::IntLiteral'), '(IntLiteral)',
    'a node type gists as Rakudo gists it';
check out('use experimental :rakuast; sub f(RakuAST::Node $n) { $n.^name }; '
          ~ 'say f(RakuAST::StrLiteral.new)'),
    'RakuAST::StrLiteral', 'a `RakuAST::Node $x` parameter binds a node';

# ---- per compilation unit ----------------------------------------------
# The pragma is the unit's, like the language revision beside it: a module that
# wants the names asks for them, and must not hand them to its importer.
{
    my $dir = $*TMPDIR.add("rakuast-p0-mod-{$*PID}");
    $dir.mkdir;
    LEAVE { .unlink for $dir.dir; $dir.rmdir }
    # q:to, not "…": the module body has a BLOCK in it, and an interpolating
    # string would run `RakuAST::IntLiteral.^name` right here instead of
    # writing it to the file — under a unit that never asked for the pragma.
    $dir.add('AstUser.rakumod').spurt(q:to/MOD/);
        use experimental :rakuast;
        unit module AstUser;
        our sub kind() is export { RakuAST::IntLiteral.^name }
        MOD
    check out("use lib '$dir'; use AstUser; say kind()"),
        'RakuAST::IntLiteral', 'a module may open the namespace for itself';
    # …and again, now that the first run has warmed the precomp cache. The AST
    # blob carries a unit's statements and none of its scalar fields, so the
    # parse-time pragma flag has to be RECOVERED on the way out — without that
    # the module hoisted its own subs as though the pragma were absent and
    # refused its own names from the second run on, while run 1 stayed green.
    check out("use lib '$dir'; use AstUser; say kind()"),
        'RakuAST::IntLiteral', '…and still does when it comes from the precomp cache';
    like-check out("use lib '$dir'; use AstUser; say kind(); say RakuAST::IntLiteral.^name"),
        $REFUSAL, '…without opening it for its importer';
    # …including when the module is required from a worker rather than the
    # mainline: the registry is built there and published with one atomic
    # compare-exchange, so the thread that gets there first is as safe as the
    # main one.
    # (Concatenated, not interpolated: `{ … }` inside a double-quoted string is
    # a code block, and this one would `require` in THIS process instead of in
    # the child — the same trap the module body above sidesteps with q:to.)
    check out("use lib '" ~ $dir ~ "'; "
              ~ 'my $p = start { require AstUser; AstUser::kind() }; say await $p'),
        'RakuAST::IntLiteral', 'a worker thread may be the one that requires it';
}
# And the lazy path under contention: 6.e opens the namespace without a pragma,
# so nothing materializes the registry up front and eight threads race to be the
# one that builds it. The loser of the compare-exchange frees its copy and reads
# the winner's — every thread must see the same class.
check out('use v6.e.PREVIEW; my @p = (^8).map({ start { RakuAST::IntLiteral.^mro.elems } }); '
          ~ 'say await(@p).unique.join(",")'),
    '13', 'eight threads racing to materialize the registry agree';

# ---- --exe parity ------------------------------------------------------
check exe-out('use experimental :rakuast; say RakuAST::IntLiteral.^mro.elems'), '13',
    '--exe resolves the namespace identically';
# The REFUSAL is where the two backends part, and not because of RakuAST: the
# native codegen runs no undeclared-name check at all, so `say Nonexistent`
# prints `(Nonexistent)` there too. The pair below pins both halves, so the day
# the native backend grows that check this case says so instead of going quietly
# green on a changed answer.
check exe-out('say Nonexistent.^name'), 'Nonexistent',
    '--exe mints an undeclared name (pre-existing: no declaration check there)';
check exe-out('say RakuAST::IntLiteral.^name'), 'RakuAST::IntLiteral',
    '…so an un-pragma\'d RakuAST name is minted too, not refused';
# What a module's feature probe actually asks, though, IS identical: `::(…)`
# is not something codegen compiles, so the unit falls back to the interpreter
# and answers the same Failure. That is the shape the release-train rule cares
# about — a probe must never decide the namespace is there when it is not.
check exe-out('my $x = ::("RakuAST::Node"); say $x.^name'), 'Failure',
    '--exe answers the symbolic probe identically';

if @fail {
    note "FAIL: $_" for @fail;
    say "FAIL: $_" for @fail;
    say "FAIL ({+@fail})";
    exit 1;
}
say "PASS";
