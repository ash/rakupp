# A slang that exercises every seam rakupp implements (docs/dev/plans/SLANG-PLAN.md),
# registered through `$*LANG.define_slang` directly — no Slangify — and carrying
# both role flavours, so the same fixture runs under Rakudo's legacy grammar and
# under rakupp's `Raku::Grammar`. The two Tuxic productions are H.Merijn Brand's,
# from Slang::Tuxic 0.0.5 (Artistic-2.0), verbatim.

# `$<n>` on an NQP match, as Slangify provides it for the legacy actions
multi sub postcircumfix:<{ }>(Mu $/, Str:D $key) { $/.hash.AT-KEY($key) }
multi sub prefix:<~>(Mu $/) { $/.Str }

my role Seams::Grammar {
    token number:sym<bin>  { '0y' <( <[01]>+ }              # `0y101` is 5: the token's own capture marker
    token number:sym<subs> { <[₀₁₂₃₄₅₆₇₈₉]>+ }              # `₅₅`: a construct the lexer refuses without the slang
    token value:sym<pct>   { \d+ )> '%' }                   # `50%`: the capture ends early, matching does not
    token identifier { <.ident> [ <.apostrophe> <.ident> ]* <[?!]>? }   # Slang::Piersing
    token sigilless-variable { <.:So> }                     # Slang::Emoji
    token pointy-block-starter { '->' | '<->' | 'λ' }       # Slang::Lambda
    token routine-declarator:sym<sub> {                     # Slang::Mosdef
        [ <.routine-sub> | lambda ] <.end-keyword>
        <routine-def=.key-origin('routine-def', 'sub')>
    }
    token term:sym<identifier> {                            # Slang::Tuxic
        :my $pos;
        <identifier>
        <!{
            my $ident = ~$<identifier>;
            $ident eq 'sub'|'lambda'|'if'|'elsif'|'while'|'until'|'for'
              || $*R.is-identifier-type([$ident])
        }>
        <?before <.unspace>|\s*'('> \s* <![:]>
        { $pos := $/.CURSOR.pos }
        <args>
    }
    token methodop(Mu $*DOTTY) {                            # Slang::Tuxic
        [
          | <longname>
            {
                self.malformed("class-qualified postfix call")
                  if ~$<longname> eq '::';
            }
          | <?[$@&]>
            <variable>
            { self.check-variable($<variable>) }
          | <?['"]>
            [ <!{$*QSIGIL}> || <!before '"' <-["]>*? [\s|$] > ]
            <quote>
            [ <?before '(' | '.(' | '\\'>
                || <.panic: "Quoted method name requires parenthesized arguments. If you meant to concatenate two strings, use '~'.">
            ]
            <.dotty-non-ident($*DOTTY)>
        ] \s* <.unspace>?
        [
          [
            |  <?before  \s*'('>  \s* <args>
            | ':' <?before \s | '{'> <!{ $*QSIGIL }> <args=.arglist>
          ]
          || <!{ $*QSIGIL }> <?>
          || <?{ $*QSIGIL }> <?[.]> <?>
        ] <.unspace>?
    }
}

my role Seams::Actions {
    method number:sym<bin>(Mu $/)  { use experimental :rakuast; make RakuAST::IntLiteral.new($/.Str.parse-base(2)) }
    method number:sym<subs>(Mu $/) { use experimental :rakuast; make RakuAST::IntLiteral.new($/.Str.trans('₀₁₂₃₄₅₆₇₈₉' => '0123456789').Int) }
    method value:sym<pct>(Mu $/)   { use experimental :rakuast; make RakuAST::IntLiteral.new($/.Str.Int) }
}

my role Seams::Legacy {
    use NQPHLL:from<NQP>;
    token number:sym<bin>  { '0y' <( <[01]>+ }
    token number:sym<subs> { <[₀₁₂₃₄₅₆₇₈₉]>+ }
    token value:sym<pct>   { \d+ )> '%' }
    token identifier { <.ident> [ <.apostrophe> <.ident> ]* <[?!]>? }
    token lambda { '->' | '<->' | 'λ' }
    rule routine_declarator:sym<sub> { [ 'sub' | 'lambda' ] <routine_def('sub')> }
    token term:sym<identifier> {
        :my $pos;
        <identifier>
        <!{
            my $ident = ~$<identifier>;
            $ident eq 'sub'|'lambda'|'if'|'elsif'|'while'|'until'|'for' || $*W.is_type([$ident])
        }>
        <?before <.unsp>|\s*'('> \s* <![:]>
        { $pos := $/.CURSOR.pos }
        <args>
        {
            self.add_mystery(
              $<identifier>, $<args>.from, $<args>.Str.substr(0,1)
            )
        }
    }
    token methodop {
        [
          | <longname>
          | <?[$@&]>
            <variable>
            { self.check_variable($<variable>) }
          | <?['"]>
            [ <!{$*QSIGIL}> || <!before '"' <-["]>*? [\s|$] > ]
            <quote>
            [ <?before '(' | '.(' | '\\'>
                || <.panic: "Quoted method name requires parenthesized arguments. If you meant to concatenate two strings, use '~'.">
            ]
        ] \s* <.unsp>?
        [
          [
            |  <?before  \s*'('>  \s* <args>
            | ':' <?before \s | '{'> <!{ $*QSIGIL }> <args=.arglist>
          ]
          || <!{ $*QSIGIL }> <?>
          || <?{ $*QSIGIL }> <?[.]> <?>
        ] <.unsp>?
    }
}

my role Seams::LegacyActions {
    method number:sym<bin>(Mu $/)  { use QAST:from<NQP>; make QAST::IVal.new(:value($/.Str.parse-base(2))) }
    method number:sym<subs>(Mu $/) { use QAST:from<NQP>; make QAST::IVal.new(:value($/.Str.trans('₀₁₂₃₄₅₆₇₈₉' => '0123456789').Int)) }
    method value:sym<pct>(Mu $/)   { use QAST:from<NQP>; make QAST::IVal.new(:value($/.Str.Int)) }
}

sub EXPORT() {
    my $LANG := $*LANG;
    my \legacy = !$LANG.^name.starts-with('Raku::');
    $LANG.define_slang('MAIN',
      $LANG.slang_grammar('MAIN').^mixin(legacy ?? Seams::Legacy !! Seams::Grammar),
      $LANG.slang_actions('MAIN').^mixin(legacy ?? Seams::LegacyActions !! Seams::Actions));
    Map.new
}
