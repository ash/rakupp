# Regression: three things META6's suite needed, each wrong in a way that gave a
# plausible answer rather than an error.
#
# 1. `state` in a ROLE method was shared by every class composing the role.
#    Rakudo makes each composition a distinct closure, so each gets its own slot.
#    META6's AutoAssoc memoises a json-name -> attribute map in `state %lookup`,
#    so META6's table answered META6::Support's subscripts and `$obj<support>`
#    read back empty.
# 2. `$obj<a><b> = v` never reached the inner object's ASSIGN-KEY: the container
#    protocol was dispatched for a variable, `self` or a method-call base, but
#    not for a NESTED SUBSCRIPT, and the whole statement was silently a no-op.
# 3. `Version.new("v0.0.1")` silently dropped the leading `v`. Rakudo keeps it as
#    a literal PART — which is how a module notices the mistake at all: META6
#    warns 'prefix "v" seen in version string' off `.parts[0] eq "v"`.
# Contract: exit 0 + last line PASS.
my @fail;

# ---- 1. state in a role method is per composition -------------------------
role Memo {
    method tag() { state $t = self.^name ~ '-computed'; $t }
    method counter() { state $n = 0; ++$n }
}
class MA does Memo { }
class MB does Memo { }
@fail.push("MA.tag ({MA.new.tag})") unless MA.new.tag eq 'MA-computed';
@fail.push("MB.tag ({MB.new.tag})") unless MB.new.tag eq 'MB-computed';
# …and the slot really is per class, not merely computed twice
MA.new.counter; MA.new.counter;
@fail.push("MB.counter ({MB.new.counter})") unless MB.new.counter == 1;
@fail.push("MA.counter ({MA.new.counter})") unless MA.new.counter == 3;

# a role arriving as the PARENT (`class X does R` with no other parent) composes
# through a different list than `does` on a second role — both must be covered
role Solo { method who() { state $w = self.^name; $w } }
class SA does Solo { }
class SB does Solo { }
@fail.push('solo') unless SA.new.who eq 'SA' && SB.new.who eq 'SB';

# ---- 2. assignment through a nested subscript ------------------------------
class Leaf does Associative {
    has Str $.source is rw;
    method AT-KEY($k)          { $!source }
    method ASSIGN-KEY($k, \v)  { $!source = v }
}
class Branch does Associative {
    has Leaf $.leaf;
    method AT-KEY($k) { $!leaf }
}
my $leaf = Leaf.new(source => 'orig');
my $branch = Branch.new(leaf => $leaf);
@fail.push("nested read ({$branch<leaf><source>})") unless $branch<leaf><source> eq 'orig';
$branch<leaf><source> = 'written';
@fail.push("nested write ({$branch<leaf><source>})") unless $branch<leaf><source> eq 'written';
# the write reached the REAL object, not a copy
@fail.push("through-object ({$leaf.source})") unless $leaf.source eq 'written';
# one level still works
my $direct = Leaf.new(source => 'a');
$direct<source> = 'b';
@fail.push('one level') unless $direct.source eq 'b';

# ---- 3. a leading `v` in a version STRING is a part -------------------------
@fail.push("v-parts ({Version.new('v0.0.1').parts.raku})")
    unless Version.new('v0.0.1').parts.head eqv 'v';
@fail.push('plain parts') unless Version.new('0.0.1').parts eqv (0, 0, 1);
@fail.push('v-detect') unless Version.new('v1.2.3').parts[0] eq 'v';
# the `v0.0.1` LITERAL is a different path and keeps its numeric parts. It needs
# the parens: `v1.2.3.parts` lexes as ONE longer version literal on both engines.
@fail.push("literal ({(v1.2.3).parts.raku})") unless (v1.2.3).parts eqv (1, 2, 3);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
