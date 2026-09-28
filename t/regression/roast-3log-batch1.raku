# Regression: the first batch from 3.log (a `run-roast.raku --failed` list).
# Each line was checked under Rakudo 2026.08 as well.
#   - text reads fold CRLF: a Proc's captured pipes as files do; `run(:bin)`
#     pipes are Bufs (S16-io/eof.t)
#   - `.reduce` with an `is raw` accumulator folds containers (S32-list/reduce.t)
#   - `for` over a block ending on a variable iterates that container
#     (S04-statements/when.t)
#   - a caught exception's backtrace lists the setting's throw/die and every
#     bare block (S32-exceptions/misc.t)
#   - `$t += $d` keeps a Duration (integration/advent2011-day07.t)
#   - `Pair.freeze` keeps the Pair's identity, a bare `fail` uses the routine's
#     own `$!`, `'-'.IO` is standard input (6.c A03-older-specs/01-misc.t)
# Contract: exit 0 + last line PASS.
my @fail;

# CRLF folding on a captured pipe, and :bin
{
    my $p = run $*EXECUTABLE, '-e', 'print "a\r\nb\r\n"', :out;
    my $s = $p.out.slurp(:close);
    @fail.push("run :out folds CRLF, got {$s.raku}") unless $s eq "a\nb\n";
    my $b = run $*EXECUTABLE, '-e', 'print "a\r\nb"', :out, :bin;
    my $buf = $b.out.slurp(:close);
    @fail.push("run :bin gives a Buf, got {$buf.^name}") unless $buf ~~ Blob;
    @fail.push('…with every CR in it') unless $buf.elems == 4;
}

# .reduce over an `is raw` accumulator hands down containers
{
    my %hash;
    my @path = <a b c>;
    my $slot := (%hash, |@path).reduce: -> $h is raw, $k { $h{$k} };
    @fail.push('the fold vivifies nothing by itself') unless %hash.elems == 0;
    $slot = 42;
    @fail.push("assigning the leaf builds the path, got {%hash.raku}")
        unless %hash<a><b><c> == 42;
}

# `.++ for do given … { when … { $a } }` increments $a itself
{
    my $a = 41;
    .++ for do given 1 { when 1 { $a } };
    @fail.push("when's container, got $a") unless $a == 42;
    my $b = 41;
    .++ for do given 1 { default { $b } };
    @fail.push("default's container, got $b") unless $b == 42;
    my $c = 1;
    .++ for do { my $c = 5; $c };
    @fail.push("a block's own variable is not the outer one, got $c") unless $c == 1;
}

# backtrace frames: the setting's throw, the routine, each bare block, the unit
{
    {
        my sub bar { X::AdHoc.new.throw }();
        CATCH { default {
            my @f = .backtrace.list;
            # throw, bar, the two bare blocks around the call, the unit
            @fail.push("five frames, got {+@f}") unless @f == 5;
            @fail.push('the setting throw leads') unless @f[0].is-setting && @f[0].code.name eq 'throw';
            @fail.push('then the thrower') unless @f[1].code.name eq 'bar';
            @fail.push('then the bare blocks') unless @f[2].code ~~ Block && @f[3].code ~~ Block && !@f[2].is-setting;
            @fail.push('and the unit last') unless @f[4].subname eq '<unit>';
        } }
    }
}

# $t += Duration stays a Duration
{
    my $d = now - now;
    my $t;
    $t += $d;
    @fail.push("Any += Duration, got {$t.^name}") unless $t ~~ Duration;
}

# Pair.freeze keeps the identity
{
    my $value = 17;
    my $pair = number => $value;
    my $w = $pair.WHICH;
    $pair.freeze;
    @fail.push('freeze keeps the WHICH') unless $w eqv $pair.WHICH;
}

# a bare `fail` reads its own routine's $!, not the caller's
{
    try { die "earlier" };
    my sub f { fail };
    my $r = f();
    @fail.push("bare fail says Failed, got {$r.exception.message}")
        unless $r.exception.message eq 'Failed';
}

# '-'.IO is standard input
{
    my $p = run $*EXECUTABLE, '-e', q{use v6.c; '-'.IO.slurp.print}, :in, :out, :err;
    $p.in.print: 'meows';
    $p.in.close;
    my $o = $p.out.slurp(:close);
    @fail.push("'-'.IO.slurp reads stdin, got {$o.raku}") unless $o eq 'meows';
    @fail.push('…and 6.c says nothing about it') unless $p.err.slurp(:close) eq '';
}

# a version pragma is the unit's first statement or nothing
{
    my $p = run $*EXECUTABLE, '-e', 'use Test; use v6.c;', :out, :err;
    my $e = $p.err.slurp(:close);
    @fail.push('use v6.c after another statement dies')
        unless $p.exitcode != 0 && $e.contains('Too late to switch language version');
}

if @fail { say "FAILED:"; .say for @fail; say "FAIL"; exit 1 }
say "PASS";
