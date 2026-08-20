# Regression: three more from the Weekly Challenge sweep.
#
# 1. `<~~>` recurses into the pattern it is WRITTEN in. A Regex value spliced
#    into another pattern used to paste its text, so `<~~>` inside it meant the
#    HOST's pattern — anchors and all — and `my $re = rx/ '(' <~~>* ')' /;
#    "(())" ~~ /^$re$/` never matched. The self-reference is also now actually
#    implemented: it recurses into its own root instead of matching nothing.
#
# 2. A typed container cannot hold a Failure quietly: checking the type means
#    looking at the value, and looking at a Failure detonates it. `my Rat $r;
#    try { $r = $s.Rat }; if $! { …fallback… }` never took its fallback here —
#    the assignment stored the Failure and blew up somewhere else later. An
#    UNTYPED container still soaks it up.
#
# 3. `@$/` is a Match's POSITIONAL CAPTURES — `~« @$/` reads $0 $1 $2 at once —
#    where rakupp answered a one-element list holding the whole Match.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want
}

# --- 1. recursive regexes ---
my $bal = rx/ '(' <~~>* ')' /;
check ("(())"   ~~ /^$bal$/).defined, True,  'a spliced self-recursive regex matches';
check ("((()))" ~~ /^$bal$/).defined, True,  '…to any depth';
check ("(()"    ~~ /^$bal$/).defined, False, '…and rejects what does not balance';
check ("()()"   ~~ /^$bal*$/).defined, True, '…quantified, too';
check ("(())"   ~~ $bal).Str, '(())',        'and matched directly it is unchanged';

# --- 2. a Failure and a typed container ---
{
    my Rat $r;
    try { $r = "0.(12)".Rat }
    check ($! ?? 'caught' !! 'missed'), 'caught', 'the failed conversion is catchable';
    check $r.^name, 'Rat', '…and the container is left undefined';
}
{
    my $untyped = "abc".Rat;      # no type to check: the Failure soaks in
    check $untyped.^name, 'Failure', 'an untyped container still holds a Failure';
}
{
    my Int $i;
    $i = 5;
    check $i, 5, 'an ordinary typed assignment is unaffected';
}

# --- 3. @$match is the captures ---
my $m = "0.(12)" ~~ / ^ (\d+) \. (\d*) \( (\d+) \) $ /;
check (~« @$m).raku, '("0", "", "12")', 'a Match derefs to its positional captures';
my $two = "ab" ~~ /(a)(b)/;
check (my @c = @$two).elems, 2, '…as a list assignment too';
check ("ab" ~~ /ab/ andthen (@$_).elems), 0, 'a match with no captures derefs to nothing';
# assigning the Match ITSELF is still one element
my @whole = "ab" ~~ /(a)(b)/;
check @whole.elems, 1, 'assigning a Match stores the Match';
check $two[0].Str, 'a', 'and indexing it still works';

if @fail { note "FAILED: " ~ @fail.join('; '); say 'FAIL' } else { say 'PASS' }
