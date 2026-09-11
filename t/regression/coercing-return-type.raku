# Regression: `--> Map()` CONVERTS the returned value; it does not constrain it.
# The parens were skipped along with the rest of the signature tail, so the
# coercion was read as a plain `--> Map` type check and `method exports(-->
# Map())` — which is how Red hands its export set to `use` — failed with
# "expected Map but got List" and took the whole module's exports with it.
#
# `(a => 1).Map` is the coercion's own route, and it answered a Hash: the
# list-to-hash builder served `.hash`, `.Hash` and `.Map` alike and stamped
# none of them Map.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- the reported shape ---------------------------------------------------
{
    sub exports(--> Map()) { (a => 1, b => 2) }
    my $m = exports();
    ck($m.^name, 'Map', 'a List returned through `--> Map()` becomes a Map');
    ck($m<a>,    1,     '…with its pairs intact');
}

# --- the same on a method, and for a scalar coercion ----------------------
{
    class C { method m(--> Map()) { (x => 9,) } }
    ck(C.new.m.^name, 'Map', 'a method coerces on the way out too');
    sub n(--> Int()) { '42' }
    ck(n(), 42,          'a Str returned through `--> Int()` becomes an Int');
    ck(n().^name, 'Int', '…and reports as one');
}

# --- .Map is a Map whatever it started as ---------------------------------
ck((a => 1, b => 2).Map.^name, 'Map', 'a list of pairs');
ck(%(a => 1).Map.^name,        'Map', 'a hash');
ck((a => 1).Map.^name,         'Map', 'a single pair');
ck(('a', 1).Map.^name,         'Map', 'a flat key/value list');
ck((a => 1).Hash.^name,        'Hash', '…while .Hash still answers a Hash');
ck((a => 1, b => 2).hash.^name, 'Hash', '…and so does .hash');

# --- a NON-coercing return type still CHECKS ------------------------------
{
    sub strict(--> Map) { my %h; %h }
    ck(?(try strict()), False, '`--> Map` still refuses a Hash');
    sub ok-strict(--> Int) { 7 }
    ck(ok-strict(), 7, '…and passes what it should');
    ck(&ok-strict.returns.^name, 'Int', 'and .returns still names the type');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
