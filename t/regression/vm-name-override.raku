# Regression: RAKUPP_VM_NAME, and the honest default it overrides.
#
# `$*VM.name` answers `cpp` — the backend, as Rakudo's answers `moar`. Some
# ecosystem modules branch on that name and die on the `else` (LibraryMake:
# "Unknown VM; don't know how to build"; uniprop: "Unexpected backend name"),
# so the variable lets the CALLER assert the moar dialect for one run. Two
# things have to hold for that to be safe:
#
#   * unset, nothing changes — the default is what this engine actually is,
#     and nothing in the toolchain sets the variable on a user's behalf;
#   * set, the name joins `$*RAKU.VMnames`, because the one hard check Roast
#     makes of it (S02-magicals/VM.t) is `$*VM.name eq any($*RAKU.VMnames)`.
#
# The subprocesses matter: the variable is read where `$*VM` is built, so it
# cannot be tested by assigning to %*ENV inside an already-running program.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

my $rakupp = $*RAKU.compiler.name eq 'Raku++';

sub vm(*%env) {
    my %e = %*ENV.clone;
    %e{.key} = .value for %env;
    %e<RAKUPP_VM_NAME>:delete unless %env<RAKUPP_VM_NAME>:exists;
    my $p = run($*EXECUTABLE, '-e',
                'say $*VM.name; say $*RAKU.VMnames.join(" "); say ($*VM.name eq any($*RAKU.VMnames)) ?? "member" !! "stranger"; say $*RAKU.compiler.backend',
                :out, :err, :env(%e));
    my @l = $p.out.slurp(:close).lines;
    $p.err.slurp(:close);
    @l
}

my @plain = vm();
check @plain[0], ($rakupp ?? 'cpp' !! 'moar'), 'the default name is the engine itself';
check @plain[2], 'member',                     'and it is one of VMnames';
check @plain[3], @plain[0],                    'compiler.backend agrees with $*VM.name';

if $rakupp {
    my @over = vm(RAKUPP_VM_NAME => 'moar');
    check @over[0], 'moar',       'the override is what $*VM.name answers';
    check @over[2], 'member',     'and the override joined VMnames';
    check @over[1], 'cpp js moar','appended, not substituted — the real backends stay';

    # An arbitrary string, to prove this is the caller's assertion and not a
    # two-valued moar/cpp switch with a hardcoded other side.
    my @other = vm(RAKUPP_VM_NAME => 'jvm');
    check @other[0], 'jvm',       'any name, not just moar';
    check @other[2], 'member',    'and that one is a member too';

    # Empty is not a name: an exported-but-empty variable must not blank $*VM.
    check vm(RAKUPP_VM_NAME => '')[0], 'cpp', 'an empty value is the default';

    # A backend we really do have stays single in the list.
    check vm(RAKUPP_VM_NAME => 'js')[1], 'cpp js', 'a real backend is not duplicated';

    # Rakudo answers the same string in both spellings; one override has to
    # move both, or a module reading the other one sees the contradiction.
    check @over[3], 'moar', 'compiler.backend follows the override too';
}

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
