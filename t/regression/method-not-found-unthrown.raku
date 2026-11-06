# A method nothing takes (2026-11-06): methodCallUnresolved RETURNS its
# X::Method::NotFound, tagged, and methodCall throws it once the dispatch
# chain's giant frames have returned — the unwinder scans each frame's
# call-site table up to its call, twice, and with the throw inside
# methodCallInner (a 21 KB table) one miss retired 1.6M instructions. The
# callers that answer a miss — `.?`, `».?`, `.*`, a coercion's other routes,
# `$.name` in a class — get it back unthrown. Every answer here is Rakudo's,
# the exception's fields included, and so is every miss that must still die:
# one deeper inside a method that was found, `.+`. Over an Array or a Hash a
# hyper call stores a Nil answer as Any, as Rakudo's do; a List keeps it.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

class C {
    method calls-missing { self.nope-inside }
}
class B is Hash { }
class R { method n { Nil } }
class HB is Hash { method name { 'hb-name' }; method get { $.name }; method get2 { $.nope } }
class Fb { method FALLBACK($name, *@a) { "fb:$name" } }
class Foo { has $.v; method COERCE($x) { Foo.new(v => $x) } }
class Dies { method Foo { die "coercer refused" } }
sub takes-foo(Foo() $f) { $f.v }

try { Int.no-such-method }
check($!.^name, 'X::Method::NotFound', 'a miss throws X::Method::NotFound');
check($!.method, 'no-such-method', '.method');
check($!.typename, 'Int', '.typename');
check($!.private, False, '.private');
check($!.message, "No such method 'no-such-method' for invocant of type 'Int'", '.message');
check((try C.new.calls-missing) // 'died', 'died', 'a miss inside a found method dies');
check($!.method, 'nope-inside', '…naming the inner method');

check(Int.?no-such-method, Nil, '.? answers Nil for its own miss');
check((try C.new.?calls-missing) // 'died', 'died', '.? still dies for a deeper miss');
check(B.new.?nope, Nil, '.? on a class built on a Hash');
check(Fb.new.?anything, 'fb:anything', '.? reaches FALLBACK');
check(((C.new, 42)».?nope).raku, '(Nil, Nil)', '».? per element of a List');
check(([C.new, 42]».?nope).raku, '[Any, Any]', '…of an Array, which stores Nil as Any');
check(({a => C.new}».?nope).raku, '{:a(Any)}', '…of a Hash, likewise');
check(([R.new]».n).raku, '[Any]', 'a method answering Nil, over an Array');
check(((R.new,)».n).raku, '(Nil,)', '…and over a List');
check((1, 2)».?succ, (2, 3), '».? that finds the method');
check(Int.*no-such-method, (), '.* of a method nobody has');
check((try Int.+no-such-method) // 'died', 'died', '.+ of one dies');
check(takes-foo(5), 5, 'a coercion through COERCE');
check((try takes-foo(Dies.new)) // $!.message, 'coercer refused', 'a coercer that dies is the answer');
check(HB.new.get, 'hb-name', '`$.name` in a class built on a Hash');
check((try HB.new.get2) // $!.^name, 'X::Method::NotFound', '…and with no such method');
my $caught;
sub catcher { Int.no-such-method; CATCH { when X::Method::NotFound { $caught = .method } } }
catcher();
check($caught, 'no-such-method', 'CATCH sees it');
check((try 42.no-such-method) // $!.typename, 'Int', 'on an instance');

if @fail { note $_ for @fail; say "FAIL" } else { say "PASS" }
exit @fail ?? 1 !! 0;
