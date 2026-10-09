# Regression, 2026-10-09: the method spellings of element assignment,
# ASSIGN-POS and ASSIGN-KEY, store an Array or a Hash in the slot's Scalar,
# as `@a[$i] = …` and `%h<k> = …` always have. They stored it bare, so a
# value read back through an attribute array or `.AT-POS` lost its `$`.
# Hash::Ordered keeps every value through `@!values.ASSIGN-POS`, and
# BSON::Simple's decoded documents then differed from the expected ones in
# `.raku` — and so in `eqv`, which compares an object's `.raku` (8 of its 92
# tests). Expected values are Rakudo 2026.09's.
use Test;
plan 8;

class Store {
    has Mu @.values;
    method put(\value) { @!values.ASSIGN-POS(0, value); @!values[0].raku }
    method put-lit()   { @!values.ASSIGN-POS(0, [10]); @!values.AT-POS(0).raku }
}
is Store.new.put([10]), '$[10]', 'ASSIGN-POS on an attribute array itemizes an Array';
is Store.new.put-lit, '$[10]', '…read back through AT-POS too';
is Store.new.put({a => 1}), '${:a(1)}', '…and a Hash';
is Store.new.put(5), '5', 'a plain value stays plain';

my @g; @g.ASSIGN-POS(0, {a => 1});
is @g[0].raku, '${:a(1)}', 'ASSIGN-POS of a Hash on a lexical array';
my %h; %h.ASSIGN-KEY('k', [10]);
is %h<k>.raku, '$[10]', 'ASSIGN-KEY itemizes an Array';
is %h.raku, '{:k($[10])}', '…as `%h<k> = [10]` does';

# the Hash::Ordered shape, without the module: values kept through ASSIGN-POS
# and handed out as Pairs, filled two ways
class Ordered {
    has @!keys; has Mu @!values;
    method set($k, \v) { my $i = @!keys.elems; @!keys.push($k); @!values.ASSIGN-POS($i, v) }
    method raku() { 'Ordered(' ~ @!keys.kv.map(-> $i, $k { Pair.new($k, @!values.AT-POS($i)).raku }).join(',') ~ ')' }
}
my $direct = Ordered.new; $direct.set('a', [10]);
my Mu $value = [10];
my $via-scalar = Ordered.new; $via-scalar.set('a', $value);
is $direct.raku, $via-scalar.raku, 'filled from a value or from a $ variable, the same';
