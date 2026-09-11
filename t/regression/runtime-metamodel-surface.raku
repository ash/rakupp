# Regression: MOP operations a metaclass performs while composing a type,
# each of which Red's `model` declarator needs and none of which worked here.
#
#   - `unit model Foo is table<sqlite_master>` — a CLASS-level user trait.
#     `is <lowercase name>` was read as a superclass ("cannot inherit from
#     'table'"), which Raku's capitalisation convention keeps clear of.
#   - `.^declares_method('TWEAK')` — does THIS class define it, rather than
#     inherit it? Red asks before installing its own.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- a class-level user trait ---------------------------------------------
{
    my $named;
    multi trait_mod:<is>(Mu:U $type, :$table! --> Empty) { $named = $table }
    class Row is table<sqlite_master> { }
    ck($named, 'sqlite_master', '`is table<…>` is a trait, not a superclass');
    ck(Row.^mro.map(*.^name).join(','), 'Row,Any,Mu', '…so it adds no ancestor');
}
# …and a real superclass is still a superclass
{
    class Parent { method who { "PARENT" } }
    class Child is Parent { }
    ck(Child.new.who, 'PARENT', 'a capitalised `is` is still inheritance');
}

# --- declares_method ------------------------------------------------------
{
    class Decl { method mine { 1 } }
    ck(?Decl.^declares_method('mine'), True,  'a method the class declares');
    ck(?Decl.^declares_method('gist'), False, '…and not one it inherits');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
