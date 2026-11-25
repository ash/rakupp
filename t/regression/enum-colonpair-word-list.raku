# Regression (#287): an enum whose `<<…>>` word list sets a starting value
# with a colonpair, `enum numbers << :one(1) two three >>`, did not declare
# its members as terms, so `for one, two { … }` read `two { … }` as a call
# that took the loop's block ("Function 'two' needs parens to avoid gobbling
# block"). The parser declared the members of a word list only when every
# word was a plain string; a colonpair word is a member too, as it already
# was in the parenthesised `enum E (a => 1, 'b')` form.
#
# Contract: exit 0 + last line PASS. Passes under both engines (Rakudo
# 2026.09), so it doubles as an oracle.

my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

enum numbers << :one(1) two three >>;
enum Letters « :x<p> y »;

# 1. the issue's program: members before a block
my @seen;
for one, two { @seen.push: .Int }
check @seen, [1, 2], 'for one, two { … }';

# 2. the colonpair member and the ones after it are terms
check three.Int, 3, 'three.Int';
check two + three, 5, 'two + three';
check numbers::two.value, 2, 'numbers::two.value';
if three > one { @seen.push: 'gt' }
check @seen[*-1], 'gt', 'if three > one { … }';

# 3. a Str starting value with the guillemets
check Letters.enums.sort.map(*.kv).flat.List, ('x', 'p', 'y', 'q'), 'Letters.enums';
my @l;
for x, y { @l.push: .value }
check @l, ['p', 'q'], 'for x, y { … }';

# 4. the parenthesised list: a double-quoted key is an InterpStr, and it
#    made the list opaque, so its members were not terms either
enum Paren (uno => 1, "dos", 'tres');
my @p;
for dos, tres { @p.push: .Int }
check @p, [2, 3], 'for dos, tres { … }';
check uno + dos, 3, 'uno + dos';

if @fail { .say for @fail; say 'FAIL' } else { say 'PASS' }
