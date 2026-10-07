# Fixture for t/regression/rule-code-sees-its-module.raku: a grammar and a
# role whose rule code calls this module's LEXICAL sub and reads its lexical
# variable — neither is visible from the script that parses.
my sub is-a($m) { $m.Str eq 'a' }
my $home = 'module-lexical';

grammar RuleHome::G is export {
    token TOP { (\w) <?{ is-a($0) }> { $*SEEN = $home } }
}
grammar RuleHome::Flag is export {
    token TOP { \w <?{ $*FLAG }> }
}
role RuleHome::R is export {
    token TOP { (\w) <?{ is-a($0) }> }
}
