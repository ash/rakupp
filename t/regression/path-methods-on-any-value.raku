# Regression: IO::Path methods on values that are not paths (#275).
#
# The string-as-path arms stringified ANY invocant: `42.add('x')` was the
# path "42/x", `C.new.add('x')` a path named after the object's address, and
# `.open` on such a value opened whatever its string named. Rakudo has none
# of these methods on Int, Bool, List, Hash, Pair, code or a plain user
# object: X::Method::NotFound. A Str stays a path on purpose, and a class's
# own method of the same name is unaffected.
#
# Contract: exit 0 + last line PASS.
my @fail;
class C { }
class OwnAdd { method add($x) { "own $x" } }
my @values = 42, 1.5, True, $(1, 2), ${a => 1}, $(a => 1), C.new, &say;
for @values -> $v {
    my $type = $v.^name;
    for <add parent child sibling basename extension resolve relative is-absolute parts open> -> $m {
        my $died = False;
        try { $v."$m"('x'); CATCH { when X::Method::NotFound { $died = True } } }
        @fail.push("$type.$m did not die with X::Method::NotFound") unless $died;
    }
}
@fail.push('a Str is still a path') unless 'a'.IO.add('b') eq 'a/b';
@fail.push('a class keeps its own .add') unless OwnAdd.new.add('y') eq 'own y';
@fail.push('a real method still works') unless 42.chars == 2;

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
