# Regression: what `%h.push`/`.append` accept, and the `$` marker on a hash value.
#   * the arguments are flattened into the PAIRS they contribute first. A list, a
#     Seq or a Hash argument contributes its own pairs — `(my %inv).push: %wc.invert`
#     was dropping every one of them and leaving an empty hash.
#   * a NAMED argument contributes nothing: `%h.push(e => 6)` is a bareword
#     fat-arrow, which binds as a named, so it is a no-op rather than an element.
#     Same for `%h.push(:c(2))`.
#   * on a NEW key both push and append store the value as it is; only a LIST value
#     spreads. It is the EXISTING-key branch that tells the two apart. `.append`
#     was wrapping a scalar in an array on a new key.
#   * every hash VALUE sits in a Scalar container, so an Array/Hash/List stored
#     there renders with the `$` itemisation marker. `%h<k> = [1,2]` set the flag
#     and `my %h = k => [1,2]` never did, so the two printed differently.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# arguments are flattened into pairs
my %wc = 'hash' => 323, 'pair' => 322, 'pipe' => 323;
(my %inv).push: %wc.invert;
check(%inv.keys.sort.gist,   '(322 323)',    'a Hash argument contributes its pairs');
check(%inv{322},             'pair',         'the single-value key');
check(%inv{323}.sort.gist,   '(hash pipe)',  'and the collided one collects both');
my %l; %l.push: ('a' => 1, 'b' => 2);
check(%l.raku, '{:a(1), :b(2)}', 'and so does a list of pairs');

# a named argument is a no-op
my %n .= push(e => 6);
check(%n.raku, '{}', 'a bareword fat-arrow binds as a named and is dropped');
my %n2; %n2.push(:c(2));
check(%n2.raku, '{}', 'and so is a colon-pair');
my %n3; %n3.push('c' => 2);
check(%n3.raku, '{:c(2)}', "but a quoted key is positional and lands");

# new key vs existing key
my %f; %f.append('c' => 2);
check(%f.raku, '{:c(2)}', 'append stores a scalar as itself on a new key');
my %e; %e.append('x' => [1, 2]);
check(%e.raku, '{:x($[1, 2])}', 'and spreads a list value');
my %g = a => 1; %g.push('a' => 2);
check(%g.raku, '{:a($[1, 2])}', 'push on an existing key makes a list');
my %k = a => 1; %k.append('a' => 2);
check(%k.raku, '{:a($[1, 2])}', 'append likewise');

# the itemisation marker on a hash value
my %s1; %s1<k> = [1, 2];
my %s2 = k => [1, 2];
check(%s1.raku, '{:k($[1, 2])}', 'assigned through a subscript');
check(%s2.raku, '{:k($[1, 2])}', 'and built from a pair — the same rendering');
my %lv = k => (1, 2);
check(%lv.raku, '{:k($(1, 2))}', 'a List value is marked too');
check({a => 1, b => 2}.raku,   '{:a(1), :b(2)}', 'a scalar value is not');
check({a => {b => 1}}.raku,    '{:a(${:b(1)})}', 'a nested Hash is');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
