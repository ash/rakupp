# Regression: a batch found by running HTTP::Tiny's own test suite.
#   * the sequence operator is a LIST infix (looser than comma) with Rakudo's
#     list-of-lists semantics, so `'A'...'Z', 'a'...'z'` is ONE sequence
#   * `»` over a Blob visits its elements
#   * `.=` mutates through an indirect `&sub` call
#   * a `&`-sigil named parameter only binds a Callable
#   * an undefined invocant answers the reducing list methods
#   * `for %h.values.grep(…)` still aliases the hash slots
#   * `for $scalar` aliases the variable, but must not clobber a write made
#     through its own name
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# --- sequence operator: list-associative -----------------------------------
ck (1 ... 5 ... 1).List, (1,2,3,4,5,4,3,2,1), 'a chained sequence turns around';
ck ('A' ... 'C', 'a' ... 'c', 0 ... 2, '+', '/').List,
   ('A','B','C','a','b','c',0,1,2,'+','/'),
   'comma groups chain into one sequence (the base64 alphabet idiom)';
ck (1 ... 3, 9).List, (1,2,3,9), 'the tail of an endpoint group is emitted verbatim';
ck (1 ... 3, 9, 20).List, (1,2,3,9,20), 'and all of it';
ck (1, 2, 4 ... 32).List, (1,2,4,8,16,32), 'a plain seed list still works';
ck ((1...3), 9).elems, 2, 'parens still close the sequence off';
ck (1 ...^ 5).List, (1,2,3,4), 'the exclusive form is unaffected';
ck (1, 1, *+* ...^ * > 20).List, (1,1,2,3,5,8,13), 'a generator closure still seeds';

my constant %b64 = ('A'...'Z', 'a'...'z', 0...9, '+', '/').pairs;
ck %b64.elems, 64, 'the base64 alphabet has 64 entries';
ck %b64{0}, 'A', 'index 0 is A';
ck %b64{26}, 'a', 'index 26 is a';
ck %b64{63}, '/', 'index 63 is /';

# --- hyper over a Blob ------------------------------------------------------
ck "foo".encode».fmt('%08b').List, ('01100110','01101111','01101111'),
   'a hyper method call visits each byte of a Blob';
ck "abc".encode>>.chr.join, 'abc', 'and the same through >>';

# --- .= through an indirect &sub call ---------------------------------------
sub bang($s) { $s ~ '!' }
my $m = 'x';
$m .= &bang;
ck $m, 'x!', '.= &sub assigns the result back';
my $m2 = 'y';
$m2 .= &bang with $m2;
ck $m2, 'y!', 'and it survives a `with` modifier';

# --- a `&`-sigil named parameter is Callable-only ---------------------------
multi sub pick-one( Str:D :$c, |rest ) { 'str' }
multi sub pick-one( $a, $b, :&c ) { 'callable' }
ck pick-one('P', 'U', c => 'x'), 'str', 'a Str never binds a :&named';
ck pick-one('P', 'U', c => sub {}), 'callable', 'a Callable still does';

# --- the reducing list methods on an undefined invocant ---------------------
my %empty;
ck %empty<nope>.first({ .defined }).defined, False, '.first on Any is undefined';
ck %empty<nope>.join('-'), '', '.join on Any is empty';
ck (%empty<nope>.head).defined, False, '.head on Any is undefined';

# --- for over a filtered .values still aliases ------------------------------
my %g = a => '@x', b => 'y';
for %g.values.grep(*.starts-with('@')) { $_ = .substr(1) }
ck %g, {a => 'x', b => 'y'}, 'a grepped .values loop writes into the hash';

my %p = req => { named => { content => { file => '@t/f.txt', other => 'plain' } } };
with %p<req><named><content> {
    for .values.grep: *.starts-with('@') { $_ = .substr(1) }
}
ck %p<req><named><content><file>, 't/f.txt', 'and reaches it through the topic';

# --- topic aliasing, and what it must NOT overwrite -------------------------
my $d = 'foo';
for $d { s/f/F/ }
ck $d, 'Foo', 'a scalar `for` topic aliases the variable';
my $e = 'bar';
given $e { s/b/B/ }
ck $e, 'Bar', 'and so does `given`';

my Blob[uint8] $chunk;
$chunk .= new without $chunk;
ck $chunk.bytes, 0, 'a `without` body may assign the topic variable by name';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
