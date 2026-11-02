# Regression: `@p.VAR.name` on a PARAMETER names the container bound to it.
#
# The arm that made `my @foo; @foo.VAR.name` answer "@foo" (sheet LA-21)
# answered every `@`/`%` variable's own name, parameters included. Rakudo
# names the CONTAINER: a parameter bound to the caller's `my @x` is "@x", and
# one bound to an anonymous `[]`, a default, a copy, a slurpy or nothing at
# all is "element". Text::CSV's CSV method decides between AoA and AoH output
# with `@kh.VAR.name ne "element"`, so a plain `csv(in => $file)` came back
# as hashes, its t/67_emptrow.t failed, and `rakupp install Text::CSV` would
# not install it. Its t/90_csv.t holds the other direction: `kh => my @kh`,
# through two `*%args` slurpies, must still not be "element".
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

my @x = 1, 2;
my %hh = a => 1;

# declared variables answer their own names, as before
check 'my @a',  do { my @a; @a.VAR.name }, '@a';
check 'my %h',  do { my %h; %h.VAR.name }, '%h';
check '@a.name without .VAR', do { my @a; @a.name }, '@a';
check '[]',     [].VAR.name, 'element';

# a parameter answers for what it was bound to
sub pos-p(@p)          { @p.VAR.name }
sub named-p(:@kh)      { @kh.VAR.name }
sub named-default(:@kh = [1]) { @kh.VAR.name }
sub copy-p(@p is copy) { @p.VAR.name }
sub slurpy-p(*@s)      { @s.VAR.name }
sub hash-p(%p)         { %p.VAR.name }
sub named-hash(:%h)    { %h.VAR.name }
sub bare-name(:@kh)    { @kh.name }

check 'positional <- @x',      pos-p(@x),          '@x';
check 'positional <- []',      pos-p([1]),         'element';
check 'named, not passed',     named-p(),          'element';
check 'named <- @x',           named-p(:kh(@x)),   '@x';
check 'named <- []',           named-p(:kh([])),   'element';
check 'named <- my @fresh',    named-p(kh => my @fresh), '@fresh';
check 'named with a default',  named-default(),    'element';
check 'is copy',               copy-p(@x),         'element';
check 'slurpy',                slurpy-p(1, 2),     'element';
check 'hash <- %hh',           hash-p(%hh),        '%hh';
check 'named hash, not passed', named-hash(),      'element';
check '.name, not passed',     bare-name(),        'element';

# through two slurpy hashes, as Text::CSV's csv() → .csv → .CSV does
class C {
    method inner(:@kh, *%args) { @kh.VAR.name }
    method outer(*%args)       { self.inner(|%args) }
}
sub front(*%args) { C.new.outer(|%args) }
check 'forwarded, not passed', front(),                    'element';
check 'forwarded <- my @kh',   front(kh => my @kh),        '@kh';
check 'forwarded, other args', front(headers => False),    'element';

# a nested call: the parameter of the inner sub still finds the outer `my`
sub hand-on(@q) { pos-p(@q) }
check 'handed on through a parameter', hand-on(@x), '@x';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
