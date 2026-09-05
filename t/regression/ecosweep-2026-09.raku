# Regression: the ecosystem batch of 2026-09-05 — ten fixes found by putting the
# 2,526-dist population's failure clusters back through `rakupp test`.
#
#   * a pseudo-package key may be COMPUTED (`MY::{$n}`) or INTERPOLATED
#     (`MY::<<$n>>`), and the chain form (`OUTER::MY::`) names a scope OUT
#   * `use lib` takes a comma list, not just its first path
#   * `our $x is export` in a module is ONE container, shared with the importer
#   * an import inside a block is LEXICAL and shadows an outer routine there
#   * `subset CC of Str() where …` — a coercion base type
#   * `--> CONSTANT` is a definite return VALUE, not a type constraint
#   * `* => 1` / `"a" => *` are WhateverCodes; `a => *` stays a Pair
#   * a nested class shadows an outer one for its parent's attribute types
#   * enum SeekType and $*DEFAULT-READ-ELEMS exist
#   * an IO::Handle subclass's .nl-out is writable per instance
#
# Runs clean under Rakudo too.

my $fails = 0;
sub ck($got, $want, $desc) {
    if $got eqv $want { say "ok - $desc" }
    else { $fails++; say "FAIL: $desc — {$got.raku} vs {$want.raku}" }
}

# ---- pseudo-package keys ------------------------------------------------
my $x = 42;
my $n = '$x';
ck MY::<$x>:exists,        True,  'a written-out key';
ck MY::{$n}:exists,        True,  'a computed key';
ck MY::<<$n>>:exists,      True,  'an interpolated key';
ck MY::{'$nope'}:exists,   False, 'and it can say no';
ck MY::<<$n>>,             42,    'the interpolated key reads the value';
ck MY::{$n},               42,    'and so does the computed one';
ck OUTER::MY::<<$n>>:exists, False, 'OUTER asks the ENCLOSING scope';
{
    my $inner = 7;
    my $iname = '$inner';
    ck OUTER::MY::<<$iname>>:exists, False, 'a block sees its own name as not OUTER';
    ck OUTER::MY::<<$n>>:exists,     True,  '…and its enclosing scope\'s as OUTER';
}

# ---- subset with a coercion base ---------------------------------------
subset Digits of Str() where { !.contains(/<-[\d\ ]>/) };
ck 5212345678901234 ~~ Digits, True,  'an Int coerces into a Str subset';
ck 'nope!'          ~~ Digits, False, '…and a non-conforming Str still fails';
sub take-digits(Digits $d) { $d.WHAT.^name }
ck take-digits(1234), 'Str', 'the parameter binds the COERCED value';

# ---- a definite return value -------------------------------------------
enum Card <NotACard Visa>;
constant KAY = 'kay';
sub dirty($, *% --> NotACard) { }
sub konst(--> KAY) { }
sub literal(--> 42) { my $ignored = 5 }
ck dirty(1),   NotACard, 'an enum member as the return type IS the return value';
ck konst(),    'kay',    'and so is a constant';
ck literal(),  42,       'and so is a literal, over what the body evaluated';

# ---- Whatever-currying through `=>` -------------------------------------
ck (* => 1).WHAT.^name,     'WhateverCode', '`* => 1` curries';
ck ("a" => *).WHAT.^name,   'WhateverCode', '…and so does a `*` on the right';
ck (a => *).WHAT.^name,     'Pair',         'but a BAREWORD key is a pair literal';
ck (a => *.uc).WHAT.^name,  'Pair',         '…even with a WhateverCode value';
ck (* => 1)('k').raku,      ':k(1)',        'the curry builds the pair';
ck <a b>.map(* => 1).raku,  '(:a(1), :b(1)).Seq', 'and one per element under .map';
my %country = <US MX>.map(* => Visa);
ck %country<US>, Visa, 'a table written that way reads back';

# ---- a nested class shadows an outer one for attribute types ------------
class Inner { has Int $.z }
class Holder {
    class Inner { has Str $.name is required }
    has Inner @.inners;
    method make() { @!inners.push: Inner.new(name => 'one'); @!inners.elems }
}
ck Holder.new.make, 1, 'the attribute type is the NESTED Inner';

# ---- the seek enum and the read-chunk dynamic --------------------------
ck SeekFromBeginning.^name,   'SeekType', 'SeekType exists';
ck SeekFromCurrent.value,     1,          '…with Rakudo\'s values';
ck SeekFromEnd.value,         2,          '…all three of them';
ck $*DEFAULT-READ-ELEMS,      65536,      '$*DEFAULT-READ-ELEMS is the chunk size';

# ---- a handle subclass owns its line endings ---------------------------
class Sink is IO::Handle {
    method whats-my-nl() { self.nl-out }
}
my $sink = Sink.new;
ck $sink.nl-out, "\n", 'the default output line ending';
$sink.nl-out = "\t\t";
ck $sink.nl-out, "\t\t", '…is writable per instance';
ck $sink.whats-my-nl, "\t\t", '…and the class reads back what was written';

# ---- a module: shared `our` containers, and a lexical import -----------
# (the two fixtures live beside this file, in eco-lib/)
use lib $?FILE.IO.parent.add('eco-lib').Str, $?FILE.IO.parent.Str;  # a COMMA LIST
{
    use EcoA;
    bump(); bump();
    ck $counter, 2, 'an exported `our` variable is one container';
}
my sub who() { 'outer' }
my sub inner-import() {
    use EcoB;
    who()
}
ck inner-import(), 'from-EcoB', 'an import inside a routine shadows an outer sub';
ck who(),          'outer',     '…and only inside it';

say $fails ?? "\n$fails FAILED" !! "\nPASS";
exit $fails ?? 1 !! 0;
