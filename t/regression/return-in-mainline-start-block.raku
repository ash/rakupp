# A `return` in a `start` block made outside every routine has nothing to
# leave, as in Rakudo (rakudo#2753): the Promise breaks with
# X::ControlFlow::Return. It was kept with the returned value.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

try await start { return 5 };
check $!.^name.subst(/'+{' .* $/, ''), 'X::ControlFlow::Return', 'await start { return } (rakudo#2753)';
check $!.message, 'Attempt to return outside of any Routine', 'its message';
try await start { if 1 { return 9 } };
check $!.^name.subst(/'+{' .* $/, ''), 'X::ControlFlow::Return', '…not in tail position either';
check (await start { 42 }), 42, 'a start block\'s value, as before';
check (await start { sub g { return 7 }; g() }), 7, 'a routine inside it returns, as before';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
