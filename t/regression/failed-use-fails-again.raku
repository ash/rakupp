# A `use` that failed fails again: the module's name was entered as loaded
# before the search for it, and nothing took it out when the load threw, so
# `try EVAL 'use No::Such'; EVAL 'use No::Such; say "alive"'` printed alive —
# the second `use` found the name and returned. Found by the review of src/
# (docs/dev/findings/src-review-2026-11-09.md, C2). Rakudo 2026.09 dies both
# times, with X::CompUnit::UnsatisfiedDependency.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

sub failure(Str $code) {
    EVAL $code;
    return 'no failure';
    CATCH { default { return .^name.subst(/'+' .*/, '') } }
}

ck failure('use No::Such::Module::Anywhere; 1'), 'X::CompUnit::UnsatisfiedDependency', 'the first use';
ck failure('use No::Such::Module::Anywhere; 1'), 'X::CompUnit::UnsatisfiedDependency', 'the second';
ck failure('need No::Such::Module::Anywhere; 1'), 'X::CompUnit::UnsatisfiedDependency', 'a need after them';
ck failure('use Test; 1'), 'no failure', 'a module that is there still loads';
ck failure('use Test; 1'), 'no failure', '…twice';

say 'PASS';
