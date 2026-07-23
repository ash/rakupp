# Regression: the Cro live-server cluster (batch 9) — fixes surfaced by running
# an actual Cro::HTTP::Router + Server app under rakupp.
#  1. `is export` subs inside a BRACED module publish even when the name
#     collides with a builtin (Router exports `get`/`put`/…).
#  2. A pointy block is a listop argument (`get -> { … }`) outside statement
#     conditions; `for @a -> $x { }` still binds the statement's own block.
#  3. Dynamic vars cross METHOD frames (route -> definition-complete -> plugin).
#  4. A public @./%. attr assigns through its accessor without `is rw`.
#  5. `only method` declarator; `state => v` is a pair, not a declaration;
#     `my (:@a, :@b) := %h` named destructuring.
#  6. Parameter introspection: .constraints (literal params), .type as a TYPE
#     OBJECT, .named_names.
my $ok = 0; my $n = 0;
sub ck($got, $want, $desc) {
    $n++;
    if $got eqv $want { $ok++ }
    else { say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# 2. pointy block as listop arg
multi taker(&handler, Str :$name --> Nil) { handler() }
my $ran = 0;
taker -> { $ran = 1 };
ck($ran, 1, 'listop with pointy-block argument');
my @collected;
for <a b> -> $x { @collected.push($x) }
ck(@collected.join(','), 'a,b', 'for -> $x block still owned by the statement');

# 3. dynamics through method frames
sub plugin-probe() { $*RSET // 'UNSET' }
class RS3 { method complete() { self!gen }; method !gen() { plugin-probe() } }
sub routish() { my $*RSET = 'the-set'; RS3.new.complete() }
ck(routish(), 'the-set', 'dynamic var visible through method frames');

# 4. accessor assignment on @-attrs without is rw (Cro's exact shape:
# `.body-parsers = @!body-parsers` — an array RHS)
class H4 { has @.body-parsers; has $.x }
my $h = H4.new;
my @src = 1, 2, 3;
$h.body-parsers = @src;
ck($h.body-parsers.elems, 3, 'public @.attr assigns through accessor');
my $died = False;
try { $h.x = 5; CATCH { default { $died = True } } }
ck($died, True, 'public $.attr without is rw still refuses');

# 5a. only method
class C5 { only method new() { self.bless } }
ck(C5.new.^name, 'C5', 'only method declarator');
# 5b. state as pair key
class S5 { has $.state; has $.sid }
ck(S5.new(sid => 1, state => 'hi').state, 'hi', 'state => v is a pair');
# 5c. named destructuring
my @things = 1, 'a', 2, 'b';
my (:@odd, :@even) := @things.classify: { $_ ~~ Int ?? 'even' !! 'odd' };
ck(@even.join(','), '1,2', 'named destructuring := classify (even)');
ck(@odd.join(','), 'a,b', 'named destructuring := classify (odd)');

# 6. Parameter introspection
my &r6 = sub ('greet', $name, Int :$port) { };
my @params = &r6.signature.params;
ck(@params[0].constraints, 'greet', 'literal param .constraints');
ck((@params[1].type =:= Any), True, 'untyped param .type is Any');
ck((@params[2].type =:= Int), True, 'typed named param .type object');
ck(@params[2].named_names.join(','), 'port', '.named_names');

say $ok == $n ?? 'PASS' !! "FAIL ($ok/$n)";
