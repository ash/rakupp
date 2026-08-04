# Regression: a `handles`-delegated ASSIGN-KEY counts, 2026-08-04. Found chasing
# DBIish, which sets its converter table from inside BUILD. Checked against
# Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — " ~ $got.raku ~ ' vs ' ~ $want.raku; $ok = False } }

# `$obj{k} = v` looks for ASSIGN-KEY before falling back to the generic container
# path. It only consulted the class's own method table, and `handles` gives the
# object those methods WITHOUT putting them there — they dispatch fine when
# called, so the interception simply missed them and the assignment replaced the
# whole object with a plain Hash.
{
    my class TC does Associative {
        has %!s handles <AT-KEY EXISTS-KEY ASSIGN-KEY>;
        method m() { 'TC' }
    }
    my class T {
        has %.C is TC;
        submethod BUILD { %!C{'k'} = 42 }
    }
    my $t = T.new;
    ck($t.C.^name, 'TC', 'the object survives an assignment through its subscript');
    ck($t.C{'k'},  42,   'and the value is readable back');
    ck($t.C.m,     'TC', 'and its own methods still work');
}

# a real ASSIGN-KEY method is unaffected
{
    my class Own does Associative {
        has %!s;
        method AT-KEY($k) { %!s{$k} }
        method ASSIGN-KEY($k, $v) { %!s{$k} = $v }
        method m() { 'Own' }
    }
    my class U { has %.C is Own; submethod BUILD { %!C{'x'} = 7 } }
    my $u = U.new;
    ck(($u.C.^name, $u.C{'x'}), ('Own', 7), 'a declared ASSIGN-KEY still works');
}

# and a plain hash attribute is untouched by any of it
{
    my class P { has %.h; submethod BUILD { %!h{'k'} = 1 } }
    my $p = P.new;
    ck(($p.h.^name, $p.h{'k'}), ('Hash', 1), 'a plain Hash attribute is unchanged');
}

say $ok ?? 'PASS' !! 'FAIL';
