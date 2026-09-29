# Regression: `::T:U $x` with NO type T anywhere threw X::Parameter::InvalidType.
#
# The smiley form is a constraint that captures nothing (see
# type-capture-smiley-is-constraint.raku), and Rakudo accepts it when T names
# no type at all — in a sub, a method, a role, a pointy block, positional or
# named. v5.0.0's check for parameters typed with an undeclared name saw the
# bare T and refused the declaration. YAMLish 0.1.3 declares
#
#     our sub load-yaml(Str $input, ::GrammarType:U :$schema = ::Schema::Core, :%tags)
#
# with no GrammarType anywhere, so the first `use YAMLish` from a cold
# precompilation store died; a warm store hid it. The parameter now carries
# Param::typeMayBeUndeclared and the checks pass it by — that parameter only:
# T used as a LATER parameter's type is still invalid, and T in the body is
# still undeclared, on both engines.
#
# What the smiley does to binding is not asserted here: rakupp enforces it
# (`f(42)` for `::G:U` throws) where Rakudo does not, as v4.0.1 did too.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
use MONKEY-SEE-NO-EVAL;
my @fail;
sub bound(&c, $want, $what) {
    my $got = try { c() };
    return @fail.push("$what: threw {$!.^name}: {$!.message.lines.head}") if $!;
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want;
}
sub dies(Str $code, $what) {
    my $got = try EVAL $code;
    @fail.push("$what: returned {$got.raku} instead of dying") unless $!;
}

# ---- no G is declared anywhere in this file ----------------------------------
sub pos-u(::G:U $x)          { $x.^name }
sub named-u(::G:U :$x = Int) { $x.^name }
sub pos-d(::G:D $x)          { $x }
sub pos-any(::G:_ $x)        { $x.^name }
bound { pos-u(Int)   }, 'Int', 'a positional `::G:U`';
bound { named-u()    }, 'Int', 'a named `::G:U` with a default';
bound { pos-d(42)    }, 42,    'a `::G:D`';
bound { pos-any(Str) }, 'Str', 'a `::G:_`';

class C { method m(::G:U $x) { $x.^name } }
bound { C.m(Int) }, 'Int', 'a method parameter';

role R[::G:U $t] { method n { $t.^name } }
class K does R[Int] { }
bound { K.n }, 'Int', 'a role parameter';

my &pointy = -> ::G:U $x { $x.^name };
bound { pointy(Str) }, 'Str', 'a pointy-block parameter';

# YAMLish's own shape: the default is a nested class, and any type object binds
class Schema { class Core { } }
sub load(Str $in, ::GrammarType:U :$schema = ::Schema::Core, :%tags) { $schema.^name }
bound { load('x')                  }, 'Schema::Core', 'the YAMLish signature, default';
bound { load('x', :schema(Int))    }, 'Int',          'the YAMLish signature, a schema passed';

# ---- …and it declares nothing ------------------------------------------------
dies 'sub f(::G:U $x, G $y) { }; f(Int, 1)', 'G as a later parameter type is still invalid';
dies 'sub f(::G:U $x) { G }; f(Int)',        'G in the body is still undeclared';

# ---- a module, compiled cold and then loaded from the store -------------------
my $dir = $*TMPDIR.add("tu-undeclared-{$*PID}-{now.Rat.nude.join('-')}");
$dir.mkdir;
$dir.add('TUUndeclared.rakumod').spurt: q:to/END/;
    unit module TUUndeclared;
    our sub pick(Str $in, ::GrammarType:U :$schema = Int) is export { "$in:{$schema.^name}" }
    END
for <cold warm> -> $when {
    my $p = run $*EXECUTABLE, '-I', $dir.Str, '-e', 'use TUUndeclared; print pick("a")', :out, :err;
    my ($out, $err) = $p.out.slurp(:close), $p.err.slurp(:close);
    @fail.push("module, $when load: exit {$p.exitcode}, out {$out.raku}, err {$err.lines.head.raku}")
        unless $p.exitcode == 0 && $out eq 'a:Int';
}
run 'rm', '-rf', $dir.Str;

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
