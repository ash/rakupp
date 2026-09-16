# Regression: `$obj."$name"() = $value` — a dynamic method call as an
# ASSIGNMENT TARGET.
#
# The name lives in the node's methodExpr, so the static `method` field is
# EMPTY, and every test along the lvalue path compared against "": no
# attribute matched, the write fell through to an attribute named "" and the
# assignment SILENTLY did nothing. The sigil lookup that decides whether a
# `@`/`%` attribute takes a container assignment asked the same empty name, so
# a list stored as one itemised value.
#
# HTTP::Cookies' grammar actions are written exactly this way —
# `$h."{$a<name>.lc}"() = ~$a<value>` for expires, path and domain — so every
# parsed cookie came back carrying only its name and its boolean flags.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

class C {
    has $.path is rw;
    has @.list is rw;
    has %.map  is rw;
    has Int $.typed is rw;
    has $.ro;
    method calc() is rw { $!path }
}

# Every spelling of the name.
my $a = C.new; $a."path"() = '/literal';
check $a.path, '/literal', 'a literal dynamic name assigns';
my $b = C.new; my $n = 'path'; $b."$n"() = '/var';
check $b.path, '/var', 'a variable one does too';
my $c = C.new; my $u = 'PATH'; $c."{$u.lc}"() = '/block';
check $c.path, '/block', 'and a computed one';
my $d = C.new; my @names = <path>; $d."@names[0]"() = '/idx';
check $d.path, '/idx', 'and one from a list';

# A `@` or `%` attribute takes a CONTAINER assignment, exactly as the direct
# spelling does — this is what the sigil lookup decides.
my $e = C.new; $e."list"() = (1, 2);
check $e.list.List, (1, 2), 'a @ attribute takes the list, not one item';
my $f = C.new; $f.list = (1, 2);
check $f.list.raku, $e.list.raku, 'and matches the direct spelling exactly';
my $g = C.new; $g."map"() = (a => 1, b => 2);
check $g.map<a>, 1, 'a % attribute becomes a hash';
check $g.map.elems, 2, 'with every pair in it';

# An `is rw` METHOD is reachable the same way.
my $h = C.new; $h."calc"() = '/method';
check $h.path, '/method', 'an is-rw method is assignable dynamically';

# The declared type is still enforced, and a read-only attribute still refuses.
my $i = C.new; $i."typed"() = 7;
check $i.typed, 7, 'a typed attribute takes a good value';
check (try { my $x = C.new; $x."typed"() = 'nope'; 'no throw' } // 'threw'), 'threw',
      'and refuses a bad one';
check (try { my $x = C.new; $x."ro"() = 5; 'no throw' } // 'threw'), 'threw',
      'a read-only attribute still refuses';

# The name expression runs exactly ONCE — the sigil lookup and the lvalue both
# need it, and it is an arbitrary expression.
my $calls = 0;
sub nm() { $calls++; 'path' }
my $j = C.new; $j."{nm()}"() = '/once';
check $j.path, '/once', 'a name with a side effect still assigns';
check $calls, 1, 'and is evaluated once, not twice';
$calls = 0;
my $k = C.new; $k."{nm()}"() = '/again';
check $calls, 1, 'once for a scalar attribute';
$calls = 0;
sub lnm() { $calls++; 'list' }
my $l = C.new; $l."{lnm()}"() = (1, 2);
check $calls, 1, 'and once for a list one, which needs the sigil first';

# Reading dynamically was never broken and must stay so.
my $m = C.new; $m.path = '/read';
check $m."path"(), '/read', 'a dynamic call still reads';

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
