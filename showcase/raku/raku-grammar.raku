#!/usr/bin/env raku
# A grammar of Raku, written in Raku, run by rakupp.
#
# The other showcases parse a language rakupp is not. This one parses the
# language rakupp *is*: the precedence ladder, statement forms and term shapes
# below are the ones in src/Parser.cpp, restated as a grammar instead of as a
# recursive-descent parser. Run it on rakupp's own examples/ and it agrees with
# the compiler on what is and is not Raku.
#
#   build/rakupp showcase/raku/raku-grammar.raku examples/fibonacci.raku
#   build/rakupp showcase/raku/raku-grammar.raku --tree examples/hanoi.raku
#   build/rakupp showcase/raku/raku-grammar.raku --check=examples
#
# What it is NOT: Raku is not a context-free language, and no grammar can be a
# complete one. Three things in the compiler sit outside any fixed grammar --
# `sub infix:<op>` adds terminals mid-parse, `is tighter(&infix:<+>)` computes a
# precedence level at parse time, and `use MONKEY-TYPING` gates whether
# `augment` parses at all. So this describes one member of a family: Raku with
# the built-in operator table. Two further boundaries are drawn deliberately:
# the regex sublanguage is bracket-matched rather than parsed, and heredoc
# bodies are lifted out by the driver before the grammar runs -- which is where
# rakupp's own lexer deals with them. README.md has the measured coverage.

# Furthest position any rule reached, across backtracking. A PEG reports
# failure at the outermost rule that gave up, which for a block-structured
# language is almost always the `{` -- useless for finding the construct that
# actually broke. `ws` runs at nearly every point in the grammar, so recording
# the high-water mark there gives the position a compiler would report.
my $furthest = 0;

grammar RakuGrammar {

    # ---------- whitespace, comments, pod ----------------------------------
    token ws       { [ \s+ | <.comment> | <.pod> ]*
                     { $furthest = $/.to if $/.to > $furthest } }
    token comment  { '#' <!before '`'> \N* }
    token pod      { ^^ '=begin' \h+ (\w+) .*? ^^ '=end' \h+ $0 \N* }

    # ---------- top level ---------------------------------------------------
    token TOP    { <.shebang>? <statementlist> $ }
    # Same as TOP without the end anchor: `.parse` is all-or-nothing, so the
    # driver re-runs this one with `.subparse` to find how far it got.
    token PREFIX { <.shebang>? <statementlist> }
    token shebang { '#!' \N* \n }

    token statementlist { <.ws> [ <statement> <.separator> ]* }

    # A statement ends at `;`, at a closing brace, or at end of input. A
    # statement whose last character was `}` needs no `;` -- that is the
    # `<?after '}'>` branch, and it is why `if ... { }` and `sub f { }` can be
    # followed directly by the next statement.
    token separator {
        <.ws>
        [ ';' <.ws> ]*
    }

    # ---------- statements --------------------------------------------------
    proto token statement {*}
    token statement:sym<label>   { <.label> <.ws> <statement> }
    token statement:sym<control> { <statement-control> }
    token statement:sym<use>     { <use-decl> }
    token statement:sym<decl>    { <declaration> }
    token statement:sym<expr>    { <EXPR> [ <.ws> <.statement-mod> ]* }

    token label { <.identifier> ':' <!before ':'> }

    # No trailing <.ws>: `separator` decides whether a statement ended on a
    # `}` by looking at the character right before it, and consumed whitespace
    # would hide it.
    token statement-mod {
        <statement-mod-kw> <.ws>
        [ <pointy-sig> <.ws> ]?
        <EXPR>
    }
    token statement-mod-kw {
        [ 'if' | 'unless' | 'while' | 'until' | 'for' | 'given' | 'when'
        | 'without' | 'with' ] <!before <.identchar>>
    }

    # ---------- statement controls -----------------------------------------
    proto token statement-control {*}

    token statement-control:sym<if> {
        'if' <!before <.identchar>> <.ws> <EXPR> <.ws> <block>
        [ <.ws> 'elsif' <.ws> <EXPR> <.ws> <block> ]*
        [ <.ws> 'else' <.ws> <block> ]?
    }
    token statement-control:sym<unless> {
        'unless' <!before <.identchar>> <.ws> <EXPR> <.ws> <block>
        [ <.ws> 'else' <.ws> <block> ]?
    }
    token statement-control:sym<with> {
        [ 'with' | 'without' ] <!before <.identchar>> <.ws> <EXPR> <.ws> <block>
        [ <.ws> 'orwith' <.ws> <EXPR> <.ws> <block> ]*
        [ <.ws> 'else' <.ws> <block> ]?
    }
    token statement-control:sym<while> {
        [ 'while' | 'until' ] <!before <.identchar>> <.ws> <EXPR> <.ws> <block>
    }
    token statement-control:sym<repeat> {
        'repeat' <!before <.identchar>> <.ws>
        [ <block> <.ws> [ 'while' | 'until' ] <.ws> <EXPR>
        | [ 'while' | 'until' ] <.ws> <EXPR> <.ws> <block> ]
    }
    token statement-control:sym<for> {
        [ 'for' | 'race' | 'hyper' ] <!before <.identchar>> <.ws> <EXPR> <.ws>
        [ <pointy-sig> <.ws> ]? <block>
    }
    token statement-control:sym<loop> {
        'loop' <!before <.identchar>> <.ws>
        [ '(' <.ws> <EXPR>? <.ws> ';' <.ws> <EXPR>? <.ws> ';' <.ws> <EXPR>? <.ws> ')' <.ws> ]?
        <block>
    }
    token statement-control:sym<given> {
        'given' <!before <.identchar>> <.ws> <EXPR> <.ws> <block>
    }
    token statement-control:sym<when> {
        'when' <!before <.identchar>> <.ws> <EXPR> <.ws> <block>
    }
    token statement-control:sym<default> {
        'default' <!before <.identchar>> <.ws> <block>
    }
    token statement-control:sym<react> {
        [ 'react' | 'supply' ] <!before <.identchar>> <.ws> <block>
    }
    token statement-control:sym<whenever> {
        'whenever' <!before <.identchar>> <.ws> <EXPR> <.ws> <block>
    }
    token statement-control:sym<phaser> {
        <phaser-name> <.ws> <block>
    }
    token phaser-name {
        [ 'BEGIN' | 'END' | 'INIT' | 'CHECK' | 'DOC' | 'ENTER' | 'LEAVE'
        | 'KEEP' | 'UNDO' | 'FIRST' | 'NEXT' | 'LAST' | 'PRE' | 'POST'
        | 'CATCH' | 'CONTROL' | 'CLOSE' | 'QUIT' ] <!before <.identchar>>
    }
    token statement-control:sym<bareblock> { <block> }

    # ---------- use / no / require -----------------------------------------
    token use-decl {
        [ 'use' | 'no' | 'need' | 'require' ] <!before <.identchar>> <.ws>
        [ <version> | <module-name> [ <.ws> <EXPR> ]? ]?
    }
    token module-name { <.identifier> [ '::' <.identifier> ]* }
    token version     { 'v' \d+ [ '.' [ \d+ | '*' ] ]* '+'? }

    # ---------- declarations ------------------------------------------------
    proto token declaration {*}

    token declaration:sym<package> {
        [ 'unit' <.ws> ]?
        <package-kw> <!before <.identchar>> <.ws>
        [ <module-name> <.ws> ]?
        <.trait>*
        [ <block> | ';' ]
    }
    token package-kw { 'class' | 'role' | 'grammar' | 'module' | 'package' | 'monitor' | 'knowhow' }

    token declaration:sym<augment> {
        'augment' <!before <.identchar>> <.ws> <package-kw> <.ws> <module-name> <.ws> <block>
    }

    token declaration:sym<routine> {
        [ <scope> <.ws> ]?
        [ <routine-mod> <.ws> ]*
        [ <routine-kw> <!before <.identchar>> <.ws> ]?
        [ <routine-name> <.ws> ]?
        [ <signature-paren> <.ws> ]?
        <.trait>*
        [ <block> | ';' ]
    }
    token routine-kw  { 'sub' | 'submethod' | 'method' }
    token routine-mod { [ 'multi' | 'proto' | 'only' ] <!before <.identchar>> }
    token routine-name {
        <op-name>
        | <.identifier> [ [ '::' | '-' | "'" ] <.identifier> ]*
          [ ':' [ 'sym' ]? <quote-words> ]?
    }
    # `sub infix:<op>` and `method postcircumfix:<[ ]>` -- the declaration form
    # that adds a terminal to the language. The grammar can recognise it; it
    # cannot then honour it, which is the point made in the header.
    token op-name {
        [ 'infix' | 'prefix' | 'postfix' | 'circumfix' | 'postcircumfix' | 'term' ]
        ':' [ 'sym' ]? <quote-words>
    }

    token declaration:sym<regex> {
        [ <scope> <.ws> ]?
        [ <routine-mod> <.ws> ]*
        [ 'token' | 'rule' | 'regex' ] <!before <.identchar>> <.ws>
        <routine-name> <.ws> [ <signature-paren> <.ws> ]? <.trait>*
        <regex-block>
    }

    token declaration:sym<subset> {
        [ <scope> <.ws> ]?
        'subset' <!before <.identchar>> <.ws> <module-name> <.ws>
        [ 'of' <.ws> <type-name> <.ws> ]?
        [ 'where' <.ws> <EXPR> ]?
    }

    token declaration:sym<enum> {
        [ <scope> <.ws> ]?
        'enum' <!before <.identchar>> <.ws> [ <module-name> <.ws> ]?
        [ 'of' <.ws> <type-name> <.ws> ]?
        <.trait>*
        <EXPR>
    }

    token declaration:sym<constant> {
        [ <scope> <.ws> ]?
        'constant' <!before <.identchar>> <.ws> <EXPR>
    }

    token declaration:sym<var> {
        <scope> <!before <.identchar>> <.ws>
        [ <type-name> <.ws> <?before <.sigil>> ]?
        <EXPR>
    }

    token scope { [ 'my' | 'our' | 'state' | 'has' | 'anon' | 'augment' | 'unit' ] <!before <.identchar>> }

    token trait {
        [ 'is' | 'does' | 'of' | 'returns' | 'handles' | 'where' ] <!before <.identchar>>
        <.ws> <trait-arg> <.ws>
    }
    # Deliberately tighter than a general expression: with a full expression
    # here, `is export { ... }` reads `export` as a list operator and takes the
    # routine's body as its argument, leaving the declaration with no block.
    token trait-arg { <trait-term> <postfix>* }
    token trait-term {
        '(' <.ws> <EXPR>? <.ws> ')'
        | <quote-words> | <quote> | <number> | <variable> | <type-name>
    }

    # ---------- signatures --------------------------------------------------
    token signature-paren { '(' <.ws> <signature> <.ws> ')' }
    token signature {
        <parameter>* %% [ <.ws> ',' <.ws> ]
        [ <.ws> <return-constraint> ]?
    }
    token return-constraint { '-->' <.ws> [ <type-name> | <term> ] }
    token pointy-sig      { [ '->' | '<->' ] <.ws> <signature> }

    # Non-nullable by construction: a parameter is at least a type or a name.
    # A nullable one would let `signature` match the empty string in front of
    # every real parameter, and every signature would then end at its first
    # parameter.
    token parameter {
        [ <type-name> <.ws> ]? <param-core>
        <param-quant>?
        [ <.ws> <sub-signature> ]?
        [ <.ws> <.trait> ]*
        [ <.ws> <param-default> ]?
        [ <.ws> 'where' <.ws> <ternary-expr> ]?
        | <type-name> [ <.ws> <.trait> ]*      # type-only: `sub f(Int, Pointer is rw)`
    }
    # `$x`, `*@rest`, `:$named`, `:%opts`, `:name($alias)`, `($a, $b)`, `\c`
    token param-core {
        ':' <.identifier> '(' <.ws> <signature> <.ws> ')'
        | <slurpy>? ':'? <variable>
        | <sub-signature>
        | <slurpy>? <.identifier>
    }
    token slurpy       { '**' | '*' | '|' | '\\' | '+' }
    token param-quant  { '?' | '!' }
    token param-default { [ '=' | '//=' | '||=' ] <.ws> <ternary-expr> }
    token sub-signature { '(' <.ws> <signature> <.ws> ')' }

    # ---------- blocks ------------------------------------------------------
    token block       { '{' <statementlist> <.ws> '}' }
    token regex-block { '{' <.balanced-regex> '}' }

    # The regex sublanguage is a language of its own; this grammar brackets it
    # rather than describing it. Quotes and character classes are respected so
    # that a `}` inside `'...'` or `<[...]>` does not close the block early.
    token balanced-regex {
        [ <.regex-atom> ]*
    }
    token regex-atom {
        <-[{}'"\\\[\]<>]>+
        | '\\' .
        | "'" [ <-['\\]> | '\\' . ]* "'"
        | '"' [ <-["\\]> | '\\' . ]* '"'
        | <.regex-angle>                  # <ident>, <[a..z]>, <!before <[=>]>>
        | '[' [ <.regex-atom> ]* ']'
        | '{' [ <.regex-atom> ]* '}'
        | <[<>]>
    }
    # Assertions nest, and may quote the bracket characters themselves:
    # `<!before <[=>]>>` contains another assertion, and `<!before '>'>`
    # contains a `>` that does not close it.
    token regex-angle {
        '<' [ <-[<>'"\[\]]>+
            | '[' [ <-[\]\\]> | '\\' . ]* ']'   # a character class: opaque, but `\]` is a member
            | "'" <-[']>* "'"           # a quoted literal: `<!before '>'>`
            | '"' <-["]>* '"'
            | <.regex-angle>            # a nested assertion
            ]*
        '>'
    }

    # ---------- the precedence ladder ---------------------------------------
    # Loosest to tightest, mirroring the BP_* levels in src/Parser.cpp.
    # Each level is `<tighter>+ % <operators at this level>`, so the grammar
    # says the same thing the binding-power table says.

    token EXPR { <loose-or-expr> }

    # BP_OR = 10
    token loose-or-expr  { <loose-and-expr>+  % [ <.ws> <loose-or-op>  <.ws> ] }
    token loose-or-op    { [ 'or' | 'xor' | 'orelse' ] <!before <.identchar>> | '==>' | '<==' }

    # BP_AND = 20
    token loose-and-expr { <listinfix-expr>+  % [ <.ws> <loose-and-op> <.ws> ] }
    token loose-and-op   { [ 'and' | 'andthen' | 'notandthen' ] <!before <.identchar>> }

    # BP_ZIP = 25 -- the list infixes, looser than comma
    token listinfix-expr { <comma-expr>+ % [ <.ws> <listinfix-op> <.ws> ] }
    token listinfix-op {
        '...^' | '^...^' | '^...' | '...'
        | [ 'minmax' | 'Z' | 'X' ] <!before <.identchar>>
        | <[ZX]>+ <.infix-word>
        | <[ZX]>+ <.symbolic-op>
    }

    # BP_COMMA = 30
    # `%%` not `%`: a trailing comma is legal, and `($caps,)` is how a
    # one-element list is written.
    token comma-expr { <assign-expr>+ %% [ <.ws> [ ',' | ':' <?before \s> ] <.ws> ] }

    # BP_ASSIGN = 40, right-associative
    token assign-expr {
        <ternary-expr> [ <.ws> <assign-op> <.ws> <assign-expr> ]?
    }
    # kAssignOps in src/Parser.cpp, plus `=>` -- which the compiler classifies
    # as its own token kind but hands the same BP_ASSIGN level.
    token assign-op {
        '=>'
        | '**=' | '//=' | '||=' | '&&=' | '^^=' | '::=' | ':=' | '.='
        | '=$=' | '=@=' | '=%='
        | '+=' | '-=' | '*=' | '/=' | '~=' | '%=' | 'x='
        | [ 'div' | 'mod' | 'gcd' | 'lcm' | 'xx' | 'min' | 'max' ] '='
        | '+|=' | '+&=' | '+^=' | '~|=' | '~&=' | '~^=' | '%%='
        | 'R='
        | '=' <!before '='|'>'|'~'>
    }

    # BP_TERNARY = 50
    token ternary-expr {
        <oror-expr> [ <.ws> '??' <.ws> <ternary-expr> <.ws> '!!' <.ws> <ternary-expr> ]?
    }

    # BP_OROR = 60
    token oror-expr   { <andand-expr>+ % [ <.ws> <oror-op> <.ws> ] }
    token oror-op     { '||' <!before '='> | '//' <!before '='> | '^^' }

    # BP_ANDAND = 70
    token andand-expr { <compare-expr>+ % [ <.ws> '&&' <!before '='> <.ws> ] }

    # BP_COMPARE = 80 -- chaining, as in the compiler
    token compare-expr { <range-expr>+ % [ <.ws> <compare-op> <.ws> ] }
    token compare-op {
        '<=>' | '==' <!before '>'> | '!=' | '<=' | '>=' | '<' <!before '<'> | '>' <!before '>'>
        | '~~' | '!~~' | '=:=' | '!=:=' | '===' | '!==' | '!===' | '=~=' | '≅'
        | <.set-compare-op>
        | [ 'eq' | 'ne' | 'lt' | 'gt' | 'le' | 'ge' | 'cmp' | 'leg' | 'eqv'
          | 'before' | 'after' | 'unicmp' | 'coll' ] <!before <.identchar>>
    }
    token set-compare-op {
        '(elem)' | '∈' | '(!elem)' | '∉' | '(cont)' | '∋' | '(!cont)' | '∌'
        | '(<=)' | '⊆' | '(<)' | '⊂' | '(>=)' | '⊇' | '(>)' | '⊃'
        | '(==)' | '(!=)' | '(<>)' | '≼' | '≽'
    }

    # BP_RANGE = 90
    token range-expr { <concat-expr>+ % [ <.ws> <range-op> <.ws> ] }
    token range-op   { '^..^' | '..^' | '^..' | '..' <!before '.'> | '∘' | 'o' <!before <.identchar>> }

    # BP_CONCAT = 100
    token concat-expr { <replicate-expr>+ % [ <.ws> '~' <!before '~'|'='> <.ws> ] }

    # BP_REPLICATE = 110
    token replicate-expr { <additive-expr>+ % [ <.ws> <replicate-op> <.ws> ] }
    token replicate-op   { [ 'xx' | 'x' ] <!before <.identchar>|'='> }

    # BP_ADD = 120
    token additive-expr { <multiplicative-expr>+ % [ <.ws> <additive-op> <.ws> ] }
    token additive-op {
        '+|' | '+^' | '~|' | '~^' | '?|' | '?^'
        | '+' <!before '='|'|'|'&'|'^'|'<'|'>'>
        | '-' <!before '='|'-'|'>'>
        | '|' <!before '|'|'='> | '&' <!before '&'|'='> | '^' <!before '^'|'='>
        | [ 'min' | 'max' ] <!before <.identchar>>
        | <.set-combine-op>
    }
    token set-combine-op {
        '(|)' | '∪' | '(&)' | '∩' | '(-)' | '∖' | '(^)' | '⊖'
        | '(+)' | '⊎' | '(.)' | '⊍'
    }

    # BP_MUL = 130
    token multiplicative-expr { <power-expr>+ % [ <.ws> <multiplicative-op> <.ws> ] }
    token multiplicative-op {
        '%%' | '!%%' | '+&' | '~&' | '?&' | '+<' | '+>' | '~<' | '~>'
        | '*' <!before '*'|'='> | '/' <!before '/'|'='> | '%' <!before '%'|'='>
        | [ 'div' | 'mod' | 'gcd' | 'lcm' | 'does' | 'but' ] <!before <.identchar>>
    }

    # BP_POW = 140, right-associative
    token power-expr { <prefix-expr> [ <.ws> '**' <!before '='> <.ws> <power-expr> ]? }

    # Hyper/reduce metaoperators take the precedence of the operator inside
    # them, which a fixed ladder cannot express; they are accepted at the
    # additive level the compiler falls back to for an unknown base.
    token infix-word   { [ 'cmp'|'leg'|'eqv'|'eq'|'ne'|'lt'|'gt'|'le'|'ge'|'before'|'after'
                         | 'unicmp'|'coll'|'min'|'max'|'minmax'|'gcd'|'lcm'|'div'|'mod'
                         | 'x'|'xx'|'and'|'or'|'andthen'|'orelse'|'but'|'does' ] <!before <.identchar>> }
    token symbolic-op  { [ <[+\-*/%~.,|&^?!<>=]> | <:Sm> | <:So> ]+ }

    # ---------- prefix / postfix -------------------------------------------
    token prefix-expr {
        [ <prefix-op> <.ws> ]* <postfix-expr>
    }
    token prefix-op {
        '++' | '--' | '+^' | '~^' | '?^'
        | '-' <!before '-'|'>'> | '+' <!before '+'> | '~' <!before '~'>
        | '?' <!before '?'> | '!' <!before '!'|'='> | '^' <!before '^'>
        | '|' <!before '|'> | '\\'
        | [ 'not' | 'so' | 'defined' | 'temp' | 'let' ] <!before <.identchar>>
    }

    # A `.method` may sit on its own line to continue a chain, so whitespace is
    # allowed before the dotted postfixes. The bracketing ones are postcircumfix
    # and must be tight: `@a [1]` is not an index.
    token postfix-expr { <term> [ <postfix> | <.ws> <dotted-postfix> ]* }

    proto token postfix {*}
    proto token dotted-postfix {*}
    token dotted-postfix:sym<method> { <postfix:sym<methodcall>> }
    token dotted-postfix:sym<hyper>  { <postfix:sym<hyper>> }
    token dotted-postfix:sym<idx>    { <postfix:sym<dotidx>> }

    token postfix:sym<methodcall> {
        '.' <.ws> [ <method-op> ]? <method-name>
        [ <call-args> | <method-colon-args> | <colonpair-args> ]?
    }
    token method-op    { '^' | '?' | '+' | '*' | '=' | '&' }
    token method-name  { <.identifier> [ [ '::' | '-' | "'" ] <.identifier> ]* | <variable> | <quote-words> }
    token postfix:sym<hyper> {
        [ '»' | '>>' ] '.'? [ <method-op> ]? <method-name> <call-args>?
    }
    token postfix:sym<privatecall> { '!' <.identifier> <call-args>? }
    token postfix:sym<index>   { '[' <.ws> <EXPR>? <.ws> ']' }
    token postfix:sym<key>     { '{' <.ws> <EXPR>? <.ws> '}' }
    token postfix:sym<angle>   { <quote-words> }
    token postfix:sym<call>    { '(' <.ws> <EXPR>? <.ws> ')' }
    token postfix:sym<dotidx>  { '.' [ '[' <.ws> <EXPR>? <.ws> ']' | '{' <.ws> <EXPR>? <.ws> '}' | '(' <.ws> <EXPR>? <.ws> ')' ] }
    token postfix:sym<adverb>  { <colonpair> }
    token postfix:sym<incr>    { '++' | '--' }
    token postfix:sym<bang>    { '!' <!before '!'|'='> }

    token call-args        { '(' <.ws> <EXPR>? <.ws> ')' }
    token method-colon-args { ':' <?before \h> <.ws> <listinfix-expr> }
    token colonpair-args { [ <.ws> <colonpair> ]+ }

    # ---------- terms -------------------------------------------------------
    proto token term {*}

    token term:sym<dotmethod> { '.' <!before '.'> [ <method-op> ]? <method-name>
                                [ <call-args> | <method-colon-args> ]? }
    token term:sym<dotpost>   { '.' <?before '['|'{'|'<'>
                                [ '[' <.ws> <EXPR>? <.ws> ']'
                                | '{' <.ws> <EXPR>? <.ws> '}'
                                | <quote-words> ] }
    token term:sym<vardecl>  { <scope> <!before <.identchar>> <.ws>
                               [ <type-name> <.ws> <?before <.sigil>> ]?
                               [ <variable> | '(' <.ws> <EXPR>? <.ws> ')' ] }
    token term:sym<opref>    { '&' '[' <.ws> [ <.symbolic-op> | <.infix-word> ] <.ws> ']' }
    token term:sym<whatever> { '**' | '*' <!before '*'> }
    token term:sym<num>      { <number> }
    token term:sym<string>   { <quote> }
    token term:sym<regex>    { <regex-literal> }
    token term:sym<var>      { <variable> }
    token term:sym<colonpair> { <colonpair> }
    token term:sym<paren>    { '(' <.ws> <EXPR>? <.ws> ')' }
    token term:sym<array>    { '[' <.ws> <EXPR>? <.ws> ']' }
    token term:sym<reduce>   { '[' [ '\\' | 'R' | 'S' ]? <.reduce-inner> ']'
                               [ <.ws> <listinfix-expr> ]? }
    token term:sym<hashblock> { <hash-or-block> }
    token term:sym<pointy>   { <pointy-sig> <.ws> <block> }
    token term:sym<lambda>   { 'sub' <!before <.identchar>> <.ws> <signature-paren>? <.ws> <.trait>* <block> }
    token term:sym<contextual> { <[$@%&]> [ '(' <.ws> <EXPR>? <.ws> ')' | '<' <-[>\n]>* '>' ] }
    token term:sym<capture>  { '\\' [ '(' <.ws> <EXPR>? <.ws> ')' | <variable> ] }
    token term:sym<do>       { [ 'do' | 'try' | 'start' | 'gather' | 'once' | 'quietly' | 'lazy' | 'eager' | 'hyper' | 'race' | 'sink' | 'supply' ] <!before <.identchar>> <.ws> [ <block> | <statement> ] }
    token term:sym<self>     { [ 'self' | 'now' | 'time' | 'rand' | 'True' | 'False' | 'Nil' | 'Any' | 'Mu' | 'Inf' | 'NaN' ] <!before <.identchar>> }
    # `<!before '{'>`: without it `if False { ... }` reads `False` as a list
    # operator and the block becomes its argument, leaving the `if` with no
    # body. The compiler settles this by knowing `False` is a term; a grammar
    # has to say that a bare block never starts a list operator's arguments.
    token term:sym<listop>   { <listop-name> <?before \h> <.ws>
                               <!before <.infix-only>> <!before '{'>
                               <listinfix-expr> }
    token term:sym<name>     { <type-name> }

    token reduce-inner { [ <.symbolic-op> | <.infix-word> ] }

    # A bare name applied to arguments with no parentheses: `say $x, $y`.
    # The `<!before ...>` guard keeps a word-form infix (`eq`, `x`, `and`) from
    # being read as the start of a call.
    token listop-name  { <!before <.reserved>> <.identifier> [ [ '::' | '-' ] <.identifier> ]* }
    token infix-only   { [ <.infix-word> | 'if' | 'unless' | 'while' | 'until' | 'for' | 'given'
                         | 'when' | 'with' | 'without' ] <!before <.identchar>> }
    token reserved     { [ 'if' | 'unless' | 'while' | 'until' | 'for' | 'given' | 'when'
                         | 'default' | 'repeat' | 'loop' | 'sub' | 'method' | 'class' | 'role'
                         | 'grammar' | 'my' | 'our' | 'state' | 'has' | 'use' | 'no'
                         | 'multi' | 'proto' | 'token' | 'rule' | 'regex' | 'else' | 'elsif' ]
                         <!before <.identchar>> }

    token type-name {
        [ '::' ]? <.identifier> [ '::' <.identifier> ]*
        [ '[' <.ws> <EXPR> <.ws> ']' ]?          # parametrised: Array[Int]
        [ '(' <.ws> <type-name>? <.ws> ')' ]?    # coercion: IO(), Int(Str)
        [ ':' <[DU_]> <!before <.identchar>> ]?  # smiley: Int:D
    }

    # ---------- block vs hash ----------------------------------------------
    # `{ }` is a term here, and the compiler's rule decides which: empty, or a
    # first element that is a pair or a `%`-sigilled variable, makes it a hash;
    # anything else is a block.
    token hash-or-block {
        '{' <.ws>
        [ '}'                                                    # {} -- empty hash
        | <?before [ <.colonpair> | '%' | <.hash-key> ] > <hash-body> '}'
        | <statementlist> <.ws> '}'
        ]
    }
    token hash-key  { [ <.identifier> | <.quote> ] <.ws> [ '=>' | ',' ] }
    token hash-body { <.ws> <EXPR>? <.ws> }

    # ---------- variables ---------------------------------------------------
    token variable {
        <sigil> <twigil>? <varname>
        | <sigil> <?before '['|'{'|'<'>            # $ @ % as anonymous state var
        | <sigil>
    }
    token sigil   { '$' | '@' | '%' | '&' }
    token twigil  { <[.!^:*?=~<]> }
    token varname {
        <.identifier> [ [ '::' | '-' | "'" ] <.identifier> ]*
        | <[/_!0..9]>
    }

    # ---------- colon pairs -------------------------------------------------
    token colonpair {
        ':' [ '!' <.identifier>
            | <.identifier> [ '(' <.ws> <EXPR> <.ws> ')' | <quote-words> | <quote> ]?
            | <variable>
            | \d+ [ '(' <.ws> <EXPR> <.ws> ')' | <.identifier> ]
            ]
    }

    # ---------- literals ----------------------------------------------------
    token number {
        [ '0' <[xbodXBOD]> <[0..9a..fA..F_]>+
        | ':' \d+ '<' <-[>]>+ '>'
        | \d [ \d | '_' ]* [ '.' \d [ \d | '_' ]* ]? [ <[eE]> <[+-]>? \d+ ]?
        | '.' \d [ \d | '_' ]*
        ]
        [ 'i' <!before <.identchar>> ]?
    }

    proto token quote {*}
    token quote:sym<single> { "'" [ <-['\\]> | '\\' . ]* "'" }
    token quote:sym<double> { '"' [ <-["\\]> | '\\' . ]* '"' }
    token quote:sym<words>  { <quote-words> }
    token quote:sym<q> {
        [ 'qqww' | 'qqw' | 'qww' | 'qw' | 'qq' | 'q' | 'Q' ] <!before <.identchar>>
        [ ':' <.identifier> [ '(' <-[)]>* ')' ]? ]*
        <.ws> <bracketed>
    }
    # Only the introducer. A heredoc's body starts after the *statement* ends,
    # so its extent is not a function of the text at this point -- it is the
    # canonical piece of Raku that no grammar can describe. The driver lifts
    # bodies out first, which is where rakupp's lexer handles it too.
    token quote:sym<heredoc> {
        [ 'qq' | 'q' | 'Q' ] [ ':' <.identifier> ]* ':to'
        [ '/' <-[/\n]>+ '/' | "'" <-['\n]>+ "'" | '|' <-[|\n]>+ '|' ]
    }
    # A word list spans lines only when the `<` ends its line -- the shape the
    # multi-line form actually takes. Allowed to span lines unconditionally, a
    # `<` becomes a scanner for the next `>` anywhere in the file, and
    # `rand < DENSITY` swallows everything up to the `>` of a later `-> $x`.
    # The multi-line form excludes `;` and braces. Allowed to span lines
    # unconditionally, a `<` becomes a scanner for the next `>` anywhere in the
    # file: `rand < DENSITY` then swallows everything up to the `>` of a later
    # `-> $x`. A word list contains no statement separators; a runaway scan
    # across statements hits one almost immediately.
    token quote-words {
        '<<' <-[<>\n]>* '>>'
        | '«' <-[«»]>* '»'
        | '<' <-[<>\n]>* '>'          # single-line: anything but the brackets
        | '<' <-[<>;{}]>* '>'          # multi-line: no statement separators
    }

    token bracketed {
        '(' [ <-[()\\]> | '\\' . | <.bracketed> ]* ')'
        | '[' [ <-[\[\]\\]> | '\\' . | <.bracketed> ]* ']'
        | '{' [ <-[{}\\]> | '\\' . | <.bracketed> ]* '}'
        | '<' [ <-[<>\\]> | '\\' . | <.bracketed> ]* '>'
        | '/' [ <-[/\\]> | '\\' . ]* '/'
        | '|' [ <-[|\\]> | '\\' . ]* '|'
        | '!' [ <-[!\\]> | '\\' . ]* '!'
    }

    # A `/` starts a regex only where a term is expected -- which is exactly
    # where this rule is reachable. At infix position the ladder has already
    # taken `/` as division, so the ambiguity never arises.
    token regex-literal {
        [ 'rx' | 'm' | 's' | 'S' | 'tr' | 'TR' ] <!before <.identchar>>
        [ ':' <.identifier> [ '(' <-[)]>* ')' ]? ]*
        <.ws> <.subst-body>
        | '/' <.regex-body> '/'
    }
    token subst-body {
        '/' <.regex-body> '/' [ <.regex-body> '/' ]?
        | <.bracketed> [ <.ws> <.bracketed> ]?
    }
    # A quoted literal inside the pattern may hold the delimiter itself, as in
    # `subst(/ '/' $ /, '')`, so quotes are recognised before the delimiter is.
    token regex-body {
        [ <-[/\\\['"]>
        | '\\' .
        | "'" <-[']>* "'"
        | '"' <-["]>* '"'
        | '[' [ <-[\]\\]> | '\\' . ]* ']'
        ]*
    }

    # ---------- lexical atoms -----------------------------------------------
    token identifier { <.identstart> <.identchar>* }
    token identstart { <[A..Za..z_]> | <:L> }
    token identchar  { <[A..Za..z0..9_]> | <:L> | <:N> }
}

# ---------- driver ---------------------------------------------------------

# Where a parse stops. `.parse` is all-or-nothing, so on failure we re-run with
# `.subparse` to find how far the grammar got, and turn that offset into a
# line/column the way a compiler error would report it.
sub stop-point(Str $src, Int $pos) {
    my $before = $src.substr(0, $pos);
    my $line   = $before.comb("\n").elems + 1;
    my $col    = $pos - ($before.rindex("\n") // -1);
    my $rest   = $src.substr($pos).lines[0] // '';
    return ($line, $col, $rest.trim.substr(0, 48));
}

# A heredoc body begins after the statement that introduces it ends, so its
# extent is not determined by the text where the `q:to/TAG/` appears. rakupp's
# lexer resolves that before the parser runs; so does this. Bodies are replaced
# by blank lines rather than deleted, so reported line numbers stay true.
sub strip-heredocs(Str $src) {
    my @lines = $src.lines;
    my @out;
    my $i = 0;
    while $i < @lines.elems {
        my $line = @lines[$i];
        @out.push: $line;
        $i++;
        # every introducer on this line, in order -- one line may open two
        my @tags = ($line ~~ m:g/ <[qQ]> \w* ':to' [ '/' $<t>=(<-[/]>+) '/' | "'" $<t>=(<-[']>+) "'" ] /)
                   .map({ ~.<t> });
        for @tags -> $tag {
            @out.push: '';                      # keep the line count honest
            while $i < @lines.elems && @lines[$i].trim ne $tag {
                @out.push: '';
                $i++;
            }
            $i++ if $i < @lines.elems;          # the terminator line itself
        }
    }
    return @out.join("\n") ~ "\n";
}

# Every .raku/.rakumod under a directory, recursively.
sub find-raku(IO::Path $dir) {
    my @out;
    for $dir.dir.sort -> $e {
        if $e.d {
            @out.append: find-raku($e);
        }
        elsif $e.extension eq 'raku' | 'rakumod' | 'rakutest' {
            @out.push: $e.Str;
        }
    }
    return @out;
}

sub check-one(Str $path) {
    my $src = strip-heredocs($path.IO.slurp);
    $furthest = 0;
    if RakuGrammar.parse($src) {
        return { :ok, :$path, :lines($src.lines.elems) };
    }
    my $p = RakuGrammar.subparse($src, :rule<PREFIX>);
    my $at = (($p ?? $p.to !! 0), $furthest).max;
    my ($line, $col, $rest) = stop-point($src, $at);
    return { :!ok, :$path, :lines($src.lines.elems), :$line, :$col, :$rest };
}

# The named captures under one match, in source order, flattened so a rule
# that matched several times contributes each of them.
sub child-matches($m) {
    my @kids;
    for $m.keys -> $k {
        my $v = $m{$k};
        next unless $v;
        for ($v ~~ Positional ?? @$v !! ($v,)) -> $kid {
            @kids.push: $k => $kid if $kid;
        }
    }
    return @kids.sort({ .value.from });
}

# The ladder is fifteen levels deep, and an expression that uses none of them
# passes through every one. A level whose single child spans exactly the same
# text adds no information, so it is collapsed; what is left is the structure
# the operators actually imposed.
sub show-tree($name, $m, Int $depth = 0) {
    return if $depth > 60;
    my @kids = child-matches($m);
    if @kids == 1 && @kids[0].value.Str eq $m.Str {
        show-tree(@kids[0].key, @kids[0].value, $depth);
        return;
    }
    my $text = $m.Str.trim.subst(/\s+/, ' ', :g);
    $text = $text.chars > 52 ?? $text.substr(0, 52) ~ '…' !! $text;
    say '  ' x $depth ~ $name ~ '  ' ~ $text;
    show-tree(.key, .value, $depth + 1) for @kids;
}

#| Parse Raku source with a grammar of Raku written in Raku.
sub MAIN(
    Str $file?,               #= a .raku/.rakumod file to parse
    Bool :$tree,              #= print the parse tree
    Str  :$check,             #= parse every .raku file in a directory
) {
    if $check {
        my @files = find-raku($check.IO).sort;
        my @results = @files.map({ check-one(.Str) });
        my $ok = @results.grep(*<ok>).elems;
        for @results -> $r {
            if $r<ok> {
                say sprintf('  ok    %-34s %4d lines', $r<path>.IO.basename, $r<lines>);
            }
            else {
                say sprintf('  FAIL  %-34s stopped %d:%d  %s',
                            $r<path>.IO.basename, $r<line>, $r<col>, $r<rest>);
            }
        }
        say '';
        say "$ok/{@results.elems} files parsed";
        exit $ok == @results.elems ?? 0 !! 1;
    }

    unless $file {
        note 'usage: raku-grammar.raku [--tree] FILE  |  --check=DIR';
        exit 2;
    }

    my $src = strip-heredocs($file.IO.slurp);
    $furthest = 0;
    my $m = RakuGrammar.parse($src);
    unless $m {
        my $p = RakuGrammar.subparse($src, :rule<PREFIX>);
        my ($line, $col, $rest) = stop-point($src, (($p ?? $p.to !! 0), $furthest).max);
        note "no parse: stopped at $file:$line:$col";
        note "  $rest";
        exit 1;
    }
    if $tree {
        show-tree('TOP', $m);
    }
    else {
        say "parsed $file ({$src.lines.elems} lines)";
    }
}
