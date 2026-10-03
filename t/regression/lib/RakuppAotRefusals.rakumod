# t/regression/exe-module-routine-refusals.raku
unit module RakuppAotRefusals;
use RakuppPkgVar;

sub routine-name() is export { &?ROUTINE.name }
sub routine-fact($n) is export { $n <= 1 ?? 1 !! $n * &?ROUTINE($n - 1) }
sub frame-line() is export { callframe(0).line - $?LINE }
sub other-package() is export { $RakuppPkgVar::greeting ~ '!' }
sub loop-codes() is export { my @r; for &routine-name, &other-package -> &f { @r.push: f() }; @r.join(',') }
sub ro-assign($p) is export { $p = 5; $p }
sub ro-incr($p) is export { my $q = $p; $p++; $q }
sub ro-copy($p is copy) is export { $p = 5; $p }
sub typed() is export { my Int @b = 1, 2; (try { @b.push('s'); 'pushed' }) // @b.^name }
sub captured-class($v) is export {
    my $n = $v * 2;
    my class K { method m { "n=$n" } }
    K.new.m
}
sub computed-enum() is export { enum Size (S => 1 + 1, M => 5); S.Int ~ ' ' ~ M.Int }
