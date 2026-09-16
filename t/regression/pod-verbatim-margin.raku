# Regression: which Pod text is VERBATIM.
#
# A block's margin is the indent of its own `=begin` delimiter; text indented
# past that is a Pod::Block::Code. The margin was read off the first CONTENT
# line instead, so a block whose content was ALL indented set its own margin
# and had no verbatim text at all — the commonest shape there is, an example
# indented under a `=begin pod` at column 0. Pod::Utils and Pod::Utilities both
# ask `first-code-block` for exactly that and both got "".
#
# A verbatim run spans internal blank lines, but only while it stays at or
# right of where it started: a line indented LESS than the run's first line
# ends it and opens a new one.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku} want {$want.raku}") unless $got eqv $want
}
# `$=pod` is per-compilation-unit, so each shape is its own EVAL'd unit.
use MONKEY-SEE-NO-EVAL;
sub shapes($src) {
    my @c = EVAL($src ~ "\n\$=pod[0].contents");
    @c.map({
        my $t = .contents[0] ~~ Str ?? .contents[0].subst("\n", '|', :g) !! '-';
        .^name.subst('Pod::Block::', '') ~ '(' ~ $t ~ ')'
    }).join(', ')
}

# Delimiter at column 0, the example indented under it: verbatim.
check shapes(q:to/POD/), 'Code(say "code";)', 'an indented example under =begin pod is code';
    =begin pod

        say "code";
    =end pod
    POD

# …and after a directive, which is where Pod::Utils' own test file put it.
check shapes(q:to/POD/), 'Named(-), Code(say "code";)', 'still code after a =TITLE';
    =begin pod
    =TITLE T

        say "code";
    =end pod
    POD

# The WHOLE block indented: content level with the delimiter is ordinary, and a
# continuation line indented further is still part of the paragraph.
check shapes(q:to/POD/), 'Para(text code)', 'content at the margin is a paragraph';
        =begin pod
        text
            code
        =end pod
    POD

# Content LEFT of an indented delimiter is ordinary too.
check shapes(q:to/POD/), 'Para(text)', 'content left of the margin is not verbatim';
        =begin pod
    text
        =end pod
    POD

# A paragraph swallows the line after it even when that line is indented —
# only a blank line (or a directive) ends one.
check shapes(q:to/POD/), 'Para(text code)', 'a paragraph continues into an indented line';
    =begin pod
    text
        code
    =end pod
    POD

# A verbatim run spans a blank line at the same indent…
check shapes(q:to/POD/), 'Code(aaa||bbb)', 'a verbatim run spans a blank line';
    =begin pod
        aaa

        bbb
    =end pod
    POD

# …and one indented further, dedented by the run's least indent.
check shapes(q:to/POD/), 'Code(aaa||  bbb)', 'and one indented further';
    =begin pod
            aaa

              bbb
    =end pod
    POD

# A SHALLOWER line ends the run, blank line or not.
check shapes(q:to/POD/), 'Code(aaa), Code(bbb)', 'a shallower line starts a new run';
    =begin pod
            aaa

          bbb
    =end pod
    POD
check shapes(q:to/POD/), 'Code(aaa), Code(bbb)', 'with no blank line between them either';
    =begin pod
            aaa
          bbb
    =end pod
    POD

# An explicit =begin code keeps its text exactly.
check shapes(q:to/POD/), 'Code(  x)', 'an explicit code block is untouched';
    =begin pod
    =begin code
      x
    =end code
    =end pod
    POD

if @fail {
    .say for @fail;
    say "FAIL";
    exit 1;
}
say "PASS";
