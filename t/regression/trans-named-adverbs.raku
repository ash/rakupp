# Regression: a NAMED argument to Str.trans is never a mapping — issue #120.
#
# `.trans("\t" => "  ", :g)` took `:g` for one more pair and replaced every
# letter g with a space ("Right grep" became "Ri ht  rep"). Rakudo's trans
# takes :c/:s/:d (any truthy value, `:delete(1)` too) and ignores the rest.
# Terminal::UI expands tabs with exactly that call. The JS backend ignored
# :complement, and an empty replacement side under it kept what it must drop.
#
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

ck "Right grep\tgg".trans("\t" => "  ", :g), "Right grep gg", ':g is ignored';
ck "gag".trans("a" => "x", :g, :foo<bar>), "gxg", 'unknown named arguments are ignored';
ck "aabbcc".trans("ab" => "x", :delete(1)), "xxcc", ':delete with an Int value deletes';
ck "aabbcc".trans("ab" => "x", :d), "xxcc", ':d';
ck "aabbcc".trans("a" => "x", :squash), "xbbcc", ':squash';
ck "aabbcc".trans("a" => "x", :s(0)), "xxbbcc", 'a false :s is off';
ck "abc".trans("a" => "x", :c), "axx", ':complement';
ck "abcd".trans("ab" => "xy", :complement), "abxx", ':complement takes the first replacement character';
ck "abcd".trans("ab" => "", :c), "ab", ':complement with an empty side drops the rest';
ck "abcdd".trans("a" => "x", :c, :s), "ax", ':complement with :squash';
my %o = :s; ck "aabbcc".trans("a" => "x", |%o), "xbbcc", 'a slipped adverb';
ck "gag".trans(["a"] => ["x"], :g), "gxg", 'with list sides';
ck "Right grep".trans("\t" => "  "), "Right grep", 'no adverb, nothing to do';

if @fail { .say for @fail; say 'FAIL'; exit 1 }
say 'PASS';
