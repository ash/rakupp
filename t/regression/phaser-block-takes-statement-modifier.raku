# A block phaser takes a statement modifier on its line, as Rakudo parses it
# (rakudo#2243): `LEAVE { … } if $cond;` was refused, "Missing ';' before 'if'".
# The phaser is a declaration and stays whatever the condition says; the
# modifier governs the statement's value, Nil, and its condition still runs.
# Rakudo 2026.09 crashes on some of these at run time ("setcodeobj needs a code
# ref"); those are checked on Raku++ only.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}

my @log;
sub g { my $n = 0; LEAVE { @log.push('left') } if $n++ == 0; @log.push("n=$n"); 1 }
g();
check @log.join(','), 'n=1,left', 'the condition runs, the phaser fires (rakudo#2243)';

@log = ();
sub k { LEAVE { @log.push('l') } for ^2; 3 }
check k(), '3', 'a for modifier';
check @log.join(','), 'l', '…the phaser fires once';

if $*RAKU.compiler.name eq 'Raku++' {
    @log = ();
    sub foo { LEAVE { @log.push(42) } if False; 99 }
    check foo(), '99', 'the issue\'s sub (Rakudo: setcodeobj needs a code ref)';
    check @log.join(','), '42', '…its LEAVE fires';
    sub h { ENTER { @log.push('in') } unless True; 2 }
    @log = ();
    check h(), '2', 'ENTER with unless';
    check @log.join(','), 'in', '…fires';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
