# Regression: a return type check sees through the Proxy a bound attribute
# reads back as (issue #77, FCO's item 2).
#
# An attribute bound to a variable (`$!model := $type`) holds the shared
# cell's Proxy, and a method returning it with a return constraint checked
# the Proxy itself: `method model(--> Mu:U) { …; $!model }` died "expected
# Mu:U but got Proxy (BoundType)". `--> BoundType` happened to pass. Red
# resolves its lazy relationship models this way.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class BoundType { }
class Holder {
    has Mu:U $!model;
    method model(--> Mu:U) {
        my $type = BoundType;
        $!model := $type;
        $!model
    }
}
ck Holder.new.model.^name, 'BoundType', 'a type object through `--> Mu:U`';

class Counter {
    has $!n;
    method bind { my $t = 5; $!n := $t; $t }
    method n(--> Int:D) { $!n }
    method s(--> Str()) { $!n }
}
my $c = Counter.new;
$c.bind;
ck $c.n, 5, 'an Int through `--> Int:D`, bound in another method';
ck $c.s, '5', 'a coercing return type converts what the cell holds';

class Wrong {
    has $!w;
    method w(--> Int) { my $t = 'x'; $!w := $t; $!w }
}
my $msg = '';
try { Wrong.new.w; CATCH { default { $msg = .message } } }
ck $msg, 'Type check failed for return value; expected Int but got Str ("x")',
   'a wrong value is still refused, named by what it is';

my $store = 1;
sub through-proxy(--> Int) is rw {
    Proxy.new(FETCH => sub ($) { $store }, STORE => sub ($, $v) { $store = $v })
}
through-proxy() = 9;
ck $store, 9, 'a user Proxy through a typed `is rw` routine still STOREs';
ck through-proxy(), 9, '…and FETCHes';

say 'PASS';
