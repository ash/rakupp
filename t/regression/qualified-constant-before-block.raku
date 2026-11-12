# Regression: a package's `our constant` named with its package before a
# statement's block — `for RakuPress::Schema::WP-TABLES -> $t { … }` — was
# refused at compile time, "Function 'RakuPress::Schema::WP-TABLES' needs
# parens to avoid gobbling block": the parser read the qualified name as a
# listop that took the block, so RakuPress's import did not compile and
# learn.raku.online could not be published. Rakudo reads it as the constant.
# A qualified name before a block is now a term, and the unit's own check
# refuses it only when its package does not resolve, so `for Nope::x { }`
# still says what Rakudo says.
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.
module Schema { our constant TABLES = <posts users>; our sub kinds { <a b> } }

my @fail;
sub ck($got, $want, $what) { @fail.push("$what: got {$got.raku}") unless $got eqv $want }

my @seen;
for Schema::TABLES -> $t { @seen.push: $t }
ck @seen, ['posts', 'users'], 'for Pkg::CONST -> $t';
@seen = ();
for Schema::TABLES { @seen.push: $_ }
ck @seen, ['posts', 'users'], 'for Pkg::CONST { … }';
ck (do if Schema::TABLES { 'yes' }), 'yes', 'if Pkg::CONST { … }';

ck (try EVAL 'for Nope::thing -> $t { }; 1') // $!.^name, 'X::Comp::Group',
   'an undeclared package still refuses at compile time';

say @fail ?? "FAIL\n" ~ @fail.join("\n") !! 'PASS';
