# Regression:
#   1. Statement modifiers inside a sigil contextualizer — `@(EXPR for LIST)`,
#      `@(EXPR if COND)`, `$(EXPR for LIST)` — used to be a parse error
#      ("expected ) got 'for'"). JSON::Unmarshal's `@(_unmarshal($_, …) for @data)`
#      needs it.
#   2. `dir()` yields IO::Path entries (not Str), matching Rakudo — File::Find's
#      `checkrules(IO::Path:D $io, …)` and any `.d`/`.IO` on the result need it.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. contextualizer statement modifiers
@fail.push("at-for (@(($_*2 for 1..3)).join(','))")
    unless @($_ * 2 for 1..3) eqv [2, 4, 6];
@fail.push('at-if-true')  unless @(42 if True).elems == 1;
@fail.push('at-if-false') unless @(42 if False).elems == 0;
@fail.push('dollar-for')  unless $($_ + 1 for 1..3).List eqv (2, 3, 4);

# 2. dir() returns IO::Path
@fail.push('dir-iopath') unless dir('.').head ~~ IO::Path;
# a :test matcher filters basenames
@fail.push('dir-test') unless dir('.', test => /'.'/).elems > 0;

# 3. File::Find works on top of dir() (finds a known file in src/)
# Guarded: File::Find is an ecosystem module; skip where it isn't installed (a
# clean CI runner), still fail if it's present but returns the wrong count.
{
    use File::Find;
    my $n = try { find(dir => 'src', name => 'Value.h').elems };
    with $n     { @fail.push("file-find ($n)") unless $n == 1 }
    else        { note '# File::Find not installed here — skipping find() check' }
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
