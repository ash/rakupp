# Regression: `flat`'s and `squish`'s exact rules, and `exit` from a worker.
#   * `flat(…)` opens each ARGUMENT one level — an Array counts, so `flat(@a)` is
#     its elements — and then descends through non-itemized sublists only, so an
#     itemized one stays whole wherever it sits. Delegating wholesale to the
#     METHOD is NOT the same rule: that one never opens an Array below the top
#     level, which is exactly what Cro's router walks, and swapping them turned
#     every route into a 404.
#   * `.squish` compares with `===` by default, so 42 and "42" are not adjacent
#     duplicates; `:with` supplies a different comparison.
#   * `sleep-until $instant` answers whether it actually waited.
#   * `exit` ends the PROCESS from whatever thread calls it. Thrown on a worker
#     the ExitEx only broke that thread's Promise and the main thread ran on —
#     `start { sleep 1; exit }` left the parent sleeping to its cap.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# flat opens an argument one level, then descends past non-itemized sublists
check((flat 1, (2, (3, 4), $(5, 6))).gist, '(1 2 3 4 (5 6))', 'an itemized sublist stays whole');
check(flat(1, (2, $(5, 6))).gist,          '(1 2 (5 6))',     'wherever it sits');
check(flat(1, (2, 3)).gist,                '(1 2 3)',         'an ordinary sublist opens');
my @a = 1, 2;
check(flat(@a).gist,                       '(1 2)',           'an array argument opens');
my @nested = [1, [2, 3]];
check(flat(@nested).gist,                  '(1 [2 3])',       'but a nested Array does not');
check((1, (2, (3, 4), $(5, 6))).flat.gist, '(1 2 3 4 (5 6))', 'the method agrees on the doc case');
check((1, $(5, 6)).flat.gist,              '(1 (5 6))',       'and on an itemized element');

# squish compares with ===
check([42, "42"].squish.gist,                          '(42 42)', 'an int and a string are not duplicates');
check([42, "42"].squish(with => &infix:<eq>).gist,     '(42)',    'unless :with says so');
check(<a a b b a>.squish.gist,                         '(a b a)', 'adjacent duplicates collapse');
check(<a a b>.squish(as => &uc).gist,                  '(a b)',   'and :as maps the key');

# sleep-until reports whether it waited
check(sleep-until(now - 5).gist,   'False', 'an instant already past does not wait');
check(sleep-until(now + 0.1).gist, 'True',  'a future one does');

# exit from a worker ends the process
my $prog = 'start { sleep 1; exit 0 }; sleep 60; say "NOT-REACHED"';
my $proc = run($*EXECUTABLE, '-e', $prog, :out, :err);
my $out  = $proc.out.slurp(:close);
check($out.contains('NOT-REACHED'), 'False', 'the main thread does not outlive the exit');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
