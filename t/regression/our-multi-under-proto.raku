# BROKE: leg 18 — X::Declaration::Scope::Multi rejected every `our multi`,
# but candidates under an our-scoped proto are legal Raku
# (S06-multi/type-based.t 32/48 -> GONE at parse, caught by gate dg1).
# FIXED: same leg — a parser-side ourProtos_ set exempts them.
class A {
    our proto sub a(|) { * }
    our multi sub a(Int $x) { 'Int ' ~ $x }
    our multi sub a(Str $x) { 'Str ' ~ $x }
    method go { a(42) ~ '/' ~ a('x') }
}
die 'our multi under our proto broken' unless A.go eq 'Int 42/Str x';
say 'PASS';
