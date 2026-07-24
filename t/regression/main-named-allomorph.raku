# Regression: a numeric-looking --named=value command-line argument must bind to a
# numeric-typed MAIN parameter (Real/Int/Numeric), just like a positional does.
# Rakudo val()-allomorphs every argv token; rakupp was allomorphing positionals
# but wrapping --opt=val values as a plain Str, so `sub MAIN(Real :$delay)` +
# `--delay=1` fell through to Usage (examples/life.raku --delay=1 reproduced it).
# Contract: exit 0 + last line PASS. Drives one nested rakupp MAIN with a mix of
# numeric and non-numeric named args.
my $rakupp = $*EXECUTABLE.absolute;

# One MAIN covering: Real<-int, Real<-rat, Int, Numeric, negative Int, Str kept,
# Bool flag. If any numeric arg failed to bind, MAIN wouldn't match → "Usage:".
my $prog = q:to/PROG/;
    sub MAIN(Real :$delay, Real :$rat, Int :$n, Numeric :$x, Int :$neg,
             Str :$name, Bool :$v) {
        say "delay=$delay rat=$rat n=$n x=$x neg=$neg name=$name v=$v";
    }
    PROG

my $p = run($rakupp, '-e', $prog,
            '--delay=1', '--rat=0.5', '--n=42', '--x=3.14',
            '--neg=-7', '--name=foo', '--v', :out, :err);
my $out = $p.out.slurp(:close);
my $err = $p.err.slurp(:close);

my @fail;
@fail.push("exit={$p.exitcode}") unless $p.exitcode == 0;
@fail.push("usage-fallthrough: $err") if $err.contains('Usage');
@fail.push("delay: $out")  unless $out.contains('delay=1 ');
@fail.push("rat: $out")    unless $out.contains('rat=0.5 ');
@fail.push("n: $out")      unless $out.contains('n=42 ');
@fail.push("x: $out")      unless $out.contains('x=3.14 ');
@fail.push("neg: $out")    unless $out.contains('neg=-7 ');
@fail.push("name: $out")   unless $out.contains('name=foo ');
@fail.push("bool: $out")   unless $out.contains('v=True');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
