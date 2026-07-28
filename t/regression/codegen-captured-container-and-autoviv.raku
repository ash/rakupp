# Regression (github.com/ash/rakupp issue #8): two --exe codegen bugs, both about
# writing to a container through a subscript.
#
#   * a closure that mutates a captured hash or array BY KEY was not detected as
#     mutating it, so the variable was captured by value, came out `const` inside
#     the generated lambda, and the C++ did not compile:
#       "binding reference of type Value& to const Value discards qualifiers".
#     The cell analysis only counted a bare VarExpr target; `%bag{$k}++`,
#     `@r[$i] += 1` and `@r[$i].push(…)` all write through an Index.
#
#   * `.push`/`.append`/`.unshift`/`.prepend` on a not-yet-existing element was a
#     SILENT wrong answer, which is worse: ex() on an Index yields rtIndexGet,
#     which returns a fresh Any for a missing element, so the push landed in a
#     temporary and vanished. `@ready[2].push(10)` left @ready empty and
#     @ready[2] reading back as Nil — which made the reporter's program loop
#     forever waiting for an element that never arrived.
#     (A plain `@a.push` was always fine: the Value copy shares the same arr
#     shared_ptr. Only the autovivifying case needed the lvalue.)
#
# The --exe halves are covered by t/run.raku's native-parity fixture; what is
# pinned here is the behaviour the compiled form must match.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# autovivifying push through a subscript
my @ready;
@ready[2].push(10);
@ready[2].push(11);
check(@ready[2].elems, '2',        'push autovivifies the inner Array');
check(@ready[2].gist,  '[10 11]',  'and both elements are there');
check(@ready.elems,    '3',        'the OUTER array grew to hold it');
check(@ready[2].shift, '10',       'and it can be shifted back out');
check(@ready[2].elems, '1',        'leaving the rest');

my %h;
%h<k>.push(1);
check(%h<k>.gist, '[1]', 'the same through a hash key');
my @u;
@u[1].unshift(9);
check(@u[1].gist, '[9]', 'and for unshift');

# a closure mutating captured containers by key
sub tally(@in) {
    my (%bag, @sum, $max);
    my @doubled = @in.map({
        %bag{$_}++;
        @sum[$_] += 10;
        $max max= $_;
        $_ * 2
    });
    return %bag, @sum, $max, @doubled;
}
my ($bag, $sum, $max, $doubled) = tally([1, 2, 2]);
check($bag.gist,     '{1 => 1, 2 => 2}', 'a captured hash counts by key');
check($sum[2],       '20',               'a captured array accumulates by index');
check($max,          '2',                'a captured scalar still works');
check($doubled.gist, '[2 4 4]',          'and the map result is unaffected');

# push into a captured array-of-arrays, the shape from the issue
sub bucket(@in) {
    my @by;
    @in.map({ @by[$_ % 2].push($_) });
    @by
}
check(bucket([1,2,3,4]).gist, '[[2 4] [1 3]]', 'pushing into a captured nested array');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
