# A rule's code is LEXICAL to the grammar: `<?{ is-a($0) }>` calls the `my
# sub` declared beside the grammar. The code ran in the scope live at MATCH
# time, so a grammar loaded from a module — or a role's token mixed in at run
# time, as a slang does with `^mixin` — could not see its own module's
# lexicals: "Undefined routine 'is-a'". Slang::Nogil's `check-keywords` is
# exactly that. The caller's `$*` variables must still reach the rule.
#
# Runs under both engines: Rakudo passes every check natively.
#
# Contract: exit 0 + last line PASS.
use lib $?FILE.IO.parent.add('../fixtures/rule-home-lib');
use RuleHome;
my @fail;
my $*SEEN = '';   # (the grammar's `{ $*SEEN = $home }` writes the caller's)

sub check($got, $want, $desc) {
    @fail.push("$desc: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

check so(RuleHome::G.parse('a')), True,  'a module grammar calls its module\'s `my sub`';
check so(RuleHome::G.parse('b')), False, '…and the sub decides';
{ $*SEEN = ''; RuleHome::G.parse('a'); check $*SEEN, 'module-lexical', '…and reads its module\'s `my` variable' }
check so((Grammar but RuleHome::R).parse('a')), True, 'a role\'s token mixed in at run time';
check so((Grammar but RuleHome::R).parse('b')), False, '…and the sub decides there too';

# the caller's dynamics, innermost first
my $*FLAG = True;
check so(RuleHome::Flag.parse('x')), True, 'a `$*` variable of the caller';
{ my $*FLAG = False; check so(RuleHome::Flag.parse('x')), False, '…the innermost one' }
sub f { my $*FLAG = False; RuleHome::Flag.parse('x') }
check so(f()), False, '…set in a routine';

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
