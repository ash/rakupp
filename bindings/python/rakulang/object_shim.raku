# The Raku half of rakulang's object surface: modules, objects and methods
# reached from Python.
#
# The binding rk_eval's this file the first time a program asks for an object
# (Interp.use, a keyword argument to Interp.call), then reaches everything
# below with rk_call. grammar_shim.raku is its sibling and shares nothing
# with it.
#
# Every sub here returns its result through rk-py-out, the TRANSPORT: data
# Python has a type for (Int, Num, Rat, Str, Bool, and Arrays, Lists, Seqs,
# Hashes and Maps of those) travels as itself, and everything else travels in
# an RkPyBox. Python converts the data and roots each box as a
# rakulang.Object, so one walk on the Python side, with no calls in between,
# turns any result into Python values — which matters, because an unrooted
# value lives only until the next call.
#
# Boxing everything that is not data, rather than only what rk_type cannot
# describe, keeps the rule in one place: rk_type answers RK_ANY for a type
# object and RK_ARRAY for a lazy Seq, so neither could travel bare. Every
# entry point unboxes its invocant and arguments with rk-py-in.

my class RkPyBox { has Mu $.value }

# A module handle: the scope that imported it, as a closure that looks names
# up there, plus the names the import brought in.
my class RkPyModule {
    has Str $.name;
    has &.look;
    has $.syms;
}

# Bumped when a sub here changes signature or meaning; the binding checks it
# right after loading.
sub rk-py-abi() { 1 }

# Python lists and dicts arrive as fresh Arrays and Hashes, so unboxing walks
# into them; a box's own contents are never touched.
sub rk-py-in(Mu \v) is raw {
    my \t = v.WHAT;
    t =:= RkPyBox ?? v.value
      !! t =:= Array ?? v.map(&rk-py-in).Array
      !! t =:= Hash  ?? %(v.pairs.map({ .key => rk-py-in(.value) }))
      !! v
}

my $data-types = (Int, Num, Rat, Str, Bool);

sub rk-py-out(Mu \v) {
    my \t = v.WHAT;
    v.exception.throw if t ~~ Failure;
    unless v.DEFINITE {
        # Any (an empty Scalar), Nil and Mu are Python's None; every other
        # type object is a value a program wants to keep: Point, to call
        # .new on it.
        return Any if t =:= Any || t =:= Nil || t =:= Mu;
        return RkPyBox.new(value => v);
    }
    # by identity: an enum value, an allomorph and a subclass are not data
    return v if $data-types.grep({ t =:= $_ });
    if t =:= Array || t =:= List || t =:= Slip || t =:= Seq {
        # a lazy one may never end, so Python iterates it instead
        return v.is-lazy ?? RkPyBox.new(value => v) !! v.map(&rk-py-out).Array;
    }
    if t =:= Hash || t =:= Map {
        my %h;
        %h{.key} = rk-py-out(.value) for v.pairs;
        return %h;
    }
    RkPyBox.new(value => v)
}

# Python names cannot hold a hyphen, Raku names usually do: to_json means
# to-json. The exact spelling is tried first, so a Raku name that really has
# an underscore still wins.
sub rk-py-spellings(Str $name) {
    my $dashed = $name.subst('_', '-', :g);
    $dashed eq $name ?? ($name,) !! ($name, $dashed)
}

# Keyword arguments take the same mapping, settled against the routine's
# signature: a key the signature names as written stays, otherwise the dashed
# spelling is used. A routine with no visible signature gets the dashed one,
# the Raku convention.
sub rk-py-named(@candidates, %named) {
    my %known;
    for @candidates -> &c {
        for (try &c.signature.params) // () -> $p {
            %known{$_} = True for $p.named_names;
        }
    }
    my %out;
    for %named.kv -> $k, Mu \v {
        my $key = %known{$k} ?? $k !! $k.subst('_', '-', :g);
        %out{$key} = rk-py-in(v);
    }
    %out
}

sub rk-py-candidates(Mu \code) {
    code ~~ Routine ?? (try code.candidates) // (code,) !! (code,)
}

sub rk-py-args(@pos) { @pos.map(&rk-py-in) }

# ---- modules -----------------------------------------------------------------

# Python evaluates `do { use NAME ARGS; rk-py-module('NAME', -> $n { ::($n) },
# MY::.keys) }`, so the closure sees exactly what the import brought in.
sub rk-py-module(Str $name, &look, $keys) {
    return RkPyBox.new(value => RkPyModule.new(:$name, :&look)) without $keys;
    my $syms = $keys.grep({ .starts-with('&') || .substr(0, 1) ~~ /<alpha>/ })
                    .grep({ $_ ne $name }).List;
    RkPyBox.new(value => RkPyModule.new(:$name, :&look, :$syms))
}

# (the symbol) when there is one, () when there is none. A missing name
# throws X::NoSuchSymbol or comes back as its Failure; the flag says which
# happened rather than the value of `try`, which is the very thing in
# question. A list, not a Nil sentinel: Nil does not survive every binding
# (Raku++ binds `my \x = Nil` as Any).
sub rk-py-look(&look, Str $name) {
    my $missing = False;
    my \r = do {
        CATCH { default { $missing = True } }
        look($name)
    }
    $missing || r.WHAT ~~ Failure ?? () !! (r,)
}

# The names a module handle answers to, for Python's dir().
sub rk-py-module-names(Mu \m) {
    my $mod = rk-py-in(m);
    return [] without $mod.syms;
    my @names = $mod.syms.map({ .starts-with('&') ?? .substr(1) !! $_ });
    # a package is a type object, so `with` would skip it
    for rk-py-look($mod.look, $mod.name) -> Mu \pkg {
        @names.append: pkg.WHO.keys.grep({ $_ ne 'EXPORT' })
                          .map({ .starts-with('&') ?? .substr(1) !! $_ });
    }
    @names.unique.sort.Array
}

# [kind, value]: ['value', v] for something to hand to Python, ['method', name]
# for a method to call on the module's own type, ['missing'] otherwise.
sub rk-py-module-get(Mu \m, Str $name) {
    my $mod = rk-py-in(m);
    for rk-py-spellings($name) -> $n {
        for "&$n", $n -> $sym {
            # no symbol list: the mainline handle, where every name is open
            if !$mod.syms.defined || $mod.syms.first($sym).defined {
                for rk-py-look($mod.look, $sym) -> Mu \v {
                    return ['value', rk-py-out(v)];
                }
            }
        }
    }
    return ['missing'] if $mod.name eq '';
    my @pkg = rk-py-look($mod.look, $mod.name);
    return ['missing'] unless @pkg;
    my \pkg = @pkg[0];
    my @got = rk-py-attr-of(pkg, $name);
    # a method here belongs to the module's own type, not to the handle
    @got.push(RkPyBox.new(value => pkg)) if @got[0] eq 'method';
    @got
}

# ---- objects -----------------------------------------------------------------

# The public attributes of the core types a program meets most, as Rakudo
# declares them (`has $.key`). They are listed rather than read through
# .^attributes so that obj.name means the same on every engine: the core
# types' own attribute tables are not something to rely on.
my @core-accessors =
    (Pair,        <key value>),
    (Enumeration, <key value>),
    (Range,       <min max>),
    (Complex,     <re im>),
    (Rational,    <numerator denominator>),
    (DateTime,    <year month day hour minute second timezone>),
    (Date,        <year month day>),
    (X::AdHoc,    <payload>),
    (Exception,   ());

# Is obj.name a public attribute's accessor? For the core types above, the
# list says. For everything else, the `has $.name` declarations of each class
# in the MRO that is not one of them.
sub rk-py-accessor(Mu \obj, Str $n) {
    for @core-accessors -> ($type, $names) {
        return True if obj ~~ $type && $names.first($n).defined;
    }
    for obj.^mro -> Mu \c {
        next if c =:= Any || c =:= Mu || c =:= Cool
             || @core-accessors.first({ c =:= .[0] }).defined;
        return True if c.^attributes(:local).first({
            .has_accessor && .name.substr(2) eq $n }).defined;
    }
    False
}

# What Python's obj.name means. A type object or a package looks in its
# namespace first (Geo::Shape, Point's nested types); then methods. A public
# attribute's accessor runs at once and its value comes back, so p.x reads as
# Python reads an attribute; any other method comes back as ['method', name]
# for Python to bind and call.
sub rk-py-attr-of(Mu \obj, Str $name) {
    my @spellings = rk-py-spellings($name);
    unless obj.DEFINITE {
        for @spellings -> $n {
            for $n, "&$n" -> $sym {
                return ['value', rk-py-out(obj.WHO{$sym})] if obj.WHO{$sym}:exists;
            }
        }
    }
    for @spellings -> $n {
        next unless obj.^can($n);
        return ['value', rk-py-out(obj."$n"())] if obj.DEFINITE && rk-py-accessor(obj, $n);
        return ['method', $n];
    }
    return ['method', $name] if obj.^can('FALLBACK');
    # A type whose method table the engine does not expose (some core types
    # on Raku++: Date.^can('new') is empty) cannot be asked, so the call is
    # made and Raku says whether the method exists. A type that lists its
    # methods is believed.
    # A module or package has no methods to guess at.
    return ['method', @spellings.tail]
        if !$name.starts-with('_') && !(try obj.^methods(:local))
        && obj.HOW.^name !~~ /ModuleHOW|PackageHOW/;
    ['missing']
}

sub rk-py-attr(Mu \o, Str $name) {
    rk-py-attr-of(rk-py-in(o), $name)
}

# obj.name = value, for an `is rw` attribute (or any rw method).
sub rk-py-set-attr(Mu \o, Str $name, Mu \value) {
    my \obj = rk-py-in(o);
    for rk-py-spellings($name) -> $n {
        next unless obj.^can($n);
        obj."$n"() = rk-py-in(value);
        return True;
    }
    die "{obj.^name} has no method '$name' to assign through";
}

sub rk-py-call-method(Mu \o, Str $name, @pos, %named) {
    my \obj = rk-py-in(o);
    my @cands = obj.^can($name);
    rk-py-out(obj."$name"(|rk-py-args(@pos), |rk-py-named(@cands, %named)))
}

sub rk-py-invoke(Mu \c, @pos, %named) {
    my \code = rk-py-in(c);
    rk-py-out(code.(|rk-py-args(@pos), |rk-py-named(rk-py-candidates(code), %named)))
}

# Interp.call with keyword arguments or object arguments: a sub by name in
# the mainline scope, as rk_call finds it.
sub rk-py-call-sub(Str $name, @pos, %named) {
    my &code = ::("&$name");
    code(|rk-py-args(@pos), |rk-py-named(rk-py-candidates(&code), %named))
}

# Python's protocols on an object, one entry point so the binding stays thin.
sub rk-py-op(Mu \o, Str $op, Mu \arg = Nil) {
    my \obj = rk-py-in(o);
    given $op {
        when 'str'      { obj.DEFINITE ?? obj.Str !! obj.gist }
        when 'repr'     { obj.raku }
        when 'name'     { obj.^name }
        when 'int'      { obj.Int }
        when 'num'      { obj.Num }
        when 'bool'     { obj.Bool }
        when 'elems'    { obj.elems }
        when 'callable' { obj ~~ Callable }
        when 'defined'  { obj.DEFINITE }
        when 'eqv'      { obj eqv rk-py-in(arg) }
        when 'which'    { obj.WHICH.Str }
        when 'pos'      { rk-py-out(arg < 0 ?? obj[* + arg] !! obj[arg]) }
        when 'key'      { rk-py-out(obj{arg}) }
        when 'iter'     { RkPyBox.new(value => obj.iterator) }
        when 'pull'     {
            my \x = obj.pull-one;
            x =:= IterationEnd ?? ['end'] !! ['value', rk-py-out(x)]
        }
        default { die "rk-py-op: unknown op '$op'" }
    }
}
