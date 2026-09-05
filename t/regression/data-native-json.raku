# DATA-PLAN P1: the `json` tag's engine primitives, `rakupp-from-json` and
# `rakupp-to-json`.
#
# They run the same codec as the JSON::Fast fast path, reached by NAME instead
# of by loading a module — which is what makes `use Data::Native` need nothing
# installed. Two things need pinning, and they are different in kind:
#
#   1. On every input the module accepts, the primitive must be
#      INDISTINGUISHABLE from it — value for value on the way in, byte for byte
#      on the way out. That half is checked against JSON::Fast in this same
#      process, so a drift between the replica and the real thing fails here
#      rather than in somebody's program.
#
#   2. On every input it does NOT cover, the primitive must RAISE. This is the
#      one real semantic difference from the wrapper, which hands such a case
#      back to the module: a primitive has no module behind it, so "fall back"
#      is not available and every case has to be decided. The type stays
#      X::AdHoc so a CATCH written against JSON::Fast still catches.
#
# The wrapper's own behaviour is pinned by json-fast-native-fastpath.raku, which
# this must not disturb.

#?requires JSON::Fast

use Test;

my &pf = &::('rakupp-from-json');
my &pt = &::('rakupp-to-json');

my (&jf, &jt);
{ use JSON::Fast; &jf = &from-json; &jt = &to-json; }

# ---- 1. indistinguishable from the module --------------------------------

my @docs =
    '{}', '[]', 'null', 'true', 'false', '1', '-0', '2.5', '1e3', '1.5E-3',
    '"plain"', '"A\n\t\\\\"', '"héllo — ünïcode ✓"', '"😀"',
    '[1,2.5,true,null,"x"]',
    '{"a":{"b":{"c":[1,{"d":2}]}}}',
    '{"big": 123456789012345678901234567890}',   # arbitrary precision, not a double
    '{"neg": -1.25e-10, "z": 0.0}',
    ' { "spaced" : [ 1 , 2 ] } ',
    ('[' ~ ('9' x 600) ~ ']'),                   # a token no fixed buffer holds
    '{"dup":1,"dup":2}',
    ;

my @vals = 1, -0, 2.5, 1e3, "x", "é✓", True, False, Any, [], {},
           [1, [2, [3]]], { a => 1, b => [2, "x"], c => { d => Any } },
           123456789012345678901234567890, 1.5e-8, "\c[0]tab\there",
           { z => 1, a => 2, m => 3 };

my @optsets = { :!pretty }, { :pretty }, { :!pretty, :sorted-keys },
              { :pretty, :spacing(4) };

for @docs -> $d {
    is-deeply pf($d).raku, jf($d).raku, "from-json matches the module: $d";
}
for @vals -> $v {
    for @optsets -> %opt {
        is pt($v, |%opt), jt($v, |%opt),
           "to-json matches the module, byte for byte: {$v.raku} {%opt.raku}";
    }
}

# The typing ladder is the reason to use Raku rather than a JS-shaped parser:
# an integer token is an Int of any size, a decimal is a Rat, an exponent form
# is a Num. Pinned separately because `.raku` equality above would also pass if
# BOTH sides were wrong.
is pf('[1, 2.5, 1e0, "s", true, null]').list.map({ .WHAT.^name }).join(' '),
   'Int Rat Num Str Bool Any', 'the value ladder is Int/Rat/Num/Str/Bool/Any';

# ---- 2. NaN and Inf follow the module, which does not throw ---------------

# Probed 2026-09-05: JSON::Fast writes `null` when $*JSON_NAN_INF_SUPPORT is
# unset and bare NaN/Inf/-Inf when it is set — output its own parser then
# refuses to read back. That is the module's business; the primitive matches it
# rather than improving on it.
is pt([NaN, Inf, -Inf], :!pretty), '[null,null,null]',
   'NaN and Inf are null when $*JSON_NAN_INF_SUPPORT is unset';
{
    my $*JSON_NAN_INF_SUPPORT = 1;
    is pt([NaN, Inf, -Inf], :!pretty), '[NaN,Inf,-Inf]',
       'and bare NaN/Inf/-Inf when it is set';
}

# ---- 3. what the primitive REFUSES, where the wrapper would delegate ------

# to-json's parameter is Any in the module, so to-json(Mu) dies in its binder.
# The primitive has no binder to die in and must refuse it itself, or a program
# would get `null` here and an exception there.
throws-like { pt(Mu) }, X::AdHoc, message => /'Mu'/,
    'to-json(Mu) is refused, as the module refuses it';
throws-like { pt(Date.new(2026, 9, 5)) }, X::AdHoc, message => /'type tag'/,
    'a Date is refused rather than having its fields invented';
throws-like { pt(1, :nope) }, X::AdHoc, message => /'adverb'/,
    'an unknown adverb is refused, naming the problem';
throws-like { pt({ a => 1 }, :sorted-keys(-> $a, $b { 0 })) }, X::AdHoc,
    message => /'Callable'/,
    'a Callable :sorted-keys comparator is refused for now (DATA-PLAN P1 leaves it)';
throws-like { pt(1, 2) }, X::AdHoc, message => /'positional'/,
    'a second positional argument is refused';

throws-like { pf('{"a": }') }, X::AdHoc, message => /'malformed JSON'/,
    'malformed input raises rather than returning Nil';
throws-like { pf('[1] xx') }, X::AdHoc, message => /'trailing content'/,
    'and so does trailing content';
throws-like { pf(42) }, X::AdHoc, message => /'expected a Str'/,
    'from-json wants a Str; the Str() coercion is the module surface, not this';

# The message says WHERE, which JSON::Fast's does not. That is the one place
# these deliberately improve on the module rather than matching it.
throws-like { pf("[\n  1,\n  oops\n]") }, X::AdHoc,
    message => /'line 3'/,
    'a parse error carries line and column';
throws-like { pf('{"a": }') }, X::AdHoc, message => /'byte 6'/,
    'and the byte offset';

# ---- 4. the adverbs the primitive DOES cover -----------------------------

is pf('{"a":1}', :immutable).WHAT.^name, 'Map', ':immutable gives a Map';
is pf('[1]', :immutable).WHAT.^name, 'List', 'and a List';
is pt({ b => 1, a => 2 }, :!pretty, :sorted-keys), '{"a":2,"b":1}',
   ':sorted-keys sorts, when it is a Bool';
is pf("[1, 2] // trailing\n", :allow-jsonc), [1, 2],
   ':allow-jsonc is covered too';

done-testing;
