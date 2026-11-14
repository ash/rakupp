# `Routine.prec` and its kin answer an operator's precedence properties, as in
# Rakudo (rakudo#4780): `&infix:<+>.prec` was "No such method". A declared
# operator's traits copy, tighten (`@=`) or loosen (`:=`) the operator they
# name and keep the declarer's own associativity.

my $fails = 0;
sub check($got, $want, $what) {
    if $got ne $want { say "FAIL: $what: got $got, want $want"; $fails++ }
}
sub p(&op) { &op.prec.sort.map({ "{.key}={.value}" }).join(' ') }

check p(&infix:<+>), 'assoc=left dba=additive prec=t=', 'a built-in';
check p(&infix:<==>), 'assoc=chain dba=chaining diffy=1 iffy=1 prec=m=', 'a chaining one';
check p(&infix:<&&>), 'assoc=left dba=tight-and iffy=1 prec=l= thunky=.t', 'a thunky one';
check p(&postcircumfix:<[ ]>), 'assoc=unary dba=methodcall fiddly=1 prec=y=', 'a postcircumfix';
check p(&prefix:<!>), 'assoc=unary dba=symbolic-unary prec=v=', 'a prefix';

sub infix:<a1>($, $) { }
check p(&infix:<a1>), 'assoc=left dba=default-infix prec=t=', 'a declared infix';
sub infix:<a2>($, $) is equiv(&infix:<*>) { }
check p(&infix:<a2>), 'assoc=left dba=multiplicative prec=u=', 'is equiv';
sub infix:<a3>($, $) is tighter(&infix:<*>) { }
check p(&infix:<a3>), 'assoc=left dba=multiplicative prec=u@=', 'is tighter';
sub infix:<a4>($, $) is looser(&infix:<a3>) { }
check p(&infix:<a4>), 'assoc=left dba=multiplicative prec=u@:=', 'is looser, of a declared one';
sub infix:<a5>($, $) is equiv(&infix:<+>) is assoc<right> { }
check p(&infix:<a5>), 'assoc=right dba=additive prec=t=', 'is assoc';
sub infix:<a6>($, $) is equiv<+> { }
check p(&infix:<a6>), 'assoc=left dba=additive prec=t=', 'is equiv<+>';
sub infix:<a7>($, $) is prec({ prec => "q=", dba => "mine" }) { }
check p(&infix:<a7>), 'dba=mine prec=q=', 'is prec';
sub prefix:<b1>($) is looser(&infix:<+>) { }
check p(&prefix:<b1>), 'assoc=unary dba=additive prec=t:=', 'a prefix keeps its unary';
sub postcircumfix:<⸨ ⸩>($a, $) is equiv(&[+]) is assoc<left> { }
check p(&postcircumfix:<⸨ ⸩>), 'assoc=left dba=additive prec=t=', 'the issue\'s postcircumfix (rakudo#4780)';
sub f { }
check &f.prec.raku, '{}', 'a sub is no operator';
check &infix:<+>.precedence ~ '|' ~ &infix:<+>.associative ~ '|' ~ &infix:<&&>.thunky ~ '|' ~ &infix:<&&>.iffy,
      't=|left|.t|True', '.precedence, .associative, .thunky, .iffy';

# `.prec('prec')` dies in Rakudo (its return type is Hash:D), which is the bug
if $*RAKU.compiler.name eq 'Raku++' {
    check &infix:<+>.prec('prec'), 't=', '.prec(key)';
    check &prefix:<b1>.prec('assoc'), 'unary', '…of a declared one';
}

say $fails ?? "FAIL ($fails)" !! 'PASS';
exit $fails ?? 1 !! 0;
