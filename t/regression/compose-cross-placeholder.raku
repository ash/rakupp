# Regression: zef-bar campaign batch 3 — five general fixes from reducing
# Digest::MD5's constant tables (its suite now runs with correct BEGIN tables;
# one padding-pipeline layer remains, tracked in the campaign notes).
#
# 1. `∘` (and ASCII `o`) sat at multiplicative precedence, so
#    `&floor ∘ * * 2**32 ∘ &abs` composed a NUMBER; it is STRUCTURAL in Raku
#    (looser than all arithmetic) — `&abs ∘ * - 5` takes the curried block.
# 2. `X[R%]` — a bracketed cross-metaop with an R-REVERSED base — fell out of
#    the bracket parse (only a lone op token was accepted) and produced
#    garbage; MD5 builds its index table with `16 X[R%] ...`.
# 3. `LIST Xxx n` replicated evaluated VALUES; xx's left is THUNKY, so each
#    replication re-evaluates the lhs (advancing anonymous $++ states), and
#    replications group element-major.
# 4. Postfix ++/-- on an UNDEFINED numeric returned Any; S03 says the type's
#    zero (`my $x; $x++` is 0, and $x becomes 1).
# 5. `$^b` and a later bare `$b` in the same block were TWO slots holding
#    copies — Buf/Blob values live by value, so MD5's padding block pushed
#    into one copy and read the other. One slot now (the bare name), with
#    caret/colon spellings resolving to it.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. composition precedence
@fail.push('compose-prec') unless (&abs ∘ * - 5)(3) == 2;
@fail.push('compose-chain') unless (&floor ∘ * * 4 ∘ &abs)(-2.3) == 9;
my @sine = (blob32.new: map &floor ∘ * * 2**32 ∘ &abs ∘ &sin ∘ * + 1, ^4).list;
@fail.push("sine: {@sine[0].base(16)}") unless @sine[0] == 0xD76AA478;

# 2. R-reversed bracketed metaop
@fail.push('X[R%]') unless (16 X[R%] (3, 20, 35)).flat.join(',') eq '3,4,3';

# 3. thunky Xxx, element-major grouping
my @g = (1, $++) Xxx 3;
@fail.push("Xxx: {@g.raku}") unless @g[0].join(',') eq '1,1,1' && @g[1].join(',') eq '0,1,2';
my @tbl = (Blob.new: 16 X[R%] flat ($++, 5*$++ + 1, 3*$++ + 5, 7*$++) Xxx 16).list;
@fail.push("md5-table: {@tbl[^4].join(',')}") unless @tbl[^8].join(',') eq '0,1,2,3,4,5,6,7';

# 4. postfix on undefined
my $u;
@fail.push('postinc-return') unless $u++ == 0;
@fail.push('postinc-store') unless $u == 1;
my $d;
@fail.push('postdec') unless $d-- == 0 && $d == -1;

# 5. placeholder caret/bare unity (by-value Buf mutation visible both ways)
@fail.push('ph-unity') unless { $^b.push(5); $b.elems }(buf8.new) == 1;
@fail.push('ph-basic') unless { $^a + $^b }(40, 2) == 42;
@fail.push('ph-named') unless { $:n * 2 }(n => 21) == 42;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
