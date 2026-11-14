# An anonymous class that opens a statement is a term like any other, as in
# Rakudo (rakudo#1360, rakudo#2078): a statement modifier after it, and an
# infix after its postfix chain, were refused — `class {…} for ^5` died
# "Missing ';' before 'for'", and `class {…}.new.z = 42` "unexpected operator
# in term position".

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my @log;
class { method m { @log.push('m') } }.new.m for ^2;
check @log.join(','), 'm,m', 'a postfix chain, then a modifier (rakudo#1360)';

@log = ();
class { method sink { @log.push('sunk') } } for ^2;
check @log.elems, '0', 'the type object as the modifier\'s term: nothing to sink';

my $o;
class { has $.z is rw; method keep { $o = self } }.new.keep.z = 42;
check $o.z, '42', 'an assignment after the postfix chain (rakudo#2078)';

@log = ();
class { method m { @log.push('if') } }.new.m if True;
class { method m { @log.push('unless') } }.new.m unless True;
check @log.join(','), 'if', 'if and unless';

@log = ();
role { } if True;
check 'parsed', 'parsed', 'an anonymous role before a modifier';

class Named { method m { @log.push('named') } }.new.m for ^2;
check @log.join(','), 'named,named', 'a named class with a postfix chain and a modifier';
check Named.^name, 'Named', '…is declared once';

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
