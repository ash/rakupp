# Regression: IO::Path methods on undefined invocants (#275, second half).
#
# The #275 fix refused the path methods on DEFINED non-paths (42.add), but an
# undefined invocant still read as the path "": `Any.add('x')` was "/x" and
# `Int.parent` was ".". Rakudo: X::Method::NotFound on any type object, Nil on
# Nil, and X::Parameter::InvalidConcreteness on the IO::Path type object.
#
# Contract: exit 0 + last line PASS.
my @fail;
class C { }
class OwnAdd { method add($x) { "own $x" } }
my @methods = <add parent child sibling basename extension resolve relative absolute
               is-absolute is-relative parts dirname volume cleanup open>;
for Any, Mu, Int, Str, Cool, C -> $t {
    for @methods -> $m {
        my $died = False;
        try { $t."$m"('x'); CATCH { when X::Method::NotFound { $died = True }; default { } } }
        @fail.push("{$t.^name}.$m did not die with X::Method::NotFound") unless $died;
    }
}
for (add => (q{x},), child => (q{x},), open => ()) -> (:key($m), :value(@a)) {
    @fail.push("Nil.$m is not Nil") unless Nil."$m"(|@a) === Nil;
    my $died = False;
    try { IO::Path."$m"(|@a); CATCH { when X::Parameter::InvalidConcreteness { $died = True }; default { } } }
    @fail.push("IO::Path.$m did not die with X::Parameter::InvalidConcreteness") unless $died;
}
@fail.push('a Str is still a path') unless 'a'.IO.add('b') eq 'a/b';
@fail.push('a type object keeps its own .add') unless OwnAdd.add('y') eq 'own y';
@fail.push('IO::Spec::Unix.basename') unless IO::Spec::Unix.basename('a/b') eq 'b';
@fail.push('IO::Spec::Unix.is-absolute') unless IO::Spec::Unix.is-absolute('/a');

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
