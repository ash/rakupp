# Regression: `my %h is MyContainer = @pairs` must tell the container that this
# is its INITIALISER.
#
# Rakudo passes `:INITIALIZE` on the declaration's own assignment and on no
# later one — it is how a container knows to build itself rather than to replace
# what it holds. We passed the value alone, always. Map::Agnostic and
# Array::Agnostic declare the parameter REQUIRED, so the first line of their
# suites died "Required named parameter 'INITIALIZE' not passed", and the
# Agnostic family gates seven dists between them.
#
# The second assignment is the row that keeps the fix honest: passing
# :INITIALIZE on every assignment would satisfy the required parameter and still
# be wrong, because a container would rebuild itself where it should replace.
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

my @seen;

class M does Associative {
    has %!s;
    method STORE(\values, :$INITIALIZE) {
        @seen.push($INITIALIZE ?? 'init' !! 'plain');
        %!s = values.list.Map;
        self
    }
    method AT-KEY($k)     { %!s{$k} }
    method EXISTS-KEY($k) { %!s{$k}:exists }
}

my %m is M = (a => 1, b => 2);
ck @seen, ['init'], 'the declaration initialiser is told it is one';
ck %m<a>, 1, '…and the container really took the values';

%m = (c => 3);
ck @seen, ['init', 'plain'], 'a LATER assignment is not an initialiser';
ck %m<c>, 3, '…and still stores';

# a container whose STORE REQUIRES the named parameter — the Agnostic shape,
# which could not survive its own first line
class R does Associative {
    has %!s;
    method STORE(\values, :$INITIALIZE!) { %!s = values.list.Map; self }
    method AT-KEY($k) { %!s{$k} }
}
my %r is R = (x => 9);
ck %r<x>, 9, 'a STORE with a REQUIRED :INITIALIZE binds on the declaration';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
