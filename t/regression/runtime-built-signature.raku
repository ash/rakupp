# A signature composed at RUNTIME — GitHub issue #84.
#
# `Parameter.new(type => $t)` and `Signature.new(:params(…), :returns(…))` are
# how a program reaches a C function whose argument list is not known until it
# runs: a variadic one, where there is no `is native` sub to declare because the
# types come from a list. Neither constructor existed, so the reporter's program
# died on its first line with "No such method 'new' for invocant of type
# 'Parameter'".
#
# Three things had to be true for that program to run, and each is checked here:
#
#   1. Parameter.new / Signature.new build the SAME shape the introspection path
#      builds for a parsed signature, so every accessor reads either;
#   2. a `:( … )` literal keeps its `--> T`. It was parsed and then dropped, so
#      `.returns` answered Mu — which no one noticed until a signature's return
#      type had to reach libffi;
#   3. `nativecast($signature, $ptr)` answers a CALLABLE. It used to fall
#      through to the plain-Int return, so the cast handed back an address and
#      calling it died "Cannot invoke non-Callable value of type Int". And
#      `cglobal($lib, 'f', Pointer)` has to answer the SYMBOL'S ADDRESS rather
#      than dereference it, or that Callable points at the first eight bytes of
#      the function's own code.
#
# Passes under both rakupp and Rakudo.

use NativeCall;

my $fails = 0;
sub check($got, $want, Str $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "not ok - $desc"; note "GOT [{$got.raku}] WANT [{$want.raku}]" }
}

# ---- 1. the constructors ---------------------------------------------------
{
    my $p = Parameter.new(type => Int);
    check $p.gist, 'Int $', 'a nameless typed parameter gists as its type and a bare sigil';
    check $p.type.^name, 'Int', '…and .type answers the type object';
    check $p.name, '', '…with no name';
    check ($p.named, $p.optional, $p.slurpy), (False, False, False), '…and none of the quantifiers';

    check Parameter.new.gist, '$', 'an empty Parameter is a bare sigil';
    check Parameter.new(type => Str, name => '$x', :optional).gist, 'Str $x?',
          'an optional named-slot parameter carries its ?';
    my $n = Parameter.new(type => Int, name => '$n', :named);
    check $n.gist, 'Int :$n', 'a :named parameter carries its colon';
    check $n.optional, True, '…and is optional without being asked, as `:$n` is';
}

{
    my @p = Parameter.new(type => Int, name => '$a'), Parameter.new(type => Str);
    my $s = Signature.new(:params(|@p), :returns(Bool));
    check $s.gist, '(Int $a, Str --> Bool)', 'a constructed signature renders like a parsed one';
    check $s.arity, 2, '…with the required positionals as its arity';
    # numeric, not eqv: Rakudo's constructed .count is a Num (2e0) where its
    # parsed one and ours are Int — the VALUE is what a caller uses
    check ($s.count == 2), True, '…and every positional as its count';
    check $s.returns.^name, 'Bool', '…and the return type it was given';
    check $s.params.elems, 2, '…holding the parameters it was given';
    check $s.params[1].type.^name, 'Str', '…which answer as parameters';

    check Signature.new.gist, '( --> Mu)', 'an empty signature returns Mu';
}

# ---- 2. a signature LITERAL keeps its return type ---------------------------
{
    check :(Int $x --> Bool).returns.^name, 'Bool', '`:( --> T)` records T';
    check :(Int $x, Str $y --> Bool).gist, '(Int $x, Str $y --> Bool)', '…and renders it';
    check :(Int $x).returns.^name, 'Mu', '…and a signature without one still answers Mu';
}

# ---- 3. the cast, and a real crossing --------------------------------------
# strcmp is in libc, and an undefined library means "whatever this process has
# already loaded" — which is the portable spelling of "the C library".
my $ptr = try cglobal(Str, 'strcmp', Pointer);
if $ptr.defined {
    my @p = Parameter.new(type => Str), Parameter.new(type => Str);
    my $sig = Signature.new(:params(|@p), :returns(int32));
    check $sig.gist, '(Str, Str --> int32)', 'the signature a C function is reached through';

    my $f = nativecast($sig, $ptr);
    check ($f ~~ Callable), True, 'nativecast of a signature answers a Callable';
    check $f('abc', 'abc'), 0, '…and calling it crosses into C';
    check ($f('abc', 'abd') < 0), True, '…with the arguments in the right order';
}
else {
    note '# strcmp not reachable through cglobal here — skipping the crossing';
}

# ---- and the reporter's own shape ------------------------------------------
# The types arrive in a list, a parameter is made for each, and the list is
# ended with a Pointer — which is what a variadic C function's sentinel argument
# needs and what no `is native` declaration can spell.
{
    my @parameterList = ();
    for (uint32, int32, Str) -> $t { @parameterList.push: Parameter.new(type => $t) }
    @parameterList.push: Parameter.new(type => Pointer);
    my $signature = Signature.new(:params(|@parameterList), :returns(Pointer));
    check $signature.params.elems, 4, 'a signature built from a list of types has every one';
    check $signature.arity, 4, '…and counts them all as required';
    check $signature.params[0].type.^name, 'uint32', '…keeping the native types';
}

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit($fails ?? 1 !! 0);
