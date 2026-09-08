# `qw:v[…]` allomorphs its words; the plain q-family does not.
#
# The q-family was made to stop allomorphing (issue #69: `qw<8 9 10>` handed
# Crane's `in` Ints where Rakudo hands it Strs, so a step read as a positional
# index and built a 10-element Array instead of nesting three hash keys). That
# is right for every implicit spelling and wrong for the one EXPLICIT one:
# Rakudo honours `:v`/`:val`, and S02-literals/allomorphic.t asserts it word by
# word. The form travelled lexer -> parser as "qw"/"qww"/"qqw"/"qqww", naming
# the split and the interpolation with nowhere to record an adverb, so `:v` was
# dropped on the way. allomorphic.t went 87 -> 108 of 119 when it stopped being.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

check(qw:v[1 2/3 4.5 abc].map(*.^name).join(' '), 'IntStr RatStr RatStr Str', 'qw:v allomorphs');
check(qw:val[7 x].map(*.^name).join(' '),         'IntStr Str',               'qw:val is the same adverb');
check(qw[1 2/3].map(*.^name).join(' '),           'Str Str',                  'plain qw stays Str (#69 stands)');
check(qww[1 2].map(*.^name).join(' '),            'Str Str',                  'plain qww stays Str');
check(<1>.^name,                                  'IntStr',                   'a bare angle list still allomorphs');
check(qqww:v[1 'a b'].map(*.^name).join(' '),     'IntStr Str',               ':v composes with ww protection');
check(qqww:v[1 'a b'][1],                         'a b',                      '…and ww still groups the quoted span');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
