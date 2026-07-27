# Regression (github.com/ash/rakupp issue #7): the &by block of `.min`/`.max`/
# `.minmax` is the first CODE argument, not the first argument.
#   * `@a.min(:k, { … })` put the `:k` Pair in args[0], so the `args[0].t ==
#     VT::Code` test failed and the mapper was silently dropped — the extremum
#     was then taken over the RAW elements (comparing whole Hashes) and the
#     reported index belonged to a different row. `.min({ … })` was fine, which
#     is what made it look like an adverb bug rather than a mapper bug.
#   * `:by(&code)` was already scanned for separately and always worked; it is
#     kept here so the two spellings stay pinned together.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# the reporter's case: sort by descending priority, then by submit time
my @a = [{:duration(5), :priority(1), :submit(2), :title("jobB")},
         {:duration(8), :priority(3), :submit(3), :title("jobC")},
         {:duration(4), :priority(2), :submit(7), :title("jobD")}];
check(@a.min(:k, { -.<priority>, .<submit> }).gist, '(1)', 'the adverb no longer eats the mapper');
check(@a.min({ -.<priority>, .<submit> })<title>,   'jobC', 'and the plain form is unchanged');
check(@a.max(:k, { -.<priority>, .<submit> }).gist, '(0)', 'max likewise');

# every adverb, with the block on either side
my @w = <apple fig banana>;
check(@w.min(:k,  *.chars).gist, '(1)',          'min :k');
check(@w.min(*.chars, :k).gist,  '(1)',          'and with the block first');
check(@w.max(:k,  *.chars).gist, '(2)',          'max :k');
check(@w.min(:kv, *.chars).gist, '(1 fig)',      'min :kv');
check(@w.min(:p,  *.chars).gist, '(1 => fig)',   'min :p');
check(@w.min(:v,  *.chars).gist, '(fig)',        'min :v');
check(@w.min(:k, :by(*.chars)).gist, '(1)',      'the :by spelling agrees');

# ties: every position attaining the extremum is answered, in order
my @t = <bb a cc d>;
check(@t.min(:k, *.chars).gist, '(1 3)', 'both shortest');
check(@t.max(:k, *.chars).gist, '(0 2)', 'both longest');

# no mapper at all still compares the elements themselves
check((4, 7, 2).min(:k).gist, '(2)', 'min :k with no block');
check((4, 7, 2).max(:k).gist, '(1)', 'max :k with no block');
check((4, 7, 2).min,          '2',   'and the bare form');

# minmax takes its mapper the same way; the endpoints stay the ORIGINAL elements
check((1, 2, 3).minmax(:by(-*)).gist, '3..1', 'minmax :by');
check((1, 2, 3).minmax({ -$_ }).gist, '3..1', 'minmax with a leading block');
check((1, 2, 3).minmax.gist,          '1..3', 'and unmapped');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
