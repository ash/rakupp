# Regression: a DECLARED type survives compilation.
#
# Codegen emitted a bare Value::array()/makeHash()/any() for every declaration
# and dropped the declared type on the floor, so `my Int @a` compiled to an Array
# of Mu, `my Int $x` defaulted to (Any) instead of (Int), and `my %h{Int}` lost
# its key type entirely — which is why github.com/ash/rakupp/issues/9 still
# reproduced under `--exe` after the interpreter was fixed. Both sides go through
# the interpreter's own typedDefault now, via rtTypedDefault.
#
# Run under --exe as well as interpreted; the point is that they agree.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

my Int $x;
check($x.gist, '(Int)', 'a typed scalar defaults to its type object');
my Str $s;
check($s.WHAT.gist, '(Str)', 'and reports that type');

my Int @a;
@a.push(1);
check(@a.of.gist, '(Int)', 'a typed array keeps .of');

my Int %v;
%v<a> = 1;
check(%v.of.gist, '(Int)', 'a typed hash keeps .of');

my %h{Int};
my $k = "33";
%h{+$k} = 66;
check(%h.raku,  '(my Any %{Int} = 33 => 66)', 'an object hash keeps its key type');
check(%h.^name, 'Hash[Any,Int]',              'and its parameterized name');
check(%h.keys[0].WHAT.gist, '(Int)',          'and hands back a real Int key');

# a declaration inside a sub takes a different codegen path than a top-level one
sub inner()     { my %g{Int}; %g{7} = 'z'; %g.raku }
sub inner-arr() { my Int @b; @b.of.gist }
check(inner(),     '(my Any %{Int} = 7 => "z")', 'a declaration inside a sub too');
check(inner-arr(), '(Int)',                      'and a typed array inside a sub');

# untyped declarations must be unaffected
my ($p, $q);
check($p.WHAT.gist, '(Any)', 'an untyped declaration list still defaults to Any');
my %plain;
%plain<x> = 1;
check(%plain.raku,   '{:x(1)}', 'an untyped hash is unaffected');
check(%plain.of.gist, '(Mu)',   'and has no element type');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
