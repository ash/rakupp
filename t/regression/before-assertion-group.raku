# Regression: `<?before [ … ]>` — a bracket after an explicit `before`/`after`
# keyword is a non-capturing GROUP, not a character class. The `<?[…]>` /
# `<![…]>` class-assertion shorthand only applies to the keyword-less form, so
# reading `<?before [ 'a' ]* 'b'>` as a class made the whole lookahead fail.
# This is the YAMLish block-scalar (`key: >-` / `key: |`) parse: its
# `block-string` rule opens with `<?before [ $indent <.space>* <.line-break> ]*
# $indent $<sp>=' '+ … >`.
# Contract: exit 0 + last line PASS.
my @fail;

# the group form — zero reps, one rep, and an alternation inside
@fail.push('before-group')      unless 'abc' ~~ / ^ <?before [ 'a' ] > 'abc' /;
@fail.push('before-group-star') unless 'abc' ~~ / ^ <?before [ 'zz' ]* > 'abc' /;
@fail.push('before-group-tail') unless 'abc' ~~ / ^ <?before [ 'zz' ]* 'a' > 'abc' /;
@fail.push('before-group-opt')  unless 'abc' ~~ / ^ <?before [ 'zz' ]? > 'abc' /;
@fail.push('before-group-alt')  unless 'abc' ~~ / ^ <?before [ 'a' | 'b' ] 'bc' > 'abc' /;
@fail.push('before-group-plus') if     'abc' ~~ / ^ <?before [ 'zz' ]+ > 'abc' /;
@fail.push('after-group')       unless 'xabc' ~~ / <?after [ 'x' ] > 'abc' /;

# the keyword-less shorthand still reads its bracket as a character class
@fail.push('class-assert')     unless 'abc' ~~ / ^ <?[a]> 'abc' /;
@fail.push('class-assert-neg') unless 'abc' ~~ / ^ <![x]> 'abc' /;
@fail.push('class-assert-rng') unless 'abc' ~~ / ^ <?+[a..z]> 'abc' /;
@fail.push('class-assert-sub') unless 'abc' ~~ / ^ <?-[x..z]> 'abc' /;
# a quote member inside the class must not open a string literal
@fail.push('class-assert-quote') unless Q[say "hi"] ~~ / '"' [ <!["]> . ]* '"' /;

# the shape that broke YAMLish: an indent-parameterised lookahead whose leading
# group matches zero times, feeding a `:my` var used later in the same token
grammar BlockScalar {
    token space      { <[\ \t]> }
    token line-break { \n }
    token TOP        { <body('')> }
    token body(Str $indent) {
        $<kind>=<[\|\>]> <.space>* <.line-break>
        :my $new-indent;
        <?before
            [ $indent <.space>* <.line-break> ]*
            $indent $<sp>=' '+ { $new-indent = $<sp> }
        >
        [ $indent $new-indent $<content>=[ \N* ] ]+ % <.line-break>
        <.line-break>?
    }
}
my $m = BlockScalar.parse("|\n  one\n  two\n");
@fail.push('block-scalar') unless $m && $m<body><content>.join('|') eq 'one|two';

if @fail { note "FAILED: @fail[]"; say 'FAIL' } else { say 'PASS' }
