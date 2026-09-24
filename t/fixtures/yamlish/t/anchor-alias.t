#!raku

use Test;

use YAMLish;

plan 2;

my $text1 = q:heredoc/END/;
entries: &entries
  - one
  - two
  - three
more: *entries
END

my $d1 = load-yaml($text1);
is-deeply($d1, ${:entries($["one", "two", "three"]), :more($["one", "two", "three"])},
          "sequence anchor and alias");

my $text2 = q:heredoc/END/;
entries: &entries
  one: value
  two: value
more: *entries
END

my $d2 = load-yaml($text2);
is-deeply($d2, ${:entries(${:one("value"), :two("value")}), :more(${:one("value"), :two("value")})},
          "mapping anchor and alias");
