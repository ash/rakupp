# Regression: an object whose class has a `gist` but no `Str` stringified
# through that gist under `~`, interpolation, infix `~` and `join` — while
# `.Str` gave `Type<id>`, as Rakudo does everywhere. IO::Notification::Change
# has exactly that shape.
# Contract: exit 0 + last line PASS.
my @fail;

class A { method gist { "gist" } }
my $a = A.new;
for (~$a, "$a", $a ~ "", join(",", $a)) -> $s {
    @fail.push("stringified as {$s.raku}") unless $s.starts-with("A<");
}
class B { method Str { "str" }; method gist { "gist" } }
@fail.push("Str method ignored") unless ~B.new eq "str" && "{B.new}" eq "str";
@fail.push("gist lost") unless A.new.gist eq "gist";

say @fail ?? "FAIL: @fail.join('; ')" !! "PASS";
exit @fail ?? 1 !! 0;
