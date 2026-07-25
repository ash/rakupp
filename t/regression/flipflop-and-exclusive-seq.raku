# Regression: the operator behaviour matrix's biggest divergence clusters
# (raku-spec DIVERGENCES.md, 192 -> 120).
#   1. Flip-flops answer Any while OFF and an Int RUN COUNTER while on — never a
#      Bool. `^` on either end excludes that edge element from the result (it
#      still counts). `fff` is sed-like: it doesn't test the RHS on the element
#      that switched it on.
#   2. `^...` / `^...^` — the sequence operator excluding its seed — didn't even
#      lex (the lexer took `^..` greedily and choked on the third dot).
#   3. `does` needs a role; `o`/`∘` need Callables. rakupp used to accept
#      `1 does 2` and `1 o 2`, which Rakudo rejects.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. flip-flops: Any while off, Int counter while on
sub ffrun(&f) { (1..8).map({ my $r = f($_); $r.defined ?? $r.Str !! '-' }).join(',') }
check(ffrun({ ($^n == 3) ff  ($^n == 5) }), '-,-,1,2,3,-,-,-', 'ff');
check(ffrun({ ($^n == 3) ff^ ($^n == 5) }), '-,-,1,2,-,-,-,-', 'ff-excl-last');
check(ffrun({ ($^n == 3) ^ff ($^n == 5) }), '-,-,-,2,3,-,-,-', 'ff-excl-first');
check(ffrun({ ($^n == 3) ^ff^ ($^n == 5) }), '-,-,-,2,-,-,-,-', 'ff-excl-both');
# a one-element run: `ff` tests the RHS on the same element, `fff` waits
check(ffrun({ ($^n == 3) ff  ($^n == 3) }), '-,-,1,-,-,-,-,-', 'ff-same-element');
check(ffrun({ ($^n == 3) fff ($^n == 3) }), '-,-,1,2,3,4,5,6', 'fff-sedlike');
# the type matters, not just the value
my $off = (False ff False);
@fail.push("off-is-{$off.^name}") unless $off.^name eq 'Any' && !$off.defined;
my $on = (True ff False);
@fail.push("on-is-{$on.^name}") unless $on.^name eq 'Int' && $on == 1;

# 2. the exclusive sequence operators
check((1 ... 5).join(','),   '1,2,3,4,5', 'seq');
check((1 ...^ 5).join(','),  '1,2,3,4',   'seq-excl-last');
check((1 ^... 5).join(','),  '2,3,4,5',   'seq-excl-first');
check((1 ^...^ 5).join(','), '2,3,4',     'seq-excl-both');
check(("a" ^... "e").join(','), 'b,c,d,e', 'seq-excl-first-str');
check((1 ^...^ 2).elems, '0', 'seq-excl-both-empty');
# `^..` (exclusive range) must still lex as itself
check((1 ^.. 5).join(','),  '2,3,4,5', 'excl-range');
check((1 ^..^ 5).join(','), '2,3,4',   'excl-range-both');

# 3. `does` wants a role, `o` wants Callables
sub rejects(&c) { (try { c(); 'accepted' }) // 'rejected' }
role Tagged { method tag { 'tag' } }
my $v = 1; $v does Tagged;
check($v.tag, 'tag', 'does-role-works');
check((5 but 'txt').Str, 'txt', 'but-value-works');
check(rejects({ my $z = 1; $z does 2 }),    'rejected', 'does-nonrole-rejected');
check(rejects({ my $z = 1; $z does "2" }),  'rejected', 'does-str-rejected');
check(((* + 1) o (* * 2))(5), '11', 'o-composes-callables');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
