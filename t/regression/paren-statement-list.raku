# The inside of `( … )`, `%( … )`, `@( … )` and `$( … )` is a statement list,
# as it is at the top level and inside `[ … ]` (issue #288):
#
#   my %output = %(
#     my %dict;
#     for @list { %dict{$_} = $_ }
#     %dict;
#   );
#
# was "Two terms in a row across lines": a `}` that closes a line ended the
# statement everywhere except in parens. Statements also only worked FIRST in
# plain parens, so `(for 1 {}; 5)`, `(1; if 1 { 2 })` and `$(if 1 { 2 })`
# were "expected )".
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

my @list = "a", "b", "c";
my %output = %(
  my %dict;
  for @list {
    %dict{$_} = $_;
  }
  %dict;
);
check %output, %(a => "a", b => "b", c => "c"), 'the #288 program';

check @(
  for 1, 2 { }
  5
), (5,), '@( … ): a for block ends its line';

check $(
  if 1 { 2 }
  5
), 5, '$( … ): an if block ends its line';

my $r = (
  my %d;
  for 1 { }
  %d
);
check $r.elems, 3, '( … ): three statements, three segments';
check $r[1], (Nil,), '…the for gives its list';

check (for 1 { }; 5), ((Nil,), 5), 'a for heading a ;-list';
check (if 1 { 3 }; 5), (3, 5), 'an if heading a ;-list';
check (given 1 { 7 }; 5), (7, 5), 'a given heading a ;-list';
check (1; if 1 { 2 }), (1, 2), 'an if in a later segment';
check $(if 1 { 2 }), 2, '$( if … )';
check %(1; if 1 { a => 2 }), %(a => 2), '%( …; if … )';

# …and what a `}` at a line end does NOT split
my %h = a => 1;
check (%h{'a'}
), 1, 'a subscript before the closing paren';
check (sub { 1 }
)(), 1, 'a sub before the closing paren';
check (if 0 { 3 } else { 4 }), 4, 'if/else is one statement';
check ($_ for 1, 2), (1, 2), 'the modifier form is unchanged';

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
