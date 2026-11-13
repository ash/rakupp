# `is cached` is an experiment, so it needs `use experimental :cached` in
# scope, as in Rakudo: without it, X::Experimental. The pragma is lexical.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}
sub refused($code) { try EVAL $code; $! ?? $!.^name.subst(/'+{' .* $/, '') !! 'ok' }

check refused('sub f($x) is cached { $x }; 1'), 'X::Experimental', 'without the pragma';
check refused('class A { method m is cached { 1 } }; 1'), 'X::Experimental', 'a method alike';
check refused('{ use experimental :cached; }; sub g is cached { 1 }; 1'), 'X::Experimental', 'the pragma is lexical';
check refused('use experimental :cached; sub f($x) is cached { $x }; 1'), 'ok', 'with the pragma';
check refused('use experimental :cached; { sub h is cached { 1 } }; 1'), 'ok', '…in an inner scope too';
try EVAL 'sub f is cached { 1 }';
check $!.message, "Use of the 'is cached' trait is experimental; please 'use experimental :cached;'", 'the message';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
