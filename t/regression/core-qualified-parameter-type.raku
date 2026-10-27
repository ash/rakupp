# Regression: a `CORE::Int $x` parameter took any value, and named itself
# `CORE::Int`.
#
# The binder could not resolve the qualified name and bound freely, the
# compile-time check skipped the call, and `.signature` and `.type` said
# CORE::Int where Rakudo says Int. The parser now writes the setting's Int,
# with a mark (Param.typeIsSetting) that no scope may re-read the short name:
# in DateTime::Timezones `DateTime` is the module's own class, and its
# `CORE::DateTime $self` candidates must still take a core DateTime.

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

sub f(CORE::Int $x) { $x }
ok f(3) == 3, 'an Int binds';
my $v = 'a';
ok (try f($v)) === Nil && $!.^name eq 'X::TypeCheck::Binding::Parameter', 'a Str does not';
ok $!.message eq q[Type check failed in binding to parameter '$x'; expected Int but got Str ("a")],
   "…and the message names Int (got '{$!.message}')";
ok &f.signature.raku eq ':(Int $x)' && &f.signature.params[0].type.^name eq 'Int', '.signature and .type';
my $err = try { EVAL q[sub g(CORE::Int $y) { $y }; g("x")] };
ok $!.message.contains('will never work') && $!.message.contains('(Int $y)'), 'the compile-time check';

# a module that declares its own DateTime keeps CORE::DateTime the core one
my $dir = $*TMPDIR.add("rakupp-core-param-{$*PID}");
$dir.add('lib').mkdir;
LEAVE { run 'rm', '-rf', $dir.Str }
$dir.add('lib/Own.rakumod').spurt: q:to/MOD/;
    unit module Own;
    class DateTime is DateTime is export { }
    multi sub kind(CORE::DateTime $d is rw) is export { 'rw ' ~ $d.^name }
    multi sub kind(CORE::DateTime $d) { 'ro ' ~ $d.^name }
    MOD
my $p = run $*EXECUTABLE, '-I', $dir.add('lib').Str, '-e',
            'use Own; my $c = CORE::DateTime.new(0); print kind($c), "|", kind(CORE::DateTime.new(0)), "|", kind(DateTime.new(0))',
            :out, :err;
my $out = $p.out.slurp(:close); $p.err.slurp(:close);
ok $out eq 'rw DateTime|ro DateTime|ro Own::DateTime', "CORE::DateTime candidates beside a DateTime of the module's own (got '$out')";

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
