# Regression: the cluster `rakupp test Data::Generators` walked into — a Rat
# index into a lazy sequence, Range items spread inside an array literal, a
# junction TOPIC in smartmatch, Range not doing Positional, and pick/roll given
# the Whatever TYPE OBJECT rather than the `*` literal. Each is a path roast
# never walks; the module's own suite (and Math::SpecialFunctions', its
# neighbour) died on each in turn.
# Contract: exit 0 + last line PASS. Passes under both engines, so it doubles
# as an oracle.
my @fail;

# 1. a RAT index into a lazy sequence materialises up to it, as an Int one does.
#    Math::SpecialFunctions indexes its Bernoulli gather as $seq[($n + 2) / 2].
{
    my $g = gather { my $i = 0; loop { take $i++ } };
    @fail.push('Int index') unless $g[70] == 70;
    my $h = gather { my $i = 0; loop { take $i++ } };
    @fail.push('Rat index') unless $h[140/2] == 70;
    my $k = gather { my $i = 0; loop { take $i++ } };
    @fail.push('Num index') unless $k[70e0] == 70;
}

# 2. a Range ITEM in an array literal stays a Range; only the one-arg form
#    spreads. Data::Generators writes its character sets as
#    [<1 8 A>, <Y H>, "0".."9"] and checks `.all ~~ Range or …` per element.
{
    @fail.push('one-arg spreads') unless [1..10].elems == 10;
    @fail.push('two ranges')      unless [1..3, 5..6].elems == 2;
    @fail.push('range types')     unless so [1..3, 5..6].all ~~ Range;
    @fail.push('mixed')           unless [<a b>, "0".."9"].elems == 2;
    @fail.push('mixed kinds')     unless [<a b>, "0".."9"].map({ .WHAT.^name }).join(',') eq 'List,Range';
    # assignment: a SINGLE range flattens, several stay whole — both as before
    @fail.push('assign one')  unless do { my @a = 1..3; @a.elems } == 3;
    @fail.push('assign many') unless do { my @a = 1..3, 5..6; @a.elems } == 2;
}

# 3. a junction TOPIC in smartmatch collapses to a Bool — ACCEPTS threads over
#    the topic — except against a regex, which keeps the junction of Matches.
{
    my $j = (1, 2).all;
    @fail.push('type matcher')  unless ($j ~~ Int) === True;
    @fail.push('range matcher') unless ($j ~~ 1..5) === True;
    @fail.push('code matcher')  unless ($j ~~ { $_ > 0 }) === True;
    @fail.push('literal')       unless ($j ~~ 1) === False;
    @fail.push('junc matcher')  unless ($j ~~ any(1, 2)) === True;
    @fail.push('negated')       unless ($j !~~ Str) === True;
    # the regex exception: a junction of Matches, truthy when the kind says so
    @fail.push('regex stays')   unless ((<ab cb>).all ~~ /b/).WHAT.^name eq 'Junction';
    @fail.push('regex truthy')  unless so ((<ab cb>).all ~~ /b/);
    # the module's own shape: per-element "is it a list of strings"
    sub is-pos-str($vec) { ($vec ~~ Positional) and ($vec.all ~~ Str) }
    my $ranges = [<1 8 A>, <Y H>, "0".."9"];
    @fail.push('the module check') unless
        (($ranges.isa(Array) or $ranges.isa(List)) and
         ($ranges.all ~~ Range or $ranges.all ~~ (is-pos-str($_))));
}

# 4. a Range does Positional
{
    @fail.push('Str range Positional') unless ("0".."9") ~~ Positional;
    @fail.push('Int range Positional') unless (1..3) ~~ Positional;
    @fail.push('range Iterable')       unless (1..3) ~~ Iterable;
}

# 5. pick/roll with the Whatever TYPE OBJECT — the spelled-out name, not the
#    `*` literal — behaves as the literal does. Data::Generators defaults its
#    draw method that way: `&method = $size.isa(Whatever) ?? &pick !! &roll`.
{
    my @l = ^10;
    @fail.push('pick(Whatever)')     unless @l.pick(Whatever).elems == 10;
    @fail.push('pick sub')           unless pick(Whatever, @l).sort.join(',') eq (^10).join(',');
    @fail.push('roll(Whatever)')     unless @l.roll(Whatever)[^3].elems == 3;
    @fail.push('roll lazy')          unless @l.roll(Whatever).is-lazy;
    @fail.push('pick(*) unchanged')  unless @l.pick(*).elems == 10;
}

die "FAILED: {@fail.join(', ')}" if @fail;
say 'PASS';
