# Any's mutators and `.sum` on a plain object answer as Rakudo's do: `.push`,
# `.append`, `.unshift` and `.prepend` are X::Multi::NoMatch, "Cannot resolve
# caller push(P:D: Int:D); none of these signatures matches:", `.splice` names
# a proto with no candidates, and `.sum` is the object's `.Numeric`, refused
# for want of one. Each died "No such method 'push'" here, and so for a Block
# and a Regex; an object with a FALLBACK reached it.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub msg(&c) { try c(); "{$!.^name}: {$!.message}" }
my $any = "none of these signatures matches:\n    (Any:U \\SELF:: |values)";

class P { }
ck msg({ P.new.push(1, "a") }), "X::Multi::NoMatch: Cannot resolve caller push(P:D: Int:D, Str:D); $any", '.push';
ck msg({ P.new.append([1, 2]) }), "X::Multi::NoMatch: Cannot resolve caller append(P:D: Array:D); $any", '.append';
ck msg({ P.new.unshift }), "X::Multi::NoMatch: Cannot resolve caller unshift(P:D: ); $any", '.unshift';
ck msg({ P.new.prepend(Int, :a(2)) }), "X::Multi::NoMatch: Cannot resolve caller prepend(P:D: Int:U, :a(Int)); $any", '.prepend';
ck msg({ P.new.splice(1, "a") }),
   'X::Multi::NoMatch: Cannot resolve caller splice(P:D, Int:D, Str:D); Routine does not have any candidates.  Is only the proto defined?', '.splice';
ck msg({ P.new.sum }), "X::Multi::NoMatch: Cannot resolve caller Numeric(P:D: ); none of these signatures matches:\n    (Mu:U \\v:: *%_)", '.sum';
ck msg({ P.new.sum(1) }), 'X::AdHoc: Too many positionals passed; expected 1 argument but got 2', '.sum with an argument';
ck msg({ { 1 }.push(2) }), "X::Multi::NoMatch: Cannot resolve caller push(Block:D: Int:D); $any", 'a Block';
ck msg({ /a/.sum }).lines[0], 'X::Multi::NoMatch: Cannot resolve caller Numeric(Regex:D: ); none of these signatures matches:', 'a Regex';
class F { method FALLBACK($n, *@a) { "fb $n" } }
ck msg({ F.new.push(1) }), "X::Multi::NoMatch: Cannot resolve caller push(F:D: Int:D); $any", 'past a FALLBACK';

class Q { method Numeric { 5 } }
class H { method push($x) { "own $x" } }
ck (Q.new.sum, H.new.push(3), F.new.zork), (5, 'own 3', 'fb zork'), 'its own methods, as before';

say 'PASS';
