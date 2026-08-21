# Regression: the cluster `rakupp test Statistics::Distributions` turned up —
# .^method_names, multi methods that span the inheritance chain, Unicode named
# arguments, `:a(:$!attr)` aliases, and `.flat(:hammer)`. All five are paths
# roast never walks; the module's own suite died on each in turn.
# Contract: exit 0 + last line PASS.
my @fail;

# 1. .^method_names — the method TABLE's names: local, role methods flattened
#    in, public accessors included, private methods out.
role R1 { method rm { } }
class MN does R1 {
    has $.x is rw;
    method cm { }
    method !hidden { }
}
class MNKid is MN { method kid { } }
{
    # Rakudo's tables also carry the internal POPULATE, which rakupp has no
    # analogue for — drop it so this file is an oracle both engines pass.
    sub own(@n) { @n.grep(* ne 'POPULATE').sort.join(' ') }
    my $got = own(MN.^method_names);
    @fail.push("method_names: $got") unless $got eq 'cm rm x';
    @fail.push('method_names is not Str') unless MN.^method_names.all ~~ Str:D;
    # inherited methods are NOT in the subclass's own table
    @fail.push('method_names inherited') unless own(MNKid.^method_names) eq 'kid';
    # an instance answers the same as the type object
    @fail.push('method_names on instance') unless own(MN.new.^method_names) eq 'cm rm x';
    # .^methods(:local) sees the flattened role too
    @fail.push('methods :local') unless own(MN.^methods(:local).map(*.name)) eq 'cm rm x';
}

# 2. a multi method's candidate set spans the MRO: the parent's positional
#    candidate answers a call the child's named-only candidates cannot take
class MBase {
    multi method gen(UInt:D $size = 1) { self.gen(:$size) }
    multi method gen(UInt:D :$size = 1) { !!! }
}
class MKid is MBase {
    multi method gen(UInt:D :$size) { "kid $size" }
}
{
    @fail.push('multi chain, no args')  unless MKid.new.gen        eq 'kid 1';
    @fail.push('multi chain, one arg')  unless MKid.new.gen(3)     eq 'kid 3';
    @fail.push('multi chain, named')    unless MKid.new.gen(:size(4)) eq 'kid 4';
}

# 3. a Unicode identifier is a named argument, not a positional Pair
sub uni(:$μ = 0, *%rest) { "$μ/{%rest.elems}" }
{
    @fail.push('unicode named arg') unless uni(:μ(5)) eq '5/0';
    my @pos;
    sub slurpy(*@p, *%n) { @pos = @p; %n }
    my %n = slurpy(:ν(7));
    @fail.push('unicode slurpy named') unless %n<ν> == 7 && !@pos;
}

# 4. `:a(:$!attr)` answers to the ATTRIBUTE's name as well as the outer key —
#    the second name is `shape`, not the twigil-carrying `!shape`
class Aliased {
    has Numeric:D $.shape    = 1;
    has Numeric:D $.location = 0;
    submethod BUILD(:a(:$!shape) = 1, :μ(:$!location) = 0) { }
}
{
    @fail.push('alias by outer key')    unless Aliased.new(:a(7), :μ(8)).shape == 7;
    @fail.push('alias by unicode key')  unless Aliased.new(:a(7), :μ(8)).location == 8;
    @fail.push('alias by attr name')    unless Aliased.new(:shape(5), :location(10)).shape == 5;
    @fail.push('alias by attr name 2')  unless Aliased.new(:shape(5), :location(10)).location == 10;
    @fail.push('alias defaults')        unless Aliased.new.shape == 1 && Aliased.new.location == 0;
}

# 5. .flat(:hammer) hammers the containers flat — itemisation stops mattering
{
    @fail.push('hammer array of arrays') unless [[1,2],[3]].flat(:hammer).join(',') eq '1,2,3';
    @fail.push('hammer itemised')        unless (1, $[2,3]).flat(:hammer).join(',') eq '1,2,3';
    @fail.push('hammer deep')            unless [[1,[2,3]],4].flat(:hammer).join(',') eq '1,2,3,4';
    @fail.push('hammer range')           unless (1..3, [4,5]).flat(:hammer).join(',') eq '1,2,3,4,5';
    # :hammer(False) and plain .flat keep the itemised Arrays whole
    @fail.push('hammer False')           unless [[1,2],[3]].flat(:hammer(False)).elems == 2;
    @fail.push('plain flat unchanged')   unless [[1,2],[3]].flat.elems == 2;
    # the shape Statistics::Distributions needs: four numbers out of two pairs
    my ($a, $b, $c, $d) = [[0, 0], [1, 0]].flat(:hammer);
    @fail.push('hammer destructure') unless ($a, $b, $c, $d).join(',') eq '0,0,1,0';
}

die "FAILED: {@fail.join(', ')}" if @fail;
say 'PASS';
