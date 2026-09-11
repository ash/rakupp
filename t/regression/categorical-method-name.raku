# Regression: `method mod:<null>($/)` — a METHOD whose name carries a
# categorical suffix. Rakudo refuses an unknown category on a SUB (`sub
# mod:<null>` is X::Syntax::Extension::Category) and accepts it on a method,
# because that is how the action methods for a `proto rule` alternative are
# spelled: `multi rule mod:<null>` in the grammar, `method mod:<null>` in the
# actions class. rakupp refused both, and only `dispatch:<…>` was let through.
#
# Red reads a SQLite CREATE TABLE statement with such a grammar
# (Red::Driver::SQLite::SchemaReader), so `use Red` died on it.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape: a grammar and its actions ------------------------
{
    grammar G {
        rule TOP { <mod>+ %% "," }
        proto rule mod {*}
        multi rule mod:<null>     { :i NULL }
        multi rule mod:<not-null> { :i NOT NULL }
    }
    class A {
        method TOP($/)            { make $<mod>».made.join('|') }
        method mod:<null>($/)     { make 'NULL' }
        method mod:<not-null>($/) { make 'NOT-NULL' }
    }
    ck(G.parse('NULL, NOT NULL', actions => A.new).made, 'NULL|NOT-NULL',
       'a categorical action method answers its rule');
}

# --- the name is a plain method name ---------------------------------------
{
    class C { method mod:<null>($x) { "got $x" } }
    ck(C.new."mod:<null>"(7), 'got 7', 'and it is callable by that name');
    ck(?C.^methods.map(*.name).first('mod:<null>'), True, '…and introspects under it');
}

# --- `dispatch:<…>`, the case that already worked, still does --------------
{
    class D { method dispatch:<!>($n, |c) { "dispatched $n" } }
    ck(D.new."dispatch:<!>"('x'), 'dispatched x', 'the metamodel dispatch hooks are untouched');
}

# --- a SUB is still refused ------------------------------------------------
{
    ck(?(try EVAL 'sub mod:<null>() { 1 }'), False, 'an unknown category on a sub is still an error');
    ck(?$!.Str.contains('category'), True, '…and says so');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
