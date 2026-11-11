# --exe: a routine's `my &rs` called by its bare name (`rs`, `rs()`) inside a
# statement the native body hands to the interpreter. In a bundled program a
# routine is compiled natively statement by statement, and one it cannot
# compile (here the coercion `Sym(rs)`) runs interpreted with the routine's
# locals copied in — only those it names with a sigil, so `&rs` stayed behind
# and the call died "Undefined routine 'rs'". Found by t/exe/parity.raku
# (coerce-boxed-builtin-subclass.raku). Every line answers the same under
# Rakudo 2026.09.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want }

role Bundled { }   # (a role: the program bundles, its routines compiled one by one)
class Sym is Str { }
sub coerce-term() { my &rs = -> { "q" }; Sym(rs) }
sub coerce-call() { my &rs = -> { "r" }; Sym(rs()) }
sub plain()       { my &rs = -> { "s" }; uc(rs) }
check coerce-term().^name, 'Sym', 'a bare `rs` in a delegated statement';
check coerce-term() eq 'q', True, '…holding its value';
check coerce-call() eq 'r', True, '`rs()` too';
check plain(), 'S', 'and in a native one';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
