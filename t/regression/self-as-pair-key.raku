# BROKE: leg 18 — the new X::Syntax::Self::WithoutObject runtime check threw
# for `(self => 1)`, where `self` is an AUTOQUOTED pair key, killing the
# EVAL loop in S02-literals/pairs.t (9 passes lost, caught by gate dg1).
# FIXED: same leg — a `self` term followed by => stays a bareword key.
my %h = (self => 1);
die "self as pair key broken: {%h.keys.raku}" unless %h<self> == 1;
say 'PASS';
