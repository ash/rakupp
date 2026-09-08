# `qw:v[…]` allomorphs its words; the plain q-family does not.
#
# The q-family was made to stop allomorphing (issue #69: `qw<8 9 10>` handed
# Crane's `in` Ints where Rakudo hands it Strs, so a step read as a positional
# index and built a 10-element Array instead of nesting three hash keys). That
# is right for every implicit spelling and wrong for the one EXPLICIT one:
# Rakudo honours `:v`/`:val`, and S02-literals/allomorphic.t asserts it word by
# word. The form travelled from lexer to parser as "qw"/"qww"/"qqw"/"qqww",
# which had nowhere to record the adverb, so it was dropped.
my @v = qw:v[1 2/3 4.5 abc];
say @v.map(*.^name).join(' ') eq 'IntStr RatStr RatStr Str'
    ?? 'ok qw:v allomorphs' !! "NOT OK: {@v.map(*.^name).join(' ')}";

# :val spells the same adverb
my @w = qw:val[7 x];
say @w.map(*.^name).join(' ') eq 'IntStr Str'
    ?? 'ok qw:val allomorphs' !! "NOT OK: {@w.map(*.^name).join(' ')}";

# …and the plain forms still do NOT allomorph — the #69 fix stands
my @p = qw[1 2/3];
say @p.map(*.^name).join(' ') eq 'Str Str'
    ?? 'ok plain qw stays Str' !! "NOT OK: {@p.map(*.^name).join(' ')}";

# a bare angle list is unaffected either way
say <1>.^name eq 'IntStr' ?? 'ok <> still allomorphs' !! "NOT OK: {<1>.^name}";

# :v composes with the ww quote-protection form
my @q = qqww:v[1 'a b'];
say @q.map(*.^name).join(' ') eq 'IntStr Str' && @q[1] eq 'a b'
    ?? 'ok qqww:v protects and allomorphs' !! "NOT OK: {@q.map(*.^name).join(' ')} / {@q[1]}";

say 'PASS';
