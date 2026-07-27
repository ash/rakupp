# Regression: things a list-op will accept as an argument, and what the empty
# container routines answer.
#   * a NAMED inline `class Foo {…}` is a term, so `say class Foo {}` passes one
#     argument. Only the ANONYMOUS form was allowed, so it parsed as a nullary
#     `say` and printed a blank line.
#   * `∞` lexes as an Op, which made `[∞]` look like a reduction over an operator
#     named `∞` rather than a one-element array literal.
#   * an object hash with no value type is `Hash[Any, K]` — a missing key answers
#     Any, not Mu.
#   * `roundrobin` takes `:slip`, which flattens the rounds; it was being swept
#     into the first round as a data Pair.
#   * the SUB forms `shift @a` / `pop @a` answer the same Failure the METHOD forms
#     already did instead of a silent Any.
#   * `open :w, $path` — the path is the first POSITIONAL. Taking args[0] blindly
#     opened a file literally named "w\tTrue".
#   * `.produce` is a Seq, and `last` inside a reduce/produce block ends the fold
#     rather than escaping as "last without loop construct". produce DROPS the
#     most recent running value when it stops, because Rakudo produces lazily and
#     so lags one behind (checked against four cases, see the comment in the fix).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# terms a list-op accepts
check((class Foo {}).gist, '(Foo)', 'a named inline class is one argument');
check((class {}).^name.chars > 0, 'True', 'and the anonymous form still is');
check([∞].elems, '1', 'an array literal holding infinity');
check([∞].gist,  '[Inf]', 'which is the value, not a reduction');
check([-∞, ∞].gist, '[-Inf Inf]', 'and two of them');
check(([+] 1, 2, 3), '6', 'a real reduction still reduces');
check(([~] <a b c>), 'abc', 'and a string one');

# an object hash defaults to Any
my %o{Int};
check(%o.WHAT.gist, '(Hash[Any,Int])', 'an object hash with no value type');
check(%o.of.gist,   '(Any)',           'its value type');
check(%o{9}.WHAT.gist, '(Any)',        'and a missing key');
my Int %t{Str};
check(%t.of.gist, '(Int)', 'an explicit value type is untouched');

# roundrobin
check(roundrobin(<a b c>, <d e f>).gist,        '((a d) (b e) (c f))', 'roundrobin rounds');
check(roundrobin(<a b c>, <d e f>, :slip).gist, '(a d b e c f)',       ':slip flattens them');
check(roundrobin((1,2), (3,), (4,5,6)).gist,    '((1 3 4) (2 5) (6))', 'ragged input');

# the empty-container sub forms
my @e = 1;
check(shift(@e), '1', 'shift returns the element');
check((shift @e).defined, 'False', 'and a Failure once empty');
my @f = 1;
check(pop(@f), '1', 'pop likewise');
check((pop @f).defined, 'False', 'and its Failure');

# open takes the first positional as the path
my $p = $*TMPDIR.add('rakupp-open-test.txt');
$p.unlink if $p.e;
my $fh = open :w, $p.Str;
$fh.print("ok\n");
$fh.close;
check($p.slurp.chomp, 'ok', 'open :w, $path writes to $path');
check($p.e.gist, 'True', 'and creates only that file');
$p.unlink;

# folds
check((1, 2, 3).produce({ $^a + $^b }).raku, '(1, 3, 6).Seq', 'produce is a Seq');
sub stop-past-seven { last if $^a > 7; $^a + $^b }
check((2, 3, 4, 5).reduce(&stop-past-seven), '9', 'last ends a reduce');
check((2, 3, 4, 5).produce(&stop-past-seven).gist, '(2 5)', 'and a produce, dropping the last');
sub stop-at-once { last if $^a > 0; $^a + $^b }
check((1, 2, 3).produce(&stop-at-once).raku, '().Seq', 'stopping immediately produces nothing');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
