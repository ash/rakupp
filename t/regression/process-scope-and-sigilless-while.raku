# Regression: the three fixes that took DBIish from 1 to 28 of its own 37 test
# files, 2026-08-04. None is about databases; DBIish is just the first code here
# to need them. Every expectation was checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. `PROCESS::<$x> = …` installs into the PROCESS scope, whatever frame runs it.
#    Assigning through the bare name made a fresh lexical in the running sub, so
#    the value vanished on return and the dynamic was never visible to anyone.
{
    sub setter() { PROCESS::<$RX-FROM-SUB> = 5 }
    setter();
    ck($*RX-FROM-SUB, 5, 'PROCESS::<$x> set inside a sub is visible outside');

    my $blk = { PROCESS::<$RX-FROM-BLOCK> = 7 };
    $blk();
    ck($*RX-FROM-BLOCK, 7, 'and inside a block');

    ck($PROCESS::RX-FROM-SUB, 5, 'the $PROCESS::x spelling reads the same slot');

    # nested: the innermost frame still reaches the process scope
    sub outer() { sub inner() { PROCESS::<$RX-DEEP> = 9 }; inner() }
    outer();
    ck($*RX-DEEP, 9, 'two frames down');

    # a plain lexical dynamic is untouched by any of this
    my $*RX-LEXICAL = 'local';
    ck($*RX-LEXICAL, 'local', 'a `my $*x` still shadows normally');
}

# 2. A sigilless loop variable in `while`. Only the sigilled form was accepted,
#    so the `\` was left for the block parser and the whole module failed to
#    parse — DBIish's StatementHandle is written `while self.row -> \r {…}`.
{
    my @q = 1, 2, 3;
    sub nxt() { @q ?? @q.shift !! Nil }
    my @seen;
    while nxt() -> \r { @seen.push(r) }
    ck(@seen, [1, 2, 3], 'while EXPR -> \r');

    my @p = 5, 6;
    my @sigilled;
    while @p.shift -> $x { @sigilled.push($x); last unless @p }
    ck(@sigilled, [5, 6], 'the sigilled form still works');

    my @u = 1, 2;
    my @until-seen;
    until !@u -> \r { @until-seen.push(@u.shift) }
    ck(@until-seen, [1, 2], 'until takes it too');
}

# 3. `Rakudo::Internals.REGISTER-DYNAMIC` — the initializer a module supplies for
#    a process-wide dynamic it owns. Missing entirely, which stopped DBIish at
#    its first `use`. We run the block at registration rather than at first
#    lookup; an existing binding is left alone.
{
    Rakudo::Internals.REGISTER-DYNAMIC: '$*RX-REGISTERED', {
        PROCESS::<$RX-REGISTERED> = { answer => 42 }
    };
    ck($*RX-REGISTERED<answer>, 42, 'the registered initializer ran');

    PROCESS::<$RX-PRESET> = 'mine';
    Rakudo::Internals.REGISTER-DYNAMIC: '$*RX-PRESET', {
        PROCESS::<$RX-PRESET> = 'overwritten'
    };
    ck($*RX-PRESET, 'mine', 'a value already in place is not overwritten');
}

say $ok ?? 'PASS' !! 'FAIL';
