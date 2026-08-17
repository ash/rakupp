# Regression: `%` and `&` followed by a Unicode letter are VARIABLES, not
# operators. The lexer only accepted an ASCII identifier after those two
# sigils (they double as modulo / junction), so `my &δy = …` parsed as an
# anonymous `&` plus assignment to the bareword `δy` and died "Target is
# not assignable". Same hole as `my %цены`. `$`/`@` already worked.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eq $want }

my &δy = -> $x { $x + 1 };
check(δy(4), 5, 'my &δy is a callable');

my %цены = хлеб => 3;
check(%цены<хлеб>, 3, 'my %цены initializer');
%цены<молоко> = 7;
check(%цены<молоко>, 7, 'assign through %цены<…>');

my $Δ = 42;
check($Δ, 42, 'ASCII-checked $ still takes a Unicode name');
my @данные = 1, 2, 3;
check(@данные[1], 2, '@-sigil Unicode name');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
