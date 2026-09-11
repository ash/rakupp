# Regression: `[ast-value *]` — a listop call whose only argument is a bare
# Whatever, immediately before a CLOSING bracket. Infix `*` would need a term
# after it and `]` is not one, so the `*` is the argument; the rule was already
# written for `;`, `)`, `,` and end-of-input, and `]` and `}` were missing.
#
# Red::ResultSeq writes `Red::AST::Function.new(:func<count>, :args[ast-value *])`
# in four places, so the whole module failed to parse.
#
# Every expectation was checked against Rakudo.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

sub av($x) { $x.^name }

# --- the reported shape ---------------------------------------------------
ck([av *],              ['Whatever'],  'a bare * before `]` is the argument');
ck([1, av *],           [1, 'Whatever'], '…including after another element');
ck(:args[av *]<args>,   ['Whatever'],  '…and inside a colonpair array composer');
ck({av *}(),            'Whatever',    'a bare * before `}` is the argument too');

# --- the HyperWhatever takes the same rule --------------------------------
ck([av **],             ['HyperWhatever'], 'and a bare ** before `]`');

# --- the spellings that already worked stay working -----------------------
ck((av *),              'Whatever',    'before `)`');
ck([av *,],             ['Whatever'],  'before `,`');
ck(av(*),               'Whatever',    'and the parenthesised call');

# --- what must NOT become an argument -------------------------------------
# `[*]` is the reduction metaop, and a real infix `*` still multiplies.
ck(([*] 1..5),          120,           'the reduce metaop is untouched');
ck([2 * 3],             [6],           'infix * inside brackets still multiplies');
{
    my @a = 1, 2, 3;
    ck(@a[*-1],         3,             'a Whatever subscript is untouched');
}

say $fails ?? "FAIL ($fails)" !! "PASS";
exit $fails ?? 1 !! 0;
