# Test's and NativeCall's routines are undeclared without a `use` of their
# module, as in Rakudo (rakudo#1631, #3607, #4967, #1612, #2191): `ok 1` and
# `is-deeply …` ran with no `use Test`. EVAL'd code sees its caller's `use`.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}
sub refused($code) { try EVAL $code; $! ?? $!.^name.subst(/'+{' .* $/, '') !! 'ok' }

check refused('is-deeply 1, 1, "x"'), 'X::Undeclared::Symbols', 'is-deeply (rakudo#4967)';
check refused('ok 1'), 'X::Undeclared::Symbols', 'ok (rakudo#1631)';
check refused('lives-ok { 1 }'), 'X::Undeclared::Symbols', 'lives-ok (rakudo#3607)';
check refused('done-testing'), 'X::Undeclared::Symbols', 'done-testing';
check refused('cglobal("c", "x", Int)'), 'X::Undeclared::Symbols', 'cglobal (rakudo#2191)';
check refused('sub ok($x) { $x }; ok 1'), 'ok', 'a routine of the same name the code declares';
try EVAL 'ok 1';
check $!.message.lines[1].trim, 'ok used at line 1', 'the message';
{
    use Test;
    my $out = EVAL q[my $o = ''; $o];   # (EVAL sees this scope's `use Test`)
    check (try EVAL q[&ok.name]) // 'none', 'ok', 'EVAL sees its caller\'s use Test';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
