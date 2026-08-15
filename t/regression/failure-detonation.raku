# Regression: an unhandled Failure must throw when its VALUE IS USED, and stay
# quiet when something merely asks ABOUT it. We only detonated on stringification
# (.Str/.gist/interpolation), so arithmetic computed with the Failure as though it
# were a number — and `.Int` returned the tagged Hash's ELEMENT COUNT, a 2 out of
# nowhere. A divide-by-zero could travel arbitrarily far as a plausible small
# integer before anything noticed.
#
# The sharp edge is the quiet set. `$f ~~ Failure` is how you TEST for one and
# `// || orelse` are how you supply a default, so none of those may detonate —
# and neither may the NEGATED identity forms: S10-packages/scope.t compares a
# missing symbol's Failure with `!===` and expects an answer, not a bang. That
# one only showed up because the file DIED early, dropping its own denominator,
# which the fully-passing-file gate cannot see.
#
# Note each case builds a FRESH Failure: the first thing to touch one marks it
# handled, so a shared one would answer differently second time round.
# Contract: exit 0 + last line PASS.
my @fail;

sub detonates(&c) {
    my $threw = False;
    try { c(); CATCH { when X::Numeric::DivideByZero { $threw = True } } }
    $threw;
}

# ---- using the value detonates --------------------------------------------
@fail.push('+')      unless detonates { (10 div 0) + 1 };
@fail.push('*')      unless detonates { (10 div 0) * 2 };
@fail.push('-')      unless detonates { (10 div 0) - 1 };
@fail.push('~')      unless detonates { (10 div 0) ~ 'x' };
@fail.push('==')     unless detonates { (10 div 0) == 1 };
@fail.push('<')      unless detonates { (10 div 0) < 1 };
@fail.push('interp') unless detonates { my $f = 10 div 0; "$f" };
@fail.push('.Str')   unless detonates { (10 div 0).Str };
@fail.push('.gist')  unless detonates { (10 div 0).gist };
@fail.push('.Int')   unless detonates { (10 div 0).Int };
@fail.push('.Num')   unless detonates { (10 div 0).Num };
@fail.push('.Numeric') unless detonates { (10 div 0).Numeric };

# ---- asking about it stays quiet ------------------------------------------
@fail.push('//')       if detonates { (10 div 0) // 5 };
@fail.push('||')       if detonates { (10 div 0) || 5 };
@fail.push('orelse')   if detonates { (10 div 0) orelse 5 };
@fail.push('.defined') if detonates { (10 div 0).defined };
@fail.push('so')       if detonates { so (10 div 0) };
@fail.push('.Bool')    if detonates { (10 div 0).Bool };
@fail.push('.handled') if detonates { (10 div 0).handled };
@fail.push('.exception') if detonates { (10 div 0).exception };
@fail.push('~~')       if detonates { (10 div 0) ~~ Failure };
@fail.push('!===')     if detonates { 1 !=== (10 div 0) };
@fail.push('===')      if detonates { 1 === (10 div 0) };

# …and the values those quiet forms give
@fail.push("// value ({(10 div 0) // 5})")   unless (10 div 0) // 5 == 5;
@fail.push('defined')  if (10 div 0).defined;
@fail.push('is Failure') unless (10 div 0) ~~ Failure;
@fail.push('exception type') unless (10 div 0).exception ~~ X::Numeric::DivideByZero;

# a symbolic lookup of a missing name is a Failure too, and comparing it by
# identity must answer rather than explode
my $missing = try { ::('No::Such::Symbol::Here') };
@fail.push('missing !===') unless (1 !=== $missing);

# ---- handled-ness: the first touch disarms it ------------------------------
my $f = 10 div 0;
$f.defined;                       # asking marks it handled
my $after = 'threw';
try { $after = ($f + 1).^name; CATCH { default { $after = 'threw' } } }
@fail.push("handled then used ($after)") if $after eq 'threw';

# ordinary arithmetic is untouched (the guard sits after the Int/Int fast path)
@fail.push('plain arith') unless 2 + 3 == 5 && 7 * 6 == 42 && 10 / 4 == 2.5;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
