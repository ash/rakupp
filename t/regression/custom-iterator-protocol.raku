# Regression: a class deciding for itself what iterating it MEANS.
#   * an object with its own `.iterator` method supplies the iterator a `for`
#     walks. Only an object that already had `pull-one` was ever driven, so a
#     container class that overrode `.iterator` was silently iterated the default
#     way. The iterator it hands back may be a user object driven by `pull-one`,
#     or a built-in one, whose remaining items are taken directly.
#   * such an object ITERATES even through a `$` variable: being Iterable is what
#     decides, not the sigil.
#   * `my @a := SubclassOfArray.new` BINDS the object. Coercing it to a plain
#     Array threw away the class, and with it every method the subclass added —
#     which is why the doc's SkippingArray iterated like an ordinary Array.
#   * IterationEnd is the protocol's SENTINEL: a literal one sitting in a list
#     ends a `for` there (both the block and the statement-modifier form), while
#     `.map` sees an ordinary element that stringifies to its own name.
#   * `.skip-one`/`.skip-at-least` answer an Int, not a Bool; an iterator over an
#     unordered source is not deterministic; `.lazy` marks a list lazy.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# a container subclass that changes what iterating it means
class SkippingArray is Array {
    method iterator {
        class :: does Iterator {
            has $.index is rw = 0;
            has $.array is required;
            method pull-one {
                $.index++ while !$.array.AT-POS($.index).defined && $.array.elems > $.index;
                $.array.elems > $.index ?? $.array.AT-POS($.index++) !! IterationEnd
            }
        }.new(array => self)
    }
}
my @a := SkippingArray.new;
@a.append: 1, Any, 3, Int, 5, Mu, 7;
check(@a.^name,  'SkippingArray', 'binding keeps the class');
check(@a.elems,  '7',             'and the elements are the appended ones');
my @pairs;
for @a -> $x, $y { @pairs.push("[$x $y]") }
check(@pairs.join(' '), '[1 3] [5 7]', 'the custom iterator skips the undefined values');

# an ordinary Array subclass still behaves like an Array
class Plain is Array { method greet { 'hi' } }
my @p := Plain.new;
check(@p.^name,  'Plain', 'a plain subclass keeps its name');
check(@p.greet,  'hi',    'and its own methods');
check(@p.elems,  '0',     'and starts empty');
@p.push(5);
check(@p.elems,  '1',     'and pushes');
check(@p[0],     '5',     'and indexes');

# an Iterable object iterates even through a $ variable
class DNA does Iterable {
    has $.chain;
    method new ($chain where { $chain ~~ /^^ <[ACGT]>+ $$ / } ) { self.bless( :$chain ) }
    method iterator(DNA:D:) { $!chain.comb.rotor(3).iterator }
}
my $d := DNA.new('GAATCC');
my @codons;
@codons.push(.gist) for $d;
check(@codons.join('|'), '(G A A)|(T C C)', 'a scalar-held Iterable is iterated');

# IterationEnd is a sentinel
my @seen;
@seen.push($_) for ["foo", IterationEnd, "baz"];
check(@seen.join('|'), 'foo', 'a literal IterationEnd ends the loop');
my @seen2;
for ["foo", IterationEnd, "baz"] { @seen2.push($_) }
check(@seen2.join('|'), 'foo', 'the block form agrees');
check(["foo", IterationEnd, "baz"].map({ "«" ~ $_ ~ "»" }).gist,
      '(«foo» «IterationEnd» «baz»)', 'but map sees an ordinary element');
check(IterationEnd.Str,  'IterationEnd', 'it stringifies to its name');
check(IterationEnd.gist, 'IterationEnd', 'and gists bare');
check((Any).gist,        '(Any)',        'an ordinary type object is unaffected');

# the iterator protocol's smaller corners
my $i = <a b>.iterator;
check($i.skip-one,  '1', 'skip-one answers an int');
check($i.pull-one,  'b', 'and advances');
check($i.skip-one,  '0', 'and answers 0 at the end');
my $j = <a b c>.iterator;
check($j.skip-at-least(2),  '1', 'skip-at-least answers an int');
check($j.pull-one,          'c', 'and advances');
check($j.skip-at-least(20), '0', 'and answers 0 when it cannot');
check((1..10).iterator.is-deterministic,            'True',  'a range iterator is deterministic');
check(%(a => 42, b => 137).iterator.is-deterministic, 'False', 'a hash iterator is not');
check((1 ... 1000).is-lazy,       'False', 'a finite sequence is not lazy');
check((1 ... 1000).lazy.is-lazy,  'True',  'but .lazy marks it');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
