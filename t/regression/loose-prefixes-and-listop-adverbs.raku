# Regression: `so`/`not` as LOOSE PREFIXES, and adverbs reaching a listop's method.
#   * `so` and `not` are prefix operators that bind TIGHTER than the comma, not
#     listops that swallow the whole list — `f(so ($x), 2)` passes two arguments and
#     `so 1, 2` is `(so 1), 2`.
#   * `not` also has to be recognised as the START of a term. It sits in the
#     block-keyword set (it is a word, like `and`/`or`), which made `say not 0` parse
#     as a zero-argument `say` followed by `not 0` — it printed a blank line.
#   * the SUB forms of the list routines forward an adverb they understand to the
#     method instead of sweeping it into the list: `unique @a, as => {.abs}` was
#     uniquing the adverb along with the elements.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# so/not bind tighter than the comma
sub two($a, $b) { "$a|$b" }
check(two(so (1 == 1), 2), 'True|2', 'so takes one argument');
check(two(not 0, 2),       'True|2', 'and so does not');
check((so 1, 2).raku,      '(Bool::True, 2)', 'so 1, 2 is (so 1), 2');
check((not 1, 2).raku,     '(Bool::False, 2)', 'and not likewise');
# …and still work everywhere they did
check(so(5).gist,          'True',  'the parenthesised call form');
check((so any(1,2,3) > 2).gist, 'True', 'over a junction comparison');
check((not 0).gist,        'True',  'a parenthesised not');
my $said = 0;
$said = 1 if not 0;
check($said, '1', 'not in a statement modifier');
check(do { my @o; @o.push('y') if not 0; @o.join }, 'y', 'and in a block');

# `not` starts a term
check((not 0).Str, 'True', 'not as a term');
# a trailing adverb belongs to the operand, not to `not`
my %h = :1a;
check((not %h<b>:exists).gist, 'True',  'not over an :exists subscript');
check((not %h<a>:exists).gist, 'False', 'and over one that is there');

# listop adverbs reach the method
check((unique <a A b>, as => &lc).gist,   '(a b)', 'unique :as as a named argument');
check((unique <a A b>, :as(&lc)).gist,    '(a b)', 'in colon-pair spelling too');
check((unique <a A b>).gist,              '(a A b)', 'and without one');
check((squish <a A a>, as => &lc).gist,   '(a)',   'squish :as');
check((repeated <a A b>, as => &lc).gist, '(A)',   'repeated :as');
# a genuine Pair ELEMENT is still data
check((unique (a => 1), (a => 1)).gist,   '(a => 1)', 'a Pair element is not an adverb');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
