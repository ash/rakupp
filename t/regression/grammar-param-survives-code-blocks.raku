# Regression: a token parameter keeps its value across the token's code blocks.
# Each block (and each `** { … }` bound) overlays the rule's params into scope and
# restores them afterwards; a param with no binding of its own was restored as
# Nil, and the next block kept that Nil because an empty-string param already
# "existed". So `<t('')>` read Nil from its second block, and YAMLish's
# `token block(Str $indent, …)` warned "Use of Nil in string context" on any
# document with `---` or a nested list (every Cro `.cro.yml` from `cro stub`).
# Contract: exit 0 + last line PASS. Runs unchanged under Rakudo.

my @fail;
my @warn;
CONTROL { when CX::Warn { @warn.push: .message; .resume } }

# 1. two plain code blocks, empty-string argument
my @seen;
grammar Two {
    token TOP { <t('')> }
    token t(Str $p) { { @seen.push: $p.raku } { @seen.push: $p.raku } 'a' }
}
Two.parse('a');
@fail.push("two blocks: {@seen.raku}") unless @seen eqv ['""', '""'];

# 2. after a closure-bounded quantifier (YAMLish's block token shape)
my $after;
grammar Quant {
    token TOP { <t('', 0)> }
    token t(Str $p, Int $m) { "\n" ' ' ** { $m..* } { $after = $p } 'a' }
}
Quant.parse("\na");
@fail.push("after ** \{…\}: {$after.raku}") unless $after eqv '';

# 3. YAMLish's `block` token, verbatim apart from the tail: no warnings, right indent
my $indent-seen;
grammar Block {
    token TOP { <block('', 0)> }
    token block(Str $indent, Int $minimum-indent) {
        '&'?
        "\n"
        :my $new-indent;
        <?before $indent $<sp>=[' ' ** { $minimum-indent..* } ] { $new-indent = $indent ~ $<sp> }>
        $new-indent
        { $indent-seen = $new-indent }
        'a'
    }
}
@fail.push("block token did not parse") unless Block.parse("\n  a");
@fail.push("block indent: {$indent-seen.raku}") unless $indent-seen eqv '  ';

@fail.push("warnings: {@warn.raku}") if @warn;

if @fail { .say for @fail; say "FAIL" } else { say "PASS" }
