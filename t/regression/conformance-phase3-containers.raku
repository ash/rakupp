# Regression: raku-spec conformance Phase 3 — value/container defects.
#   1. a QuantHash's `.raku` is the EXPRESSION that rebuilds it, not a Hash
#      literal: the weighted kinds as a pair list coerced to the kind, the Set
#      family as a constructor, a Map as Map.new((…)).
#   2. `.splice` hands back an Array (it came out of one); Complex.reals is a
#      List.
#   3. `\(…)` is exactly the parenthesised group — parsing a full prefix made
#      `\(1,2).list` a capture OF `(1,2).list`.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# 1. QuantHash .raku rebuilds the value
# single-key values only — a QuantHash's ITERATION ORDER is arbitrary in Rakudo,
# so only the FORM is comparable across engines
check(bag(<b b>).raku,          '("b"=>2).Bag',            'bag-raku');
check(<b b>.BagHash.raku,       '("b"=>2).BagHash',        'baghash-raku');
check((a => 0.5).Mix.raku,      '("a"=>0.5).Mix',          'mix-raku');
check((a => 0.5).MixHash.raku,  '("a"=>0.5).MixHash',      'mixhash-raku');
check(set(<a>).raku,            'Set.new("a")',            'set-raku');
check(<a>.SetHash.raku,         'SetHash.new("a")',        'sethash-raku');
check(Map.new((a => 1)).raku,   'Map.new((:a(1)))',        'map-raku');
# a plain Hash still renders as a Hash literal
check({ a => 1 }.raku,          '{:a(1)}',                 'plain-hash-raku');
# and the gists are untouched
check(bag(<b b>).gist,          'Bag(b(2))',               'bag-gist-unchanged');
check(set(<a>).gist,            'Set(a)',                  'set-gist-unchanged');

# 2. the container each operation hands back
my @a = <a b c d e f g>;
check(@a.splice(2, 3).raku, '["c", "d", "e"]', 'splice-returns-an-array');
check((3 + 5i).reals.raku,  '(3e0, 5e0)',      'complex-reals-is-a-list');

# 3. `\(…)` is the group, and postfixes attach to the CAPTURE
check(\(1, 2).list.raku,          '(1, 2)',            'capture-then-list');
check(\(1, 2, :x(3)).hash.raku,   'Map.new((:x(3)))',  'capture-then-hash');
check(\(1, 2, :x(3)).elems,       '2',                 'capture-then-elems');
check(\().elems,                  '0',                 'empty-capture');
my $c = \(1, 2);
check($c.raku, '\\(1, 2)', 'capture-raku-round-trips');

# 4. typed exceptions the docs demonstrate
class Req { has $.a is required }
check((try { Req.new; 'no-throw' }) // $!.^name, 'X::Attribute::Required', 'required-attr-throws');
check(Req.new(a => 5).a, '5', 'required-attr-supplied');
# a default of its own does NOT excuse `is required`
class ReqD { has $.d is required = 7 }
check((try { ReqD.new; 'no-throw' }) // $!.^name, 'X::Attribute::Required', 'required-beats-default');
# the DEFAULT constructor is named-only …
class Plain { has $.b }
check((try { Plain.new(1); 'no-throw' }) // $!.^name, 'X::Constructor::Positional', 'default-ctor-is-named-only');
check(Plain.new(b => 2).b, '2', 'named-construction-works');
# … but a class with its own .new, a BUILD, or a BUILT-IN parent takes positionals
class Own { has $.e; method new($v) { self.bless(e => $v) } }
check(Own.new(9).e, '9', 'own-new-takes-positional');
class Built { has $.f; submethod BUILD(:$!f = 3) { } }
check(Built.new.f, '3', 'build-submethod');
my $sub = my class MyNum is Num { }.new(NaN);
check($sub.defined, 'True', 'builtin-parent-takes-positional');
# Channel wording
my $ch = Channel.new; $ch.close;
check((try { $ch.send(1); 'no-throw' }) // $!.message, 'Cannot send a message on a closed channel', 'send-on-closed-wording');

# 5. an allomorph IS both of its types, in an ASSIGNMENT check as well as a
#    smartmatch — `my Str $s = <42>` holds
my $is = <42>;
check($is.^name, 'IntStr', 'allomorph-name');
my Int $ai = $is; check($ai, '42', 'allomorph-satisfies-Int');
my Str $as = $is; check($as, '42', 'allomorph-satisfies-Str');
sub takes-str(Str $x) { $x }
check(takes-str($is), '42', 'allomorph-binds-to-a-Str-param');
my $ns = <1e5>;
my Num $an = $ns; check($an, '1e5', 'numstr-satisfies-Num');
my Str $an2 = $ns; check($an2, '1e5', 'numstr-satisfies-Str');
# a plain Int still does NOT satisfy Str
check((try { my Str $bad = 42; 'no-throw' }) // 'threw', 'threw', 'plain-int-is-not-a-Str');

# 6. Mix gists like Bag — elem(weight), a weight of 1 omitted
check((butter => 0.22).Mix.gist, 'Mix(butter(0.22))', 'mix-gist-form');
check((a => 1.0).Mix.gist,       'Mix(a)',            'mix-gist-omits-weight-one');
check((a => 2).Bag.gist,         'Bag(a(2))',         'bag-gist-unchanged-2');

# 7. a Buf/Blob is a Str only in REPRESENTATION — its .raku is the constructor
#    that rebuilds it over its ELEMENTS, and the ENCODING names the result type
check(Buf.new(1, 42, 3).raku,   'Buf.new(1,42,3)',      'buf-raku');
check(Blob.new(1, 2).raku,      'Blob.new(1,2)',        'blob-raku');
check(blob8.new(1, 2).raku,     'Blob[uint8].new(1,2)', 'blob8-raku');
check(blob32.new(1, 2).raku,    'Blob[uint32].new(1,2)','blob32-raku');
check(Buf.new.raku,             'Buf.new()',            'empty-buf-raku');
check('abc'.encode.raku,        'utf8.new(97,98,99)',   'encode-raku');
check('abc'.encode.^name,       'utf8',                 'encode-names-its-encoding');
check('abc'.encode.gist,        'utf8:0x<61 62 63>',    'encode-gist');
check('abc'.encode.decode,      'abc',                  'encode-round-trips');
check(Buf.new(1, 2).gist,       'Buf:0x<01 02>',        'buf-gist-unchanged');
# a plain Str is still a string literal
check('plain'.raku,             '"plain"',              'plain-str-raku');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
