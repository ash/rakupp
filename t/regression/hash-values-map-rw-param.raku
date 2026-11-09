# `%h.values.map` and `.grep` bind an `is rw`, raw or `\x` parameter to the
# hash's own value, as Rakudo's `.values` hands out its containers: `%h.
# values.map(-> $v is rw { $v++ })` left %h as it was, as did `&inc` and
# `-> \v { … }`; only `$_` was written back. A typed hash's value answers to
# its type, a QuantHash's weight of 0 deletes the key, and a plain
# parameter stays a copy.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

my %h = a => 1, b => 2;
%h.values.map(-> $v is rw { $v++ }).eager;
ck %h.sort.List, (a => 2, b => 3), 'is rw';
sub inc($x is rw) { $x++ }
%h.values.map(&inc).eager;
ck %h.sort.List, (a => 3, b => 4), 'a routine';
%h.values.map(-> \v { v = 10 }).eager;
ck %h.sort.List, (a => 10, b => 10), 'a sigilless one';
ck %h.values.grep(-> $v is rw { $v++; True }).sort.List, (11, 11), '.grep';
ck %h.sort.List, (a => 11, b => 11), 'and it wrote';

my Int %t = a => 1;
ck (try { %t.values.map(-> $v is rw { $v = "x" }).eager; 1 }) // $!.message,
   'Type check failed for an element of %t; expected Int but got Str ("x")', 'a typed hash';
ck %t<a>, 1, 'kept';
my $s = SetHash.new(<a b>);
$s.values.map(-> $v is rw { $v = False }).eager;
ck $s.elems, 0, 'a SetHash';
ck (%h.values.map(-> $v { $v * 2 }).List, %h.values.map({ $_ - 1 }).List), ((22, 22), (10, 10)), 'a copy, and $_, as before';

say 'PASS';
