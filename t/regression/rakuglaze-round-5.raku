# Regression: the fifth Rakuglaze round — the judgement calls left by round 4.
#
# A Supply that quits hands out a list that raises only when it is READ past
# the values it emitted, so a sunk `.list` raises nothing; `@$list` over a lazy
# list keeps its source; an INIT in a parameter default runs once, at program
# start (inside an EVAL too, where the unit's subs and `my`s exist by then);
# a subscript key is evaluated before the right side that changes what it
# reads; a `:D` coercion target refuses a type object.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }
sub dies($code, $type, $what) {
    my $got = 'nothing';
    try { $code(); CATCH { default { $got = .^name } } }
    @fail.push("$what: died with $got, wanted $type") unless $got eq $type;
}

# a quit Supply's list: the values, then the error — and nothing when sunk
{
    my $bad = supply { emit 'x'; emit 'y'; die 'broke' };
    my @seen;
    try { for $bad.list { @seen.push($_) }; CATCH { default { @seen.push(.message) } } }
    ck @seen, ['x', 'y', 'broke'], 'for over a quit Supply reads up to the error';
    my $l = $bad.list;
    ck $l[0], 'x', 'a quit Supply list reads its first value';
    try $bad.list;
    ck $!, Nil, 'a sunk .list of a quit Supply raises nothing';
    dies { my @a = $bad.list }, 'X::AdHoc', 'assigning a quit Supply list raises';
}

# `@$x` over a lazy list keeps the source
{
    my $g = (gather { take 1; take 2 }).list;
    my @seen; for @$g { @seen.push($_) }
    ck @seen, [1, 2], '@$ over a gather list';
    ck @$g.elems, 2, '@$ over a gather list counts it';
}

# an INIT in a parameter default runs once, at INIT time
{
    my $out = EVAL q:to/END/;
        my $calls = 0;
        sub lang { $calls++; 'en' }
        sub f(:$language = INIT { lang }) { $language }
        (f(), f(), f(language => 'fr'), $calls).join(' ')
        END
    ck $out, 'en en fr 0', 'INIT in a named default, in an EVAL';
    my $top = EVAL q[sub seven { 7 }; my $s; INIT $s = seven(); $s];
    ck $top, 7, 'a top-level INIT in an EVAL calls the unit\'s sub';
}

# the key is evaluated before a right side that changes what it reads
{
    my %h; my $i = 0;
    %h{~$i} = $i++ for ^3;
    ck %h.sort.map({ "{.key}:{.value}" }).join(' '), '0:0 1:1 2:2', '%h{~$i} = $i++';
    my %g; $i = 0;
    %g{$i} = ++$i;
    ck %g, %(1 => 1), 'a bare variable key is read after the right side';
    # …and a key that changes what the right side reads (2026-10-09)
    my @a; $i = 0;
    @a[$i++] = $i;
    ck @a, [1], '@a[$i++] = $i stores 1 at index 0';
    my %k; $i = 0;
    %k{$i++} = $i;
    ck %k, %(0 => 1), '%k{$i++} = $i';
}

# a :D coercion target refuses a type object
{
    sub esc(Str:D() $s) { $s }
    ck esc(3.14), '3.14', 'Str:D() coerces a value';
    dies { esc(Int) }, 'X::AdHoc', 'Str:D() refuses a type object';
}

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
