# Regression: the fifth ecosystem-sweep fix batch (2026-08-25) — the gaps the
# cluster triage surfaced: the P5* native family, the `if` dist's dependents,
# BinaryHeap/Test::Async parse forms, and the Getopt::Long gauntlet. Every
# expectation is the Rakudo 2026.07/08 answer (scratch probes, one per fix).

my $fails = 0;
sub ok($cond, $what) { $fails++ unless $cond; say "not ok - $what" unless $cond }

# --- angle-valued traits (lizmat's P5* family spells them all this way) ------
my class ReprAngle is repr<CStruct> {
    has Str    $.name;
    has uint32 $.id;
    multi method probe(::?CLASS:U: --> Nil) { }
    multi method probe(::?CLASS:D:) { $.id }
}
ok(ReprAngle.probe === Nil, 'is repr<CStruct> parses (class body intact)');

use NativeCall;
sub my-getuid(--> uint32) is native is symbol<getuid> { * }
ok(my-getuid() == +$*USER, 'is symbol<name> resolves the C symbol');

# $*USER / $*GROUP are IntStr allomorphs (P5getpwnam's suite runs on +$*USER)
ok($*USER.^name eq 'IntStr' && +$*USER > 0 && ~$*USER ne '', '$*USER is an IntStr');
ok($*GROUP.^name eq 'IntStr', '$*GROUP is an IntStr');

# CArray[Str] fields: has-decl keeps the [T]; elements deref, NULL terminates
my class GrpProbe is repr<CStruct> {
    has Str         $.gr_name;
    has Str         $.gr_passwd;
    has uint32      $.gr_gid;
    has CArray[Str] $.gr_mem;
}
sub my-getgrgid(uint32 --> GrpProbe) is native is symbol<getgrgid> { * }
{
    my $g = my-getgrgid(+$*GROUP);
    ok($g.gr_name eq ~$*GROUP, 'CStruct field reads line up (name)');
    my $m = $g.gr_mem;
    my $count = 0;
    while $m[$count].defined { $count++; last if $count > 1000 }
    ok($count <= 1000, 'CArray[Str] walk terminates on the NULL entry');
    ok(!$m[$count].defined, 'the terminator reads back as undefined Str');
}

# a `--> T` where T is a CONSTANT aliasing a class still boxes the struct
my constant GrpAlias = GrpProbe;
sub my-getgrgid2(uint32 --> GrpAlias) is native is symbol<getgrgid> { * }
ok(my-getgrgid2(+$*GROUP).gr_name eq ~$*GROUP, 'constant-aliased native return type boxes');

# --- parse forms -------------------------------------------------------------
# invocant marker AFTER traits (BinaryHeap's writable class-invocant push)
my class InvTrait {
    has @!store;
    multi method push(::?CLASS:U $_ is rw: *@values) { $_ = InvTrait.new; $_.push(|@values); $_ }
    multi method push(InvTrait:D: *@values) { @!store.append(@values); self }
    method list() { @!store.List }
}
{
    my InvTrait $h;
    $h.push(3, 1);
    ok($h.defined && $h.list eqv (3, 1), 'invocant marker parses after `is rw`');
}

# `::<name>:exists` — namespace existence probe (Test::Async's EXPORT guard)
my $probe-var = 42;
ok((::<$probe-var>:exists) === True,  '::<known>:exists is True');
ok((::<$no-such-var>:exists) === False, '::<unknown>:exists is False');
ok((::<$no-such-var>:!exists) === True, ':!exists negates');

# a LEXICAL `my rule` resolves as a grammar subrule (Getopt::Long's shared
# `rule name` lives outside its Parser grammar)
my rule shared-word { \w+ }
grammar LexSubrule {
    token TOP { <shared-word>+ % '|' }
}
ok(?LexSubrule.parse('foo|f|fooo'), 'lexical my-rule resolves inside a grammar');

# --- Getopt::Long's engine gaps ---------------------------------------------
# `is CORE::Exception` — the CORE:: qualifier names the setting's type
class ShadowEx is CORE::Exception {
    method message() { 'shadowed' }
}
ok(ShadowEx.new.message eq 'shadowed' && (ShadowEx.new ~~ Exception),
   'is CORE::Exception resolves to the builtin');

# multi params typed with a PACKAGE-RELATIVE multi-segment name
module AliasProbe {
    class Argument { }
    class Argument::Boolean is Argument { }
    my multi which(Argument::Boolean $) { 'bool' }
    my multi which(Str $) { 'str' }
    our sub probe() { which(Argument::Boolean.new) }
}
ok(AliasProbe::probe() eq 'bool', 'multi-segment relative type names dispatch');

# object hash with a smiley key shape, initialized from type-keyed pairs
{
    sub conv(Any:U $type) {
        state %c{Any:U} = (
            Pair.new(Int, *.Int),
            Pair.new(Str, *.Str),
        );
        %c{$type}
    }
    ok(conv(Int)('42') == 42,   'state %h{Any:U} keeps type keys distinct (Int)');
    ok(conv(Str)(42) eq '42',   '…and (Str)');
}

# Capture equivalence ignores named order
ok(\(1, :a, :b) eqv \(1, :b, :a), 'Capture nameds compare as a map');

# attribute defaults see CONSTRUCTED values of earlier attributes
{
    sub pick-conv(Any:U $t) { $t === Int ?? sub ($v) { $v.Int } !! sub ($v) { ~$v } }
    my role TypedConv { has Any:U $.type = Str; has Code:D $.converter = pick-conv($!type); }
    my class ConvProbe does TypedConv { }
    ok(ConvProbe.new(:type(Int)).converter.('0o12') == 10,
       'attr default reads the constructed value of an earlier attr');
}

# Mu ~~ Any is False (Any sits below Mu)
ok((Mu ~~ Any) === False && (Any ~~ Mu) === True && (5 ~~ Any) === True,
   'Mu ~~ Any is False, Any ~~ Mu True');

# typed @-params report the parametric container type
{
    sub f(Str :@foo) { }
    my $p = &f.signature.params[0];
    ok($p.type.^name eq 'Positional[Str]' && $p.type.of === Str,
       'Str :@foo is Positional[Str] with .of Str');
    ok($p.named_names eqv ("foo",), 'named_names for the simple case');
}
{
    sub f(Str :fooo(:f(:@foo))) { }
    ok(&f.signature.params[0].named_names eqv ("foo", "f", "fooo"),
       'named_names order is innermost-first');
    sub g(Bool :$bar) { }
    ok(&g.signature.params[0].constraints.^name eq 'Junction',
       'Parameter.constraints is a Junction');
}

# enums answer the Rakudo meta protocol
{
    my enum Flavor <Vanilla Mint Cocoa>;
    ok(Flavor.HOW ~~ Metamodel::EnumHOW, 'user enum HOW is EnumHOW');
    ok(Order.HOW ~~ Metamodel::EnumHOW, 'builtin Order HOW is EnumHOW');
    ok(Order.WHO{'Same'} === Same, 'Order.WHO holds the members');
    ok(Flavor.WHO{'Mint'} === Mint, 'user enum WHO holds the members');
    # an enum TYPE as a hash subscript is ONE key, not a slice
    my %by-type{Any:U} = (Pair.new(Flavor, 'f'), Pair.new(Order, 'o'));
    ok(%by-type{Order} eq 'o' && %by-type{Flavor} eq 'f',
       'enum type objects subscript as single keys');
}

# use Foo:if(COND) + Raku.legacy live in dynamic-var-caller-chain.raku

say $fails == 0 ?? 'PASS' !! "FAIL ($fails)";
exit $fails == 0 ?? 0 !! 1;
