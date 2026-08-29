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
    unless @($_ * 2 for 1..3).Array eqv [2, 4, 6];  # .Array: eqv is type-aware
@fail.push('at-if-true')  unless @(42 if True).elems == 1;
@fail.push('at-if-false') unless @(42 if False).elems == 0;
@fail.push('dollar-for')  unless $($_ + 1 for 1..3).List eqv (2, 3, 4);

# 2. dir() returns IO::Path
@fail.push('dir-iopath') unless dir('.').head ~~ IO::Path;
# a :test matcher filters basenames
@fail.push('dir-test') unless dir('.', test => /'.'/).elems > 0;

# 3. File::Find works on top of dir() (finds a known file in src/)
# Guarded with a RUNTIME require: File::Find is an ecosystem module, absent on a
# clean CI runner. The old guard put `use` inside the try, which stopped working
# when a failed `use` became a compile-time error — the file then died before
# the try existed, on CI only (the module IS installed on the dev machine, which
# is exactly how the gap hid locally). `require` fails at run time, catchably;
# the rest of this file's checks keep running everywhere either way.
# The directory is derived from $?FILE, not written as the relative 'src' this
# check used to pass. A bare relative path made the whole file depend on being
# run from the repo root, silently: `t/run.raku` says nothing about a working
# directory, every other path in this file is either '.' or absolute, and the
# only symptom was `file-find (0)` — a count, with nothing to say the directory
# had not been looked at. Found when a release harness ran the suite from its
# own scratch directory and one check of 580 went red.
{
    my $src = $?FILE.IO.parent.parent.parent.add('src').Str;   # t/regression/… -> repo root
    my $n = try { require File::Find; find(dir => $src, name => 'Value.h').elems };
    with $n     { @fail.push("file-find ($n) in $src") unless $n == 1 }
    else        { note '# File::Find not installed here — skipping find() check' }
}

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
