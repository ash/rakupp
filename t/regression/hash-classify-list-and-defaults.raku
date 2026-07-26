# Regression: `.classify-list`/`.categorize-list`, and an `is default` that
# survives assignment.
#   * `%h.classify-list($mapper, *@values)` files the values INTO the invocant
#     and answers it. A list-valued key NESTS — ("1a","1b") files under
#     %h<1a><1b> — which is what separates it from plain `.classify`.
#     `.categorize-list` files under every key the mapper yields instead.
#   * `my %h is default(1) = …` lost its element default: assignment REFILLS the
#     container, so the trait (which lives on the container, beside its element
#     type) has to survive that. `.default` answered Any and a missing key had no
#     default at all.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# classify-list files into the invocant
my %h1;
%h1.classify-list({ $_ %% 2 ?? 'even' !! 'odd' }, ^10);
check(%h1.gist, '{even => [0 2 4 6 8], odd => [1 3 5 7 9]}', 'classify-list-with-a-block');
# an existing key is kept, and a LIST mapper indexes by the value
my @mapper = <zero one two three four five>;
my %h2 = foo => 'bar';
%h2.classify-list(@mapper, 1, 2, 3, 4, 4);
check(%h2.gist, '{foo => bar, four => [4 4], one => [1], three => [3], two => [2]}',
      'classify-list-keeps-what-was-there');
# a list-valued key nests
my %h3;
%h3.classify-list([['1a','1b','1c'],['2a','2b','2c'],['3a','3b','3c']], 1,2,1,1,2,0);
check(%h3.gist, '{1a => {1b => {1c => [0]}}, 2a => {2b => {2c => [1 1 1]}}, 3a => {3b => {3c => [2 2]}}}',
      'classify-list-nests-a-list-key');
my %h4;
%h4.classify-list([['1a','1b'],['2a','2b'],['3a','3b']], 1,0,1,1,1,0,2);
check(%h4.gist, '{1a => {1b => [0 0]}, 2a => {2b => [1 1 1 1]}, 3a => {3b => [2]}}',
      'classify-list-nests-two-deep');
# categorize-list files under every key
my %h5;
%h5.categorize-list({ $_ %% 2 ?? 'even' !! 'odd' }, ^4);
check(%h5.gist, '{even => [0 2], odd => [1 3]}', 'categorize-list');
# the answer IS the invocant
my %h6;
check((%h6.classify-list({ 'k' }, 1) === %h6).Bool, 'True', 'classify-list-answers-the-invocant');
# plain .classify is unchanged
check(<a bb ccc>.classify(*.chars).gist, '{1 => [a], 2 => [bb], 3 => [ccc]}', 'classify-is-unchanged');

# `is default` survives assignment
my %d is default(1) = 'apples' => 3, 'oranges' => 7;
check(%d.default,            '1', 'a-hash-default-survives-assignment');
check(%d{'apples'} + %d{'bananas'}, '4', 'and-fills-a-missing-key');
check(%d{'apples'},          '3', 'a-present-key-is-unaffected');
my @a is default(9) = 1, 2;
check(@a.default,            '9', 'an-array-default-survives-assignment');
check(@a[5],                 '9', 'and-fills-an-unassigned-slot');
check(@a[0],                 '1', 'an-assigned-slot-is-unaffected');
# without the trait there is no default
my %plain;
check(%plain.default.gist,   '(Any)', 'no-trait-means-no-default');
my @plain;
check(@plain.default.gist,   '(Any)', 'the-same-for-an-array');
# a declaration with no assignment still works
my %e is default(0);
%e<a>++;
check(%e.raku,               '{:a(1)}', 'the-default-feeds-an-increment');
my $s is default(3);
check($s,                    '3', 'a-scalar-default-is-unchanged');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
