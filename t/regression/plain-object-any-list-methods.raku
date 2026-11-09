# A plain object answers Any's list methods as a one-item list, as Rakudo's
# Any gives it: `class P { }; P.new.max` died "No such method 'max'", and so
# did `.min`, `.Seq`, `.is-lazy`, `.AT-POS`, `.EXISTS-KEY`, `.invert`,
# `.classify`, `.deepmap`, `.flatmap`, `.repeated`, `.pairup`, `.produce`,
# `.hash` and the rest. The subscript protocol answers too: position 0 is the
# object, another is out of range, a key is none, and nothing is deleted.
# `.invert` of a non-Pair says Rakudo's "Type check failed in invert", and a
# scalar's `.maxpairs` keys its pair by the Int 0, not the Str.
# Every line answers the same under Rakudo 2026.09.

sub ck($got, $want, $desc) {
    return if $got eqv $want;
    say "FAIL: $desc — got {$got.raku}, want {$want.raku}";
    exit 1;
}
sub name-of(&c) { try { c().raku }; $! ?? $!.^name !! 'lived' }

class P { has $.v = 1 }
my $p = P.new;
ck ($p.max === $p, $p.min === $p), (True, True), '.max and .min';
ck $p.Seq.List.elems, 1, '.Seq';
ck $p.is-lazy, False, '.is-lazy';
ck $p.AT-POS(0) === $p, True, '.AT-POS(0)';
ck $p.AT-POS(1).exception.^name, 'X::OutOfRange', '.AT-POS(1) is a Failure';
ck ($p.EXISTS-POS(0), $p.EXISTS-POS(1), $p.EXISTS-KEY("a")), (True, False, False), '.EXISTS-POS and .EXISTS-KEY';
ck ($p.ZEN-KEY === $p, $p.serial === $p), (True, True), '.ZEN-KEY and .serial';
ck $p.classify(*.so).keys.List, (True,), '.classify';
ck $p.categorize(*.so).keys.List, (True,), '.categorize';
ck $p.deepmap(*.so).List, (True,), '.deepmap';
ck $p.nodemap(*.so).List, (True,), '.nodemap';
ck $p.duckmap(*.so).List, (True,), '.duckmap';
ck $p.flatmap({ 1 }).List, (1,), '.flatmap';
ck $p.repeated.List, (), '.repeated';
ck $p.produce(&[+]).List.elems, 1, '.produce';
ck $p.collate.List.elems, 1, '.collate';
ck $p.slice(0).List.elems, 1, '.slice';
ck $p.toggle(*.so).List.elems, 1, '.toggle';
ck ($p.maxpairs[0].key, $p.minpairs[0].key, 5.maxpairs[0].key), (0, 0, 0), '.maxpairs and .minpairs, an Int key';
ck name-of({ $p.pairup }), 'X::Pairup::OddNumber', '.pairup';
ck name-of({ $p.hash }), 'X::Hash::Store::OddNumber', '.hash';
ck name-of({ $p.Map }), 'X::Hash::Store::OddNumber', '.Map';
ck (try { $p.invert.eager; 1 }) // $!.message, 'Type check failed in invert; expected Pair but got P', '.invert';
ck (try { (1,).invert.eager; 1 }) // ($!.^name, $!.message), ('X::TypeCheck', 'Type check failed in invert; expected Pair but got Int'),
   "a list's .invert";
ck (try { $p.DELETE-KEY("a"); 1 }) // $!.message, 'Can not remove values from a P', '.DELETE-KEY';
ck (try { $p.DELETE-POS(0); 1 }) // $!.message, 'Can not remove elements from a P', '.DELETE-POS';

say 'PASS';
