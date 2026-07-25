# Regression: HyperWhatever (`**`) as a TERM and as a list-match wildcard.
#   1. `**` could only be the exponent infix. In term position — after a comma,
#      or as a bare listop argument — it is a HyperWhatever. The listop case is
#      gated on what FOLLOWS (`;`, `)`, `,`, EOF), since infix `**` would need a
#      term there and there isn't one.
#   2. In a list pattern `**` matches a RUN of elements, including none, so
#      `(1, 2, 4, 8) ~~ (1, **, 8)` holds. Matched by greedy globbing with
#      backtracking over the two lists.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. `**` as a term
sub kind($arg) { $arg.^name }
check(kind(**),        'HyperWhatever', 'hyperwhatever-in-parens');
check(kind( ** ),      'HyperWhatever', 'hyperwhatever-spaced');
my $hw = **;
check($hw.^name,       'HyperWhatever', 'hyperwhatever-assigned');
check((1, **).elems,   '2',             'hyperwhatever-after-a-comma');
check([1, **, 8].elems, '3',            'hyperwhatever-in-an-array-literal');
my @l = 1, **, 8;
check(@l.elems,        '3',             'hyperwhatever-in-a-list-assignment');

# the exponent infix is untouched
check(2 ** 3,          '8',   'infix-pow-spaced');
check(2**3,            '8',   'infix-pow-tight');
my $n = 2;
check($n ** 3,         '8',   'infix-pow-on-a-variable');
check((2 ** 3 ** 2),   '512', 'infix-pow-is-right-associative');

# 2. `**` matches a run of elements in a list pattern
check(((1, 8)                ~~ (1, **, 8)).Bool, 'True',  'matches-nothing');
check(((1, 2, 4, 5, 6, 7, 8) ~~ (1, **, 8)).Bool, 'True',  'matches-a-run');
check(((1, 2, 8, 9)          ~~ (1, **, 8)).Bool, 'False', 'must-still-reach-the-end');
check(((1, 2, 3)             ~~ (**)).Bool,       'True',  'matches-everything');
check((()                    ~~ (**)).Bool,       'True',  'matches-the-empty-list');
check(((1, 2, 3)             ~~ (**, 3)).Bool,    'True',  'anchored-at-the-end');
check(((1, 2, 3)             ~~ (1, **)).Bool,    'True',  'anchored-at-the-start');
check(((1, 2, 3)             ~~ (1, 2, 3)).Bool,  'True',  'plain-list-match-unaffected');
check(((1, 2, 3)             ~~ (1, 2)).Bool,     'False', 'plain-list-mismatch-unaffected');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
