# Regression: the four fixes that took DateTime::Format to PASS, 2026-08-04.
# The first is the general one and by far the widest: `$.name(ARGS)` was not a
# method call. Every expectation was checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. `$.name(ARGS)` is `self.name(ARGS)` — a call that TAKES those arguments.
#    It used to parse as the no-argument accessor `$.name` followed by a postfix
#    call on whatever that returned, so the arguments never arrived and the
#    result was invoked as if it were a Callable.
{
    my class P {
        method greet($n) { "hi-$n" }
        method viaDot($n)  { $.greet($n) }
        method viaSelf($n) { self.greet($n) }
        method twoArgs($a, $b) { $.greet("$a$b") }
        method noArgs() { $.greet('none') }
    }
    ck(P.new.viaDot(1),      'hi-1',    '$.name($x) passes its argument');
    ck(P.new.viaSelf(1),     'hi-1',    'self.name($x) is unchanged');
    ck(P.new.twoArgs(1, 2),  'hi-12',   'two arguments');
    ck(P.new.noArgs,         'hi-none', 'a literal argument');

    # the bare accessor, with no parentheses, still reads the attribute
    my class Q { has $.v = 7; method read() { $.v } }
    ck(Q.new.read, 7, 'bare $.attr is still the accessor');
}

# 2. A formatter may be an OBJECT doing Callable, not only a bare Code — which
#    is how DateTime::Format ships one. Requiring Code dropped it silently and
#    the DateTime stringified as ISO-8601.
{
    my class F does Callable { method CALL-ME($d) { "F-{$d.year}" } }
    my $d = DateTime.new(:2020year, :1month, :2day, :formatter(F.new));
    ck(~$d, 'F-2020', 'an object formatter is invoked');

    my $c = DateTime.new(:2020year, :1month, :2day, :formatter(-> $x { "C-{$x.year}" }));
    ck(~$c, 'C-2020', 'a plain Callable still works');
}

# 3. Every DateTime CONVERSION keeps the formatter.
{
    my $f = -> $d { "F-{$d.hour}" };
    my $d = DateTime.new(:2020year, :1month, :2day, :3hour, :formatter($f), :timezone(3600));
    ck((~$d.utc, ~$d.clone, ~$d.in-timezone(0), ~$d.later(:1day), ~$d.earlier(:1hour)),
       ('F-2', 'F-3', 'F-2', 'F-3', 'F-2'),
       'utc / clone / in-timezone / later / earlier all stay formatted');
}

# 4. `|$obj` passes the object as ONE positional. Only a real Hash or Map slips
#    as named arguments — plenty of ordinary values are hash-BACKED here, and
#    slipping their internals meant the value never arrived at all.
{
    my sub takes-dt(DateTime $d, *%o) { $d.defined ?? "got-{$d.year}" !! 'TYPE OBJECT' }
    my $dt = DateTime.new(:2020year, :1month, :2day);
    ck(takes-dt(|$dt), 'got-2020', 'a slipped DateTime arrives as itself');

    my sub takes-named(:$a, :$b) { "$a-$b" }
    my %h = a => 1, b => 2;
    ck(takes-named(|%h), '1-2', 'a real Hash still slips as nameds');
}

say $ok ?? 'PASS' !! 'FAIL';
