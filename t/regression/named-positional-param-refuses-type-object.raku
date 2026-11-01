# Regression: a named `@`/`%` parameter refuses a TYPE OBJECT that is no
# Positional/Associative at dispatch (issue #77, FCO's `:@to!` item).
#
# The implicit Positional/Associative check on `:@x`/`:%x` skipped undefined
# values, so `:to(Str)` reached Red's `multi method inflate(Str $value,
# :@to!)` and died binding it ("expected Positional but got Str") instead of
# falling through to `inflate(Any $value, Any :$to)`. `:to(Array)` and
# `:to(Hash)` are type objects that DO conform, and still bind.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}

class Driver { multi method inflate(Any $value, Any :$to) { "any" } }
class SQLite is Driver { multi method inflate(Str $value, :@to!) { "json" } }
ck SQLite.new.inflate("x", :to(Str)), 'any', 'a Str type object passes over :@to!';
ck SQLite.new.inflate("x", :to(Array)), 'json', 'an Array type object binds it';
ck SQLite.new.inflate("x", :to([1, 2])), 'json', 'so does an Array';
ck SQLite.new.inflate("x"), 'any', 'and an absent one passes over it';

multi f(Any $v, Mu :$to) { 'any' }
multi f(Str $v, :@to!) { 'arr' }
ck f('x', :to(Any)), 'any', 'Any is no Positional';
ck f('x', :to(Positional)), 'arr', 'Positional is one';
ck f('x', :to(List)), 'arr', 'List is one';
ck f('x', :to(Hash)), 'any', 'Hash is not';

multi g(Any $v, Mu :$to) { 'any' }
multi g(Str $v, :%to!) { 'hash' }
ck g('x', :to(Str)), 'any', 'a Str type object passes over :%to!';
ck g('x', :to(Hash)), 'hash', 'a Hash type object binds it';
ck g('x', :to(Map)), 'hash', 'so does a Map type object';
ck g('x', :to(Array)), 'any', 'an Array type object does not';

say 'PASS';
