# Regression: `.new(a => <a Seq a sub returned>)` fills `has @.a`.
#
# The default constructor puts a named value through coerceToSigil, which
# copied the Array's buffer when the body was shared, so the attribute would
# own it. A LAZY Seq's buffer holds only what has been pulled: a `.map` handed
# back from a sub had pulled nothing, and the attribute came out empty, with
# no error. A `.map` written in the argument list arrived unshared and went
# through whole, which is why only the returned one failed. JSON::Unmarshal's
# `has Inner @.inners is unmarshalled-by(&code)` builds exactly that, and its
# t/080-trait.rakutest failed, so `rakupp install` skipped the dist. Such a Seq
# is now stored as list assignment stores it: made if finite, lazy if endless.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($what, $got, $want) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}

class Inner { has Str $.name }
class T { has Inner @.inners }
class U { has @.items }
my @names = <one two three>;
sub mk(@in) { @in.map(-> $name { Inner.new(:$name) }) }
sub plain(@in) { @in.map({ $_ }) }

check 'typed, a sub\'s .map',   T.new(inners => mk(@names)).inners.map(*.name).list, <one two three>;
check 'untyped, a sub\'s .map', U.new(items => plain(@names)).items, [<one two three>];
check ':items(…) spelling',     U.new(:items(plain(@names))).items.elems, 3;
check 'bless',                  U.bless(items => plain(@names)).items.elems, 3;
check 'a returned gather',      U.new(items => (sub { gather { take $_ for @names } })()).items.elems, 3;
check 'a returned grep',        U.new(items => (sub (@i) { @i.grep(*.so) })(@names)).items.elems, 3;
check 'a Seq through *%args',   do { sub make(*%a) { U.new(|%a) }; make(items => plain(@names)).items.elems }, 3;
check 'what was always right: inline .map', U.new(items => @names.map({ $_ })).items.elems, 3;

# made AT construction, into a buffer of the attribute's own
{
    my @src = 1, 2, 3;
    my $u = U.new(items => (sub { @src.map(* + 1) })());
    @src[0] = 99;
    check 'made at .new', $u.items, [2, 3, 4];
    $u.items.push(5);
    check 'the attribute owns it', ($u.items.elems, @src.elems), (4, 3);
    check 'an Array, not a Seq', $u.items.^name, 'Array';
}

# …and an endless one stays lazy, as `my @a = …` keeps it
{
    my $l = U.new(items => (sub { (1..*).map(* * 2) })());
    check 'endless: lazy', $l.items.is-lazy, True;
    check 'endless: its head', $l.items[^4], (2, 4, 6, 8);
}

# as JSON::Unmarshal has it: the Seq bound into a hash, the hash flattened
{
    my %args;
    %args<inners> := mk(@names);
    check 'bound into %args, then |%args', T.new(|%args).inners.elems, 3;
}

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
