# Regression: `nextsame`/`nextwith` from a method that overrides a BUILT-IN one.
# When the invocant is a plain user object there is no builtin box to dispatch
# on, so the redispatch runs on the object ITSELF — and landed straight back on
# the method that asked for it. `class E is Exception { method throw { nextwith
# $bt } }` (Red's mapped-driver exceptions) recursed 22,456 frames deep.
#
# The guards on .throw, .Str/.gist and .backtrace say "a class that defines this
# name wins", which is right for an ordinary call and exactly wrong for a
# redispatch — `new` had already learned that.
#
# …and `class A is Exception` reported its ancestor TWICE (`A,Exception,
# Exception,Any,Mu`): the parent arrives once as the named ClassInfo and once
# from the built-in ancestry.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape ---------------------------------------------------
{
    class E1 is Exception {
        has $.note;
        method message { "MSG:$!note" }
        method throw is hidden-from-backtrace { nextwith }
    }
    ck((try E1.new(note => 'z').throw) // $!.message, 'MSG:z',
       'nextwith reaches the built-in Exception.throw');
    ck($!.^name, 'E1', '…and what it threw is the exception itself');
}

# --- the other redispatch spellings, and the other guarded names ----------
{
    class E2 is Exception { method message { "M2" }; method throw { nextsame } }
    class E3 is Exception { method message { "M3" }; method throw { callsame } }
    class E4 is Exception { method message { "M4" }; method gist  { nextsame } }
    ck((try E2.new.throw) // $!.message, 'M2', 'nextsame');
    ck((try E3.new.throw) // $!.message, 'M3', 'callsame');
    ck(E4.new.gist, 'M4', 'gist redispatches to the exception gist');
}

# --- a USER ancestor was never broken, and must stay working -------------
{
    class Base { method greet($x) { "BASE($x)" } }
    class Kid  is Base { method greet($x) { nextwith "kid-$x" } }
    class Kid2 is Base { method greet($x) { nextsame } }
    ck(Kid.new.greet('a'),  'BASE(kid-a)', 'nextwith through a user parent');
    ck(Kid2.new.greet('b'), 'BASE(b)',     'nextsame through a user parent');
}
{
    class C1 { method clone { nextsame } }   # over Mu.clone
    ck(C1.new.clone.^name, 'C1', 'a builtin behind a user method with no user parent');
}

# --- and the MRO the same ancestry produces -------------------------------
{
    class A1 is Exception { }
    class A3 { }
    class A4 is A3 { }
    role R1 { }
    class A6 does R1 { }
    ck(A1.^mro.map(*.^name).join(','), 'A1,Exception,Any,Mu', 'a built-in parent appears once');
    ck(A4.^mro.map(*.^name).join(','), 'A4,A3,Any,Mu',        'a user parent is untouched');
    ck(A6.^mro.map(*.^name).join(','), 'A6,Any,Mu',           'a composed role is not an ancestor');
    ck(X::AdHoc.^mro.map(*.^name).join(','), 'X::AdHoc,Exception,Any,Mu', 'a built-in class too');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
