# The general-interpreter faults behind Cro::Core's dist suite (the 50th
# battery dist), reduced to dependency-free shapes. Each block prints its own
# check; the file passes when the last line is PASS and exit is 0.
my $fails = 0;
sub check($name, $got, $want) {
    if $got ne $want {
        say "FAIL $name: got <$got> want <$want>";
        $fails++;
    }
    else {
        say "ok $name";
    }
}

# 1. `state` in a loop body restarts per loop-STATEMENT execution (Rakudo
#    closure-clone semantics). Cro.compose's `++state $split` Z-loop recursed
#    forever when the counter leaked across calls.
{
    my @out;
    sub f(@a) {
        my @r;
        for @a {
            ++state $n;
            push @r, $n;
        }
        @r.join(',')
    }
    push @out, f(<a b c>);
    push @out, f(<a b>);
    check 'state-in-for resets per call', @out.join('|'), '1,2,3|1,2';
    # ...while a routine-level state var still persists across calls
    sub g { state $n = 0; ++$n }
    g();
    check 'state-in-sub persists', g().Str, '2';
}

# 2. A sigilless binding to an array is the array itself, not an itemized
#    scalar: flat() flattens it. Cro::CompositeConnector builds its component
#    list as flat(before, $con-tran, after) with `my \before = @!before`.
{
    my @empty;
    my \b = @empty;
    my $x = 42;
    check 'flat over sigilless empty', flat(b, $x, b).join(','), '42';
    my @two = 1, 2;
    my \d = @two;
    check 'flat over sigilless full', flat(d, 3, d).join(','), '1,2,3,1,2';
    # and a $-container still itemizes
    my $t = (1, 2);
    my @c = $t;
    check '$-container itemizes', @c.elems.Str, '1';
}

# 3. Match.caps lists positional AND named captures, one entry per occurrence,
#    in match order. Cro::MediaType joins qtext/quoted-pair caps to decode
#    quoted parameter values.
{
    grammar G {
        token TOP { <value> }
        token value { '"' ~ '"' [<qtext> | <quoted-pair>]* }
        token qtext { <-["\\\n]>+ }
        token quoted-pair { \\ <( . )> }
    }
    my $m = G.parse('"ab\\"cd"');
    check 'caps joins named occurrences', $m<value>.caps.map(*.value.Str).join, 'ab"cd';
}

# 4. Identity/introspection on an on-demand supply must not run its block.
#    isa-ok in Cro's composer tests double-processed every sink message.
{
    my $taps = 0;
    my $s = supply { whenever Supply.from-list(1, 2) -> $v { $taps++ } };
    my $isa = $s.isa(Supply);
    my $before = $taps;
    $s.list;
    check 'isa does not tap', "$isa $before $taps", 'True 0 2';
}

# 5. Draining a supply waits for a whenever on a still-pending Promise (Cro's
#    Connector.establish awaits connect() inside the supply block).
{
    my class Tr {
        method transformer(Supply $incoming) {
            supply { whenever $incoming { emit "T:" ~ $_ } }
        }
    }
    my $in = supply { emit "interested" };
    my $out = supply {
        my Promise $connection = start { Tr.new };
        whenever $connection -> $transform {
            whenever $transform.transformer($in) -> $msg { emit $msg }
        }
    };
    check 'drain waits for promise-whenever', $out.list.join(','), 'T:interested';
}

# 6. .Channel on an on-demand supply is a LIVE conversion: emits that happen
#    after the conversion still arrive (establish(...).Channel then emit).
{
    my class Tr2 {
        method transformer(Supply $incoming) {
            supply { whenever $incoming { emit $_.uc } }
        }
    }
    my $sup = Supplier::Preserving.new;
    my $chan = supply {
        my Promise $connection = start { Tr2.new };
        whenever $connection -> $transform {
            whenever $transform.transformer($sup.Supply) -> $msg { emit $msg }
        }
    }.Channel;
    $sup.emit("bbq");
    check 'live .Channel sees late emit', $chan.receive, 'BBQ';
}

# 7. Parameterized roles: `::T` type captures resolve in role method bodies
#    (composed AND punned), and value-param puns work — Cro::ConnectionState[
#    TestState] / Cro::Policy::Timeout[%defaults] use every one of these.
{
    my role R[::T] {
        method t { T }
        method mk { T.new }
    }
    my class C does R[Int] { }
    check 'composed ::T resolves (instance)', C.new.t.^name, 'Int';
    check 'composed ::T resolves (type)', C.t.^name, 'Int';
    check 'punned ::T resolves', R[Str].t.^name, 'Str';
    my role P[%defaults] {
        has %.d;
        method get(Str $k) { %!d{$k} // %defaults{$k} }
    }
    my $obj = P[%( one => 1, two => 2 )].new(d => %( two => 22 ));
    check 'pun value-param default', $obj.get('one').Str, '1';
    check 'pun value-param override', $obj.get('two').Str, '22';
}

# 8. A broken Promise in a react whenever (no QUIT phaser) fails the react —
#    directly, and through a supply wrapping it (Cro::TCP's dies-ok on a
#    refused connect).
{
    my $p = Promise.new;
    $p.break("nope");
    my $died = 'no';
    {
        react { whenever $p { } }
        CATCH { default { $died = 'yes' } }
    }
    check 'broken promise fails react', $died, 'yes';
    my $died2 = 'no';
    my $qs = supply { whenever $p -> $v { emit $v } };
    {
        react { whenever $qs { } }
        CATCH { default { $died2 = 'yes' } }
    }
    check 'quit supply fails react', $died2, 'yes';
}

if $fails == 0 {
    say "PASS";
}
else {
    say "FAILED: $fails";
    exit 1;
}
