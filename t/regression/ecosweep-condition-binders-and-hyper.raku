# Regression: two more gaps from the full ecosystem sweep.
#
# 1. A SIGILLESS binder on a statement condition — `if EXPR -> \name { }` —
#    parses and binds (Array::Agnostic's pop, List::MoreUtils' firstres). The
#    typed and sigiled forms came from the fresh-100 sweep; the `\name` form
#    is the same machinery.
# 2. `.hyper` / `.race` run serially: the stand-in is the flat ordered list a
#    Seq would give, so .map/.grep/.sum downstream just work, and the
#    :degree/:batch tuning nameds are accepted and ignored (Ecosystem,
#    JSON::Fast::Hyper call this surface).
#
# Oracle-verified against Rakudo 2026.07 (all shapes print identically).

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# 1 — sigilless condition binders
my $r = '';
if 5 -> \v { $r = v * 2 }
ok($r == 10, 'if EXPR -> \v binds the condition value');

my $s = '';
if 0 -> \v { }
elsif 'yes' -> \w { $s = w }
ok($s eq 'yes', 'elsif EXPR -> \w binds too');

class Stack {
    has @!e;
    method elems { @!e.elems }
    method push-one { @!e.push(1); self }
    method pop-report {
        if self.elems -> \elems {
            "have {elems}"
        }
        else {
            'empty'
        }
    }
}
ok(Stack.new.pop-report eq 'empty', 'the Agnostic shape, empty side');
ok(Stack.new.push-one.push-one.pop-report eq 'have 2', 'the Agnostic shape, bound side');

# 1b — the negation metaop tight on a parenthesized set operator is ONE
#      negated infix, not prefix-not on a term (Template6's
#      `* !(elem) $raw-words`); prefix-not on real parens stays itself
my @set = 1, 2, 3;
# (assigned first, compared after: under Rakudo 2026.07 the INLINE
# `(5 !(elem) @set) === True` flips to False once any `-> \x` binder appeared
# earlier in the file — a state-dependent quirk of theirs, since the same
# expression is True standalone and True through a variable either way)
my $not-in = 5 !(elem) @set;
my $in     = 2 !(elem) @set;
ok($not-in === True,  '5 !(elem) @set negates the infix');
ok($in === False,     '2 !(elem) @set is False');
ok((1..9).first(* !(elem) @set) == 4, 'the Template6 shape: first(* !(elem) …)');
ok(!(1 == 2) === True, 'prefix ! on a parenthesized expression is untouched');

# 2 — hyper/race as ordered serial lists
ok((1..10).hyper.map(* * 2).List eqv (2, 4, 6, 8, 10, 12, 14, 16, 18, 20),
   '.hyper.map is the ordered map');
ok(<a b c>.race.grep(* eq 'b').List eqv ('b',), '.race.grep filters');
ok((1..5).hyper(:2batch, :4degree).sum == 15, 'tuning nameds are accepted');
my %h = a => 1, b => 2;
ok(%h.hyper.map(*.key).sort.List eqv ('a', 'b'), 'Hash.hyper iterates pairs');

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
