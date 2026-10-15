# Regression: a NAMED `$` parameter, and a `$` parameter's DEFAULT, did not
# itemize the Array or Hash they bound.
#
#   sub m(:$d) { my @y = $d; @y.elems }
#   m(d => [1, 2, 3])      # Rakudo: 1; rakupp: 3
#
# A `$` parameter is a scalar container: what it holds is one thing, so list
# assignment, `.flat`, `.append` and `.raku` all see an item. The positional
# arm of bindParams itemized; the named arm and evalDefault (a default,
# positional or named) bound the value bare. So `submethod BUILD(:$d) { @!d =
# $d }` spread a Hash Rakudo keeps whole, and `.raku` lost the `$(…)`.
#
# Itemizing the named values exposed what the positional arm already did
# wrong: a `%` parameter kept the item it was handed (`sub f(%h) { my @y = %h
# }` given a `$h` got one element). Binding to `%` drops it now, as `@` did.
#
# Found after hash-into-array-attribute.raku. Every expectation was checked
# against Rakudo 2026.09.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — got {$got.raku} want {$want.raku}" }
}

sub n(:$d)                     { my @y = $d; @y.elems }
sub rn(:d($x))                 { my @y = $x; @y.elems }
sub al(:a(:$d))                { my @y = $d; @y.elems }
sub ty(Array :$d)              { my @y = $d; @y.elems }
sub rq(:$d!)                   { my @y = $d; @y.elems }
sub cp(:$d is copy)            { my @y = $d; @y.elems }
sub rw(:$d is raw)             { my @y = $d; @y.elems }
sub nd(:$d = [1, 2, 3])        { my @y = $d; @y.elems }
sub ndh(:$d = {a => 1, b => 2}) { my @y = $d; @y.elems }
sub pd($d = [1, 2, 3])         { my @y = $d; @y.elems }
sub pl($d = (1, 2, 3))         { my @y = $d; @y.elems }
sub ps($d)                     { my @y = $d; @y.elems }

# --- a named `$` holds one item ---------------------------------------------------
ck n(d => [1, 2, 3]), 1, 'an Array';
ck n(d => {a => 1, b => 2}), 1, 'a Hash';
ck n(d => (1, 2, 3)), 1, 'a List';
{
    my @arr = 1, 2, 3;
    my %h = a => 1, b => 2;
    ck n(d => @arr), 1, 'an @-variable';
    ck n(:d(@arr)), 1, 'an @-variable, colon form';
    ck n(d => %h), 1, 'a %-variable';
}
ck rn(d => [1, 2, 3]), 1, 'a renamed named parameter';
ck al(a => [1, 2, 3]), 1, 'an aliased named parameter';
ck ty(d => [1, 2, 3]), 1, 'a typed named parameter';
ck rq(d => [1, 2, 3]), 1, 'a required named parameter';
ck cp(d => [1, 2, 3]), 1, 'an `is copy` named parameter';
ck rw(d => [1, 2, 3]), 3, '…but `is raw` binds the Array itself';
ck (-> :$d { my @y = $d; @y.elems })(d => [1, 2, 3]), 1, "a pointy block's named parameter";
{
    my class C { method m(:$d) { my @y = $d; @y.elems } }
    ck C.m(d => [1, 2, 3]), 1, "a method's named parameter";
}

# --- …and so does a `$` parameter's default --------------------------------------
ck nd(), 1, 'a named default Array';
ck nd(d => [4, 5]), 1, '…and the argument that replaces it';
ck ndh(), 1, 'a named default Hash';
ck pd(), 1, 'a positional default Array';
ck pl(), 1, 'a positional default List';
ck ps([1, 2, 3]), 1, 'a positional argument (always was)';

# --- what an item does elsewhere ---------------------------------------------------
ck (sub (:$d) { $d.raku })(d => [1, 2]), '$[1, 2]', '.raku shows the item';
ck (sub (:$d) { $d.raku })(d => {a => 1}), '${:a(1)}', '…of a Hash too';
ck (sub (:$d) { (1, $d).flat.elems })(d => [1, 2, 3]), 2, '.flat leaves it whole';
ck (sub (:$d) { my @y; @y.append: $d; @y.elems })(d => [1, 2, 3]), 1, '.append takes it as one';
ck (sub (:$d) { my @y = |$d; @y.elems })(d => [1, 2, 3]), 3, '…while |$d still slips';
ck (sub (:$d) { my @y = @$d; @y.elems })(d => [1, 2, 3]), 3, '…and @$d still spreads';
ck (sub (:$d) { my $n = 0; $n++ for $d; $n })(d => [1, 2, 3]), 1, 'for $d is one iteration';
{
    my @src = 1, 2;
    (sub (:$d) { $d.push: 9 })(d => @src);
    ck @src, [1, 2, 9], 'a push through it still reaches the caller';
}

# --- a construction-time BUILD ----------------------------------------------------
{
    my class B { has @.d; submethod BUILD(:$d) { @!d = $d } }
    ck B.new(d => {a => 1}).d, [{a => 1},], 'BUILD(:$d) then @!d = $d keeps a Hash whole';
    ck B.new(d => [1, 2]).d, [[1, 2],], '…and an Array';
}

# --- a `%` or `@` parameter binds what it is handed, item or not -----------------
sub ih(%h)  { my @y = %h; @y.elems }
sub ia(@a)  { my @y = @a; @y.elems }
sub inh(:%h) { my @y = %h; @y.elems }
sub ina(:@a) { my @y = @a; @y.elems }
sub fwd-p($h, $a)   { ih($h), ia($a), inh(:$h), ina(a => $a) }
sub fwd-n(:$h, :$a) { ih($h), ia($a), inh(:$h), ina(:$a) }
sub fwd-d(:$h = {x => 1, y => 2}, :$a = [1, 2, 3]) { ih($h), ia($a), inh(:$h), ina(:$a) }
ck fwd-p({x => 1, y => 2}, [1, 2, 3]), (2, 3, 2, 3), 'positional $ forwarded into %/@ parameters';
ck fwd-n(h => {x => 1, y => 2}, a => [1, 2, 3]), (2, 3, 2, 3), 'named $ forwarded into %/@ parameters';
ck fwd-d(), (2, 3, 2, 3), '$ defaults forwarded into %/@ parameters';

say $fails == 0 ?? 'PASS' !! 'FAIL';
exit $fails == 0 ?? 0 !! 1;
