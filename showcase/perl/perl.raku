#!/usr/bin/env raku
# A Perl 5 interpreter. A Raku grammar parses a practical slice of Perl into an
# AST, and a tree-walking evaluator runs it. This is Raku parsing its own
# ancestor: the sigil variables ($ @ %), context (scalar vs list), string
# interpolation, statement modifiers and regexes that Raku grew out of.
#
#   build/rakupp showcase/perl/perl.raku showcase/perl/examples/fizzbuzz.pl
#   build/rakupp showcase/perl/perl.raku --ast=program.pl     # dump the AST
#   build/rakupp showcase/perl/perl.raku                      # REPL
#
# Scalars carry Perl's number/string duality; `.` concatenates and `x` repeats;
# arrays and hashes flatten in list context and count in scalar context. The
# hard, undecidable corners of Perl are out of scope: references and nested data
# structures, prototypes, typeglobs, `tie`, source filters, and the parse-time
# `/`-divide-vs-regex ambiguity (a bare `/` is always division here; use `m//`).
# See README.md for the exact subset.

# ---------- values ------------------------------------------------------
# Perl's undef is a distinct singleton, not Raku's Nil.
class PerlUndef { }
my \UNDEF = PerlUndef.new;

sub is-undef($v --> Bool) { $v ~~ PerlUndef }

# ---------- control-flow exceptions -------------------------------------
class RetX  is Exception { has $.value }     # return (carries a list)
class LastX is Exception { }                  # last
class NextX is Exception { }                  # next
class DieX  is Exception { has $.msg }         # die

sub perl-die(Str $msg) { DieX.new(msg => $msg).throw }

# ---------- environment -------------------------------------------------
# Perl keeps $x, @x and %x in separate slots, so variables are keyed by their
# full sigil name ('$x', '@x', '%x'). Arrays/hashes store the live container so
# element access mutates in place.
class Env {
    has %.vars;
    has $.parent;
    method declare($name, $val) { %!vars{$name} = $val }
    method put($name, $val)     { %!vars{$name} = $val }   # mutate own slot (public accessor is RO)
    method lookup($name) {
        my $e = self;
        while $e.defined {
            return $e if $e.vars{$name}:exists;
            $e = $e.parent;
        }
        Nil
    }
    method get($name) {
        my $e = self.lookup($name);
        $e.defined ?? $e.vars{$name} !! Nil;
    }
    method set($name, $val) {
        my $e = self.lookup($name);
        if $e.defined { $e.put($name, $val) }
        else          { %!vars{$name} = $val }   # autovivify at current scope
    }
}

# ---------- coercions ---------------------------------------------------
sub to-num($v) {
    return 0 if is-undef($v);
    return $v if $v ~~ Numeric;
    return $v ?? 1 !! 0 if $v ~~ Bool;
    my $s = ~$v;
    # Perl reads a leading numeric prefix and ignores the rest.
    if $s ~~ /^ \s* ('-'|'+')? [ \d+ ['.' \d*]? | '.' \d+ ] [ <[eE]> <[+-]>? \d+ ]? / {
        my $m = ~$/;
        return $m.contains('.') || $m.lc.contains('e') ?? +$m !! $m.Int;
    }
    return 0;
}

sub to-str($v) {
    return '' if is-undef($v);
    return $v ?? '1' !! '' if $v ~~ Bool;
    if $v ~~ Numeric {
        return $v.Str if $v ~~ Int;
        return $v.Int.Str if $v == $v.floor && $v.abs < 1e15;
        return sprintf('%.15g', $v.Num);      # Perl's default float stringification
    }
    ~$v;
}

sub truthy($v --> Bool) {
    return False if is-undef($v);
    return $v if $v ~~ Bool;
    if $v ~~ Numeric { return $v != 0 }
    my $s = ~$v;
    $s ne '' && $s ne '0';
}

# ---------- comment stripping -------------------------------------------
# A custom `ws` token now works, but keeping comments out of the grammar is
# simpler and matches how the other showcases do it. Blank `#`-comments to
# end of line, but never inside a string or a regex delimiter.
sub strip-comments(Str $src --> Str) {
    my @c = $src.comb;
    my $out = '';
    my $i = 0;
    my $n = @c.elems;
    while $i < $n {
        my $c = @c[$i];
        if $c eq '#' {
            # skip to end of line
            while $i < $n && @c[$i] ne "\n" { $i++ }
        }
        elsif $c eq '"' || $c eq "'" {
            my $q = $c;
            $out ~= $c; $i++;
            while $i < $n && @c[$i] ne $q {
                if @c[$i] eq '\\' && $i + 1 < $n { $out ~= @c[$i]; $i++ }
                $out ~= @c[$i]; $i++;
            }
            $out ~= @c[$i] if $i < $n;
            $i++;
        }
        else {
            $out ~= $c; $i++;
        }
    }
    $out;
}

# ---------- grammar -----------------------------------------------------
grammar PerlGrammar {
    token ws { <!ww> [ \s | '#' \N* ]* }
    token kw($k) { $k <!before <[A..Za..z0..9_]>> }
    token idlike { <[A..Za..z_]> <[A..Za..z0..9_]>* }

    rule TOP { <.ws> <statement>* }

    rule block { '{' <statement>* '}' }

    # ----- statements -----
    proto rule statement {*}
    rule statement:sym<empty>  { ';' }
    rule statement:sym<sub>    { <.kw('sub')> <idlike> [ '(' <-[)]>* ')' ]? <block> }
    rule statement:sym<my>     { <modified> }
    rule statement:sym<if>     { <.kw('if')> '(' <expr> ')' <block> <elsif>* <elsec>? }
    rule statement:sym<unless> { <.kw('unless')> '(' <expr> ')' <block> <elsec>? }
    rule statement:sym<while>  { <.kw('while')> '(' <expr> ')' <block> }
    rule statement:sym<until>  { <.kw('until')> '(' <expr> ')' <block> }
    rule statement:sym<cfor>   { [ <.kw('for')> | <.kw('foreach')> ] '(' <cinit>? ';' <ccond>? ';' <cstep>? ')' <block> }
    rule cinit { <modexpr> }
    rule ccond { <expr> }
    rule cstep { <modexpr> }
    rule statement:sym<forlist>{ [ <.kw('for')> | <.kw('foreach')> ] [ <.kw('my')>? <scalar> ]? '(' <expr> ')' <block> }
    rule statement:sym<block>  { <block> }
    rule statement:sym<mod>    { <modified> }

    # a "modified" statement: a bare expression with optional trailing modifier
    rule modified {
        <mexpr=modexpr>
        [ $<modkw>=[ 'if' | 'unless' | 'while' | 'until' | 'for' | 'foreach' ] <modcond=expr> ]?
        [ ';' | <?before '}'> | $ ]
    }
    proto rule modexpr {*}
    rule modexpr:sym<my>   { <.kw('my')> [ '(' <lhs=varlist> ')' | <one=lvar> ] [ '=' <rhs=expr> ]? }
    rule modexpr:sym<our>  { <.kw('our')> [ '(' <lhs=varlist> ')' | <one=lvar> ] [ '=' <rhs=expr> ]? }
    rule modexpr:sym<last> { <.kw('last')> }
    rule modexpr:sym<next> { <.kw('next')> }
    rule modexpr:sym<return> { <.kw('return')> <expr>? }
    rule modexpr:sym<expr> { <expr> }

    rule elsif { <.kw('elsif')> '(' <expr> ')' <block> }
    rule elsec { <.kw('else')> <block> }

    rule varlist { <lvar>+ % ',' }
    rule lvar { <scalar> | <array> | <hash> }

    # ----- expressions (precedence: loose -> tight) -----
    rule expr     { <lowor> }
    rule lowor    { <lowand> [ $<op>=[ 'or'<!bw> | 'xor'<!bw> ] <lowand> ]* }
    rule lowand   { <lownot> [ <.kw('and')> <lownot> ]* }
    rule lownot   { $<neg>=[ 'not'<!bw> ]* <listex> }
    rule listex   { <assign>+ %% [ ',' | '=>' ] }
    rule assign   { <ternary> [ $<op>=[ '=' <!before '='> | '+=' | '-=' | '.=' | '*=' | '/=' | '%=' | '**=' | 'x=' | '||=' | '&&=' | '//=' ] <assign> ]? }
    rule ternary  { <range> [ '?' <assign> ':' <assign> ]? }
    rule range    { <oror> [ '..' <oror> ]? }
    rule oror     { <andand> [ $<op>=[ '||' | '//' ] <andand> ]* }
    rule andand   { <equ> [ '&&' <equ> ]* }
    rule equ      { <rel> [ $<op>=[ '==' | '!=' | '<=>' | 'eq'<!bw> | 'ne'<!bw> | 'cmp'<!bw> ] <rel> ]* }
    rule rel      { <add> [ $<op>=[ '<=' | '>=' | '<' | '>' | 'lt'<!bw> | 'gt'<!bw> | 'le'<!bw> | 'ge'<!bw> ] <add> ]* }
    rule add      { <mul> [ $<op>=[ '+' | '-' | '.' <!before <[.=]>> ] <mul> ]* }
    rule mul      { <bind> [ $<op>=[ '*' | '/' | '%' | 'x'<!bw> ] <bind> ]* }
    rule bind     { <unary> [ $<op>=[ '=~' | '!~' ] <bindarg> ]? }
    rule bindarg  { <barerx> | <unary> }
    rule unary    { $<op>=[ '!' | '-' <!before '-'> | '+' <!before '+'> | '\\' ]* <pow> }
    rule pow      { <incdec> [ '**' <unary> ]? }
    rule incdec   { $<pre>=[ '++' | '--' ]? <postfix> $<post>=[ '++' | '--' ]? }
    # calls are postfix: `word` matches the name, `(...)` binds as a call tail
    rule postfix  { <primary> <ptail>* }
    proto rule ptail {*}
    rule ptail:sym<call>  { '(' [ <expr> ]? ')' }
    rule ptail:sym<index> { '[' <expr> ']' }

    proto rule primary {*}
    rule primary:sym<num>      { <num> }
    rule primary:sym<str>      { <str> }
    rule primary:sym<qw>       { <.kw('qw')> <qw> }
    # list operators and named-unary operators carry a literal prefix, so LTM
    # prefers them over a bare word. `map/grep/sort` block form is a distinct
    # rule (gated on a following `{`); their plain forms go through <listop>,
    # which — unlike <higherop> — parses a following bareword cleanly in rakupp.
    rule primary:sym<higherop> { $<op>=[ 'map' | 'grep' | 'sort' ]<!bw> <?before <.ws> '{'> <block> <expr> }
    rule primary:sym<listop>   { <listopname> [ <callargs> || <listex> ]? }
    rule primary:sym<unaryop>  { <unaryname> [ <callargs> || <add> ]? }
    rule callargs { '(' [ <expr> ]? ')' }
    rule primary:sym<subst>    { <relit_s> }
    rule primary:sym<match>    { <relit_m> }
    rule primary:sym<barerx>   { <relit_bare> }
    rule primary:sym<hash>     { <hash> }
    rule primary:sym<array>    { <array> }
    rule primary:sym<lastidx>  { '$#' <varname> }
    rule primary:sym<scalar>   { <scalar> [ $<idx>=[ '[' <expr> ']' | '{' <hkey> '}' ] ]? }
    rule primary:sym<paren>    { '(' [ <expr> ]? ')' }
    rule primary:sym<word>     { <idlike> }

    # regex literals are tokens (no auto-ws inside the pattern)
    token repat      { [ '\\' . | <-[/\\]> ]* }
    token reflags    { <[a..z]>* }
    token relit_m    { 'm' '/' <repat> '/' <reflags> }
    token relit_s    { 's' '/' <repat> '/' <repat> '/' <reflags> }
    token relit_bare { '/' <repat> '/' <reflags> }

    token listopname { [ 'printf' | 'print' | 'say' | 'push' | 'unshift' | 'warn' | 'die' | 'join' | 'split' | 'sprintf' | 'reverse' | 'sort' ] <!bw> }
    token unaryname  { [ 'ucfirst' | 'lcfirst' | 'uc' | 'lc' | 'length' | 'abs' | 'int' | 'sqrt' | 'ord' | 'chr' | 'chomp' | 'defined' | 'exists' | 'scalar' | 'keys' | 'values' | 'pop' | 'shift' ] <!bw> }

    # ----- variables -----
    token scalar { '$' <varname> }
    token array  { '@' <varname> }
    token hash   { '%' <varname> }
    token varname { <[A..Za..z_]> <[A..Za..z0..9_]>* | '_' | \d+ }
    token hkey   { <idlike> <?before <.ws> '}'> | <expr> }

    # ----- literals -----
    token num  { \d+ [ '.' \d+ ]? [ <[eE]> <[+-]>? \d+ ]? | '.' \d+ }
    token str  { '"' ~ '"' $<body>=[ [ '\\' . | <-["\\]> ]* ] | "'" ~ "'" $<body>=[ [ '\\' . | <-['\\]> ]* ] }
    token qw   { '(' $<body>=<-[)]>* ')' | '/' $<body>=<-[/]>* '/' | '<' $<body>=<-[>]>* '>' }

    token bw   { <[A..Za..z0..9_]> }
}

# ---------- AST helpers -------------------------------------------------
sub node-list($caps) {
    my @out;
    for @($caps) -> $c { @out.push($c.made) }
    @out;
}

# ---------- values: runtime containers ----------------------------------
class PerlSub { has $.name; has @.body; has $.env; }

# ---------- actions -----------------------------------------------------
class Actions {
    method TOP($/)   { make %( t => 'prog',  stmts => node-list($<statement>) ) }
    method block($/) { make node-list($<statement>) }

    method statement:sym<empty>($/)  { make %( t => 'nop' ) }
    method statement:sym<sub>($/)    { make %( t => 'subdef', name => ~$<idlike>, body => $<block>.made ) }
    method statement:sym<my>($/)     { make $<modified>.made }
    method statement:sym<mod>($/)    { make $<modified>.made }
    method statement:sym<block>($/)  { make %( t => 'block', stmts => $<block>.made ) }
    method statement:sym<if>($/) {
        make %( t => 'if', cond => $<expr>.made, then => $<block>.made,
                elsifs => node-list($<elsif>),
                else => ($<elsec> ?? $<elsec>.made !! Nil) )
    }
    method statement:sym<unless>($/) {
        make %( t => 'if', neg => True, cond => $<expr>.made, then => $<block>.made,
                elsifs => [], else => ($<elsec> ?? $<elsec>.made !! Nil) )
    }
    method statement:sym<while>($/) { make %( t => 'while', cond => $<expr>.made, body => $<block>.made ) }
    method statement:sym<until>($/) { make %( t => 'while', neg => True, cond => $<expr>.made, body => $<block>.made ) }
    method statement:sym<cfor>($/) {
        make %( t => 'cfor',
                init => ($<cinit> ?? $<cinit><modexpr>.made !! Nil),
                cond => ($<ccond> ?? $<ccond><expr>.made !! Nil),
                step => ($<cstep> ?? $<cstep><modexpr>.made !! Nil),
                body => $<block>.made )
    }
    method statement:sym<forlist>($/) {
        make %( t => 'forlist',
                var  => ($<scalar> ?? ~$<scalar><varname> !! Nil),
                list => $<expr>.made, body => $<block>.made )
    }
    method elsif($/) { make %( cond => $<expr>.made, body => $<block>.made ) }
    method elsec($/) { make $<block>.made }

    method modified($/) {
        my $base = $<mexpr>.made;
        if $<modkw> {
            make %( t => 'modif', kind => (~$<modkw>).trim, expr => $base, cond => $<modcond>.made )
        }
        else { make $base }
    }
    method modexpr:sym<my>($/) {
        my @targets = $<lhs> ?? $<lhs>.made !! [ $<one>.made ];
        make %( t => 'my', 'our' => False, listform => ?$<lhs>, targets => @targets,
                rhs => ($<rhs> ?? $<rhs>.made !! Nil) );
    }
    method modexpr:sym<our>($/) {
        my @targets = $<lhs> ?? $<lhs>.made !! [ $<one>.made ];
        make %( t => 'my', 'our' => True, listform => ?$<lhs>, targets => @targets,
                rhs => ($<rhs> ?? $<rhs>.made !! Nil) );
    }
    method modexpr:sym<last>($/)   { make %( t => 'last' ) }
    method modexpr:sym<next>($/)   { make %( t => 'next' ) }
    method modexpr:sym<return>($/) { make %( t => 'return', expr => ($<expr> ?? $<expr>.made !! Nil) ) }
    method modexpr:sym<expr>($/)   { make $<expr>.made }

    method varlist($/) { make node-list($<lvar>) }
    method lvar($/) {
        make ($<scalar> ?? $<scalar>.made !! $<array> ?? $<array>.made !! $<hash>.made)
    }

    method scalar($/) { make %( t => 'var', sigil => '$', name => ~$<varname> ) }
    method array($/)  { make %( t => 'var', sigil => '@', name => ~$<varname> ) }
    method hash($/)   { make %( t => 'var', sigil => '%', name => ~$<varname> ) }

    method expr($/)   { make $<lowor>.made }
    method lowor($/)  { make fold-logic($<lowand>, $<op>) }
    method lowand($/) { make fold-logic-op($<lownot>, 'and') }
    method lownot($/) {
        my $e = $<listex>.made;
        $e = %( t => 'not', e => $e ) for ^(node-strs($<neg>).elems);
        make $e;
    }
    method listex($/) {
        my @a = node-list($<assign>);
        make @a.elems == 1 ?? @a[0] !! %( t => 'list', items => @a );
    }
    method assign($/) {
        if $<assign> {
            make %( t => 'assign', op => (~$<op>).trim, target => $<ternary>.made, value => $<assign>.made )
        }
        else { make $<ternary>.made }
    }
    method ternary($/) {
        my @a = node-list($<assign>);
        if @a.elems == 2 { make %( t => 'cond', cond => $<range>.made, then => @a[0], else => @a[1] ) }
        else { make $<range>.made }
    }
    method range($/) {
        my @o = node-list($<oror>);
        if @o.elems == 2 { make %( t => 'range', from => @o[0], to => @o[1] ) }
        else { make @o[0] }
    }
    method oror($/)   { make fold-logic($<andand>, $<op>) }
    method andand($/) { make fold-logic-op($<equ>, '&&') }
    method equ($/)    { make fold-bin($<rel>,  $<op>) }
    method rel($/)    { make fold-bin($<add>,  $<op>) }
    method add($/)    { make fold-bin($<mul>,  $<op>) }
    method mul($/)    { make fold-bin($<bind>, $<op>) }
    method bind($/) {
        if $<op> && ~$<op> ne '' {
            my $neg = (~$<op>).trim eq '!~';
            make %( t => 'bind', neg => $neg, target => $<unary>.made, rx => $<bindarg>.made );
        }
        else { make $<unary>.made }
    }
    method bindarg($/) { make ($<barerx> ?? $<barerx>.made !! $<unary>.made) }
    method primary:sym<match>($/) {
        make %( t => 'rx', kind => 'm', pat => ~$<relit_m><repat>, flags => ~$<relit_m><reflags> )
    }
    method primary:sym<barerx>($/) {
        make %( t => 'rx', kind => 'm', pat => ~$<relit_bare><repat>, flags => ~$<relit_bare><reflags> )
    }
    method primary:sym<subst>($/) {
        make %( t => 'rx', kind => 's',
                pat  => ~$<relit_s><repat>[0], repl => ~$<relit_s><repat>[1],
                flags => ~$<relit_s><reflags> )
    }
    method unary($/) {
        my $e = $<pow>.made;
        my @ops = node-strs($<op>);
        for @ops.reverse -> $o { $e = %( t => 'unary', op => $o, e => $e ) }
        make $e;
    }
    method pow($/) {
        if $<unary> { make %( t => 'bin', op => '**', l => $<incdec>.made, r => $<unary>.made ) }
        else { make $<incdec>.made }
    }
    method incdec($/) {
        if $<pre> && ~$<pre> ne '' { make %( t => 'preinc',  op => (~$<pre>).trim,  target => $<postfix>.made ) }
        elsif $<post> && ~$<post> ne '' { make %( t => 'postinc', op => (~$<post>).trim, target => $<postfix>.made ) }
        else { make $<postfix>.made }
    }
    method postfix($/) {
        my $base = $<primary>.made;
        for @($<ptail>) -> $t {
            my $tm = $t.made;
            if $tm<kind> eq 'call' {
                my $name = ($base<t> eq 'str' && !$base<interp>) ?? $base<body> !! Nil;
                $base = %( t => 'call', name => $name, args => $tm<args> );
            }
            elsif $tm<kind> eq 'index' {
                $base = %( t => 'lindex', list => $base, idx => $tm<idx> );
            }
        }
        make $base;
    }
    method ptail:sym<call>($/) {
        make %( kind => 'call', args => ($<expr> ?? $<expr>.made !! %( t => 'list', items => [] )) )
    }
    method ptail:sym<index>($/) { make %( kind => 'index', idx => $<expr>.made ) }
    method primary:sym<lastidx>($/) { make %( t => 'lastidx', name => ~$<varname> ) }

    method primary:sym<num>($/)  { make %( t => 'num', v => num-val(~$/) ) }
    method primary:sym<str>($/) {
        my $raw = ~$/;
        my $dq  = $raw.starts-with('"');
        make %( t => 'str', body => ~$<str><body>, interp => $dq );
    }
    method primary:sym<qw>($/) {
        my @w = (~$<qw><body>).words;
        make %( t => 'list', items => @w.map(-> $x { %( t => 'str', body => $x, interp => False ) }).Array );
    }
    method primary:sym<higherop>($/) {
        make %( t => 'higherop', op => (~$<op>).trim,
                block => ($<block> ?? $<block>.made !! Nil),
                args  => $<expr>.made )
    }
    method primary:sym<listop>($/) {
        my $args = $<callargs> ?? $<callargs>.made
                !! $<listex>   ?? $<listex>.made
                !! %( t => 'list', items => [] );
        make %( t => 'call', name => (~$<listopname>).trim, args => $args );
    }
    method primary:sym<unaryop>($/) {
        my $args = $<callargs> ?? $<callargs>.made
                !! $<add>      ?? $<add>.made
                !! %( t => 'list', items => [] );
        make %( t => 'call', name => (~$<unaryname>).trim, args => $args );
    }
    method callargs($/) { make ($<expr> ?? $<expr>.made !! %( t => 'list', items => [] )) }
    method hkey($/) {
        if $<idlike> { make %( t => 'str', body => ~$<idlike>, interp => False ) }
        else { make $<expr>.made }
    }
    method primary:sym<hash>($/)   { make $<hash>.made }
    method primary:sym<array>($/)  { make $<array>.made }
    method primary:sym<scalar>($/) {
        my $var = $<scalar>.made;
        if $<idx> {
            my $raw = ~$<idx>;
            if $raw.starts-with('[') {
                make %( t => 'elem', kind => 'idx', name => $var<name>, key => $<expr>.made )
            }
            else {
                make %( t => 'elem', kind => 'key', name => $var<name>, key => $<hkey>.made )
            }
        }
        else { make $var }
    }
    method primary:sym<paren>($/) {
        my $inner = $<expr> ?? $<expr>.made !! %( t => 'list', items => [] );
        # keep an explicit list; wrap a single element so `(x) x n` can list-repeat
        make $inner<t> eq 'list' ?? $inner !! %( t => 'list', items => [$inner], paren => True );
    }
    method primary:sym<word>($/)   { make %( t => 'str', body => ~$<idlike>, interp => False ) }
}

# fold helpers (top-level, per rakupp R7: class-body subs unseen by methods)
sub node-strs($caps) {
    my @out;
    return @out unless $caps;
    for @($caps) -> $c { my $s = (~$c).trim; @out.push($s) if $s ne '' }
    @out;
}
sub fold-bin($terms, $ops) {
    my @t = node-list($terms);
    my @o = node-strs($ops);
    my $acc = @t[0];
    for ^@o.elems -> $i { $acc = %( t => 'bin', op => @o[$i], l => $acc, r => @t[$i + 1] ) }
    $acc;
}
sub fold-logic($terms, $ops) {
    my @t = node-list($terms);
    my @o = node-strs($ops);
    my $acc = @t[0];
    for ^@o.elems -> $i { $acc = %( t => 'logic', op => @o[$i], l => $acc, r => @t[$i + 1] ) }
    $acc;
}
sub fold-logic-op($terms, Str $op) {
    my @t = node-list($terms);
    my $acc = @t[0];
    for 1 ..^ @t.elems -> $i { $acc = %( t => 'logic', op => $op, l => $acc, r => @t[$i] ) }
    $acc;
}
sub num-val(Str $t) {
    ($t.contains('.') || $t.lc.contains('e')) ?? +$t !! $t.Int;
}

# =========================================================================
# EVALUATOR
# =========================================================================
sub name-key($sigil, $name) { $sigil ~ $name }

sub get-array($env, $name) {
    my $k = '@' ~ $name;
    my $e = $env.lookup($k);
    if $e.defined { return $e.vars{$k} }
    my @a;
    $env.set($k, @a);            # autovivify at top
    $env.get($k);
}
sub get-hash($env, $name) {
    my $k = '%' ~ $name;
    my $e = $env.lookup($k);
    if $e.defined { return $e.vars{$k} }
    my %h;
    $env.set($k, %h);
    $env.get($k);
}

sub eval-stmts(@stmts, $env) {
    my $last = UNDEF;
    for @stmts -> $s { $last = eval-stmt($s, $env) }
    $last;
}

sub eval-stmt($n, $env) {
    given $n<t> {
        when 'prog'   { return eval-stmts($n<stmts>, $env) }
        when 'block'  { return eval-stmts($n<stmts>, Env.new(parent => $env)) }
        when 'nop'    { return UNDEF }
        when 'subdef' {
            $env.declare('&' ~ $n<name>, PerlSub.new(name => $n<name>, body => $n<body>, env => $env));
            return UNDEF;
        }
        when 'my'     { return eval-my($n, $env) }
        when 'if'     { return eval-if($n, $env) }
        when 'while'  { return eval-while($n, $env) }
        when 'cfor'   { return eval-cfor($n, $env) }
        when 'forlist' { return eval-forlist($n, $env) }
        when 'modif'  { return eval-modif($n, $env) }
        when 'last'   { LastX.new.throw }
        when 'next'   { NextX.new.throw }
        when 'return' { RetX.new(value => ($n<expr> ?? eval-list($n<expr>, $env) !! [])).throw }
        default       { return eval-scalar($n, $env) }   # expression statement (void)
    }
}

sub eval-if($n, $env) {
    my $c = truthy(eval-scalar($n<cond>, $env));
    $c = !$c if $n<neg>;
    return eval-stmts($n<then>, Env.new(parent => $env)) if $c;
    for @($n<elsifs>) -> $ei {
        return eval-stmts($ei<body>, Env.new(parent => $env)) if truthy(eval-scalar($ei<cond>, $env));
    }
    return eval-stmts($n<else>, Env.new(parent => $env)) if $n<else>;
    UNDEF;
}

sub eval-while($n, $env) {
    loop {
        my $c = truthy(eval-scalar($n<cond>, $env));
        $c = !$c if $n<neg>;
        last unless $c;
        {
            eval-stmts($n<body>, Env.new(parent => $env));
            CATCH {
                when LastX { last }
                when NextX { }
            }
        }
    }
    UNDEF;
}

sub eval-cfor($n, $env) {
    my $loop = Env.new(parent => $env);
    eval-stmt($n<init>, $loop) if $n<init>;
    loop {
        last if $n<cond> && !truthy(eval-scalar($n<cond>, $loop));
        {
            eval-stmts($n<body>, Env.new(parent => $loop));
            CATCH {
                when LastX { last }
                when NextX { }
            }
        }
        eval-stmt($n<step>, $loop) if $n<step>;
    }
    UNDEF;
}

sub eval-forlist($n, $env) {
    my @items = eval-list($n<list>, $env);
    my $vname = $n<var> // '_';
    for @items -> $item {
        my $loop = Env.new(parent => $env);
        $loop.declare('$' ~ $vname, $item);
        {
            eval-stmts($n<body>, $loop);
            CATCH {
                when LastX { last }
                when NextX { }
            }
        }
    }
    UNDEF;
}

sub eval-modif($n, $env) {
    # Note: eval-stmt(...) may throw a cooperative RetX/LastX/NextX. Assign to a
    # temp before returning — `return <throwing-expr>` loses the throw in rakupp.
    given $n<kind> {
        when 'if' {
            return UNDEF unless truthy(eval-scalar($n<cond>, $env));
            my $r = eval-stmt($n<expr>, $env);
            return $r;
        }
        when 'unless' {
            return UNDEF if truthy(eval-scalar($n<cond>, $env));
            my $r = eval-stmt($n<expr>, $env);
            return $r;
        }
        when 'while'  { while truthy(eval-scalar($n<cond>, $env)) { eval-stmt($n<expr>, $env) }; return UNDEF }
        when 'until'  { until truthy(eval-scalar($n<cond>, $env)) { eval-stmt($n<expr>, $env) }; return UNDEF }
        when 'for' | 'foreach' {
            for eval-list($n<cond>, $env) -> $it {
                my $loop = Env.new(parent => $env);
                $loop.declare('$_', $it);
                eval-stmt($n<expr>, $loop);
            }
            return UNDEF;
        }
    }
}

sub eval-my($n, $env) {
    my @targets = @($n<targets>);
    my $listctx = $n<listform> || @targets > 1 || @targets[0]<sigil> ne '$';
    if $listctx {
        my @vals = $n<rhs> ?? eval-list($n<rhs>, $env) !! [];
        assign-targets(@targets, @vals, $env, :decl);
        return @vals.elems;
    }
    else {
        my $v = $n<rhs> ?? eval-scalar($n<rhs>, $env) !! UNDEF;
        $env.declare('$' ~ @targets[0]<name>, $v);
        return $v;
    }
}

sub assign-targets(@targets, @vals, $env, Bool :$decl) {
    my $i = 0;
    for @targets -> $t {
        given $t<sigil> {
            when '$' {
                my $v = $i < @vals.elems ?? @vals[$i] !! UNDEF;
                $decl ?? $env.declare('$' ~ $t<name>, $v) !! $env.set('$' ~ $t<name>, $v);
                $i++;
            }
            when '@' {
                my @rest = $i < @vals.elems ?? @vals[$i .. *] !! [];
                my @a = @rest;
                $decl ?? $env.declare('@' ~ $t<name>, @a) !! $env.set('@' ~ $t<name>, @a);
                $i = @vals.elems;
            }
            when '%' {
                my %h;
                my @rest = $i < @vals.elems ?? @vals[$i .. *] !! [];
                loop (my $j = 0; $j < @rest.elems; $j += 2) {
                    %h{to-str(@rest[$j])} = $j + 1 < @rest.elems ?? @rest[$j + 1] !! UNDEF;
                }
                $decl ?? $env.declare('%' ~ $t<name>, %h) !! $env.set('%' ~ $t<name>, %h);
                $i = @vals.elems;
            }
        }
    }
}

# ---------- scalar-context evaluation -----------------------------------
sub eval-scalar($n, $env) {
    return UNDEF unless $n;
    given $n<t> {
        when 'num'  { return $n<v> }
        when 'str'  { return $n<interp> ?? interpolate($n<body>, $env) !! unescape-sq($n<body>) }
        when 'var'  {
            given $n<sigil> {
                when '$' { my $v = $env.get('$' ~ $n<name>); return $v.defined ?? $v !! UNDEF }
                when '@' { return get-array($env, $n<name>).elems }
                when '%' { return get-hash($env, $n<name>).keys.elems }
            }
        }
        when 'elem' { return elem-get($n, $env) }
        when 'list' {
            my @a = eval-list($n, $env);
            return @a ?? @a[*-1] !! UNDEF;    # comma op in scalar ctx = last
        }
        when 'bin'    { return eval-bin($n, $env) }
        when 'unary'  { return eval-unary($n, $env) }
        when 'logic'  { return eval-logic($n, $env) }
        when 'not'    { return truthy(eval-scalar($n<e>, $env)) ?? '' !! 1 }
        when 'cond'   { return truthy(eval-scalar($n<cond>, $env)) ?? eval-scalar($n<then>, $env) !! eval-scalar($n<else>, $env) }
        when 'range'  { my @a = eval-list($n, $env); return @a.elems }
        when 'assign' { return eval-assign($n, $env) }
        when 'preinc' | 'postinc' { return eval-incdec($n, $env) }
        when 'call'   { my @r = eval-call($n, $env, 'scalar'); return @r ?? @r[*-1] !! UNDEF }
        when 'higherop' { my @r = eval-higherop($n, $env); return @r.elems }
        when 'bind'   { return eval-bind($n, $env, 'scalar') }
        when 'rx'     { return eval-rx-standalone($n, $env, 'scalar') }
        when 'lastidx' { return get-array($env, $n<name>).elems - 1 }
        when 'lindex' {
            my @l = eval-list($n<list>, $env);
            my $i = to-num(eval-scalar($n<idx>, $env)).Int;
            $i += @l.elems if $i < 0;
            return (0 <= $i < @l.elems && @l[$i].defined) ?? @l[$i] !! UNDEF;
        }
        default { perl-die("cannot evaluate node type '$n<t>'") }
    }
}

sub eval-bin($n, $env) {
    my $op = $n<op>;
    my $a = eval-scalar($n<l>, $env);
    my $b = eval-scalar($n<r>, $env);
    given $op {
        when '+'  { return to-num($a) + to-num($b) }
        when '-'  { return to-num($a) - to-num($b) }
        when '*'  { return to-num($a) * to-num($b) }
        when '/'  { my $d = to-num($b); perl-die("Illegal division by zero") if $d == 0; return to-num($a) / $d }
        when '%'  { my $d = to-num($b).Int; perl-die("Illegal modulus zero") if $d == 0; return to-num($a).Int % $d }
        when '**' { return to-num($a) ** to-num($b) }
        when '.'  { return to-str($a) ~ to-str($b) }
        when 'x'  { return to-str($a) x (to-num($b).Int max 0) }
        when '==' { return to-num($a) == to-num($b) ?? 1 !! '' }
        when '!=' { return to-num($a) != to-num($b) ?? 1 !! '' }
        when '<'  { return to-num($a) <  to-num($b) ?? 1 !! '' }
        when '>'  { return to-num($a) >  to-num($b) ?? 1 !! '' }
        when '<=' { return to-num($a) <= to-num($b) ?? 1 !! '' }
        when '>=' { return to-num($a) >= to-num($b) ?? 1 !! '' }
        when '<=>' { my $r = to-num($a) <=> to-num($b); return $r == Less ?? -1 !! ($r == More ?? 1 !! 0) }
        when 'eq' { return to-str($a) eq to-str($b) ?? 1 !! '' }
        when 'ne' { return to-str($a) ne to-str($b) ?? 1 !! '' }
        when 'lt' { return to-str($a) lt to-str($b) ?? 1 !! '' }
        when 'gt' { return to-str($a) gt to-str($b) ?? 1 !! '' }
        when 'le' { return to-str($a) le to-str($b) ?? 1 !! '' }
        when 'ge' { return to-str($a) ge to-str($b) ?? 1 !! '' }
        when 'cmp' { my $r = to-str($a) leg to-str($b); return $r == Less ?? -1 !! ($r == More ?? 1 !! 0) }
        default   { perl-die("unknown operator '$op'") }
    }
}

sub eval-unary($n, $env) {
    given $n<op> {
        when '!' { return truthy(eval-scalar($n<e>, $env)) ?? '' !! 1 }
        when '-' { return -to-num(eval-scalar($n<e>, $env)) }
        when '+' { return eval-scalar($n<e>, $env) }
        when '\\' { return eval-scalar($n<e>, $env) }   # references out of scope: pass through
    }
}

sub eval-logic($n, $env) {
    my $a = eval-scalar($n<l>, $env);
    given $n<op> {
        when '&&' | 'and' { return truthy($a) ?? eval-scalar($n<r>, $env) !! $a }
        when '||' | 'or'  { return truthy($a) ?? $a !! eval-scalar($n<r>, $env) }
        when '//'         { return is-undef($a) ?? eval-scalar($n<r>, $env) !! $a }
        when 'xor'        { return truthy($a) xor truthy(eval-scalar($n<r>, $env)) ?? 1 !! '' }
    }
}

sub eval-incdec($n, $env) {
    my $old = to-num(lvalue-get($n<target>, $env));
    my $new = $n<op> eq '++' ?? $old + 1 !! $old - 1;
    lvalue-set($n<target>, $new, $env);
    $n<t> eq 'preinc' ?? $new !! $old;
}

sub eval-assign($n, $env) {
    my $op = $n<op>;
    my $target = $n<target>;
    if $op eq '=' {
        # list assignment if target is a list or an array/hash var
        if $target<t> eq 'list' {
            my @vals = eval-list($n<value>, $env);
            assign-targets($target<items>, @vals, $env);
            return @vals.elems;
        }
        if $target<t> eq 'var' && $target<sigil> ne '$' {
            my @vals = eval-list($n<value>, $env);
            assign-targets([$target], @vals, $env);
            return @vals.elems;
        }
        my $v = eval-scalar($n<value>, $env);
        lvalue-set($target, $v, $env);
        return $v;
    }
    # compound assignment
    my $cur = lvalue-get($target, $env);
    my $rhs = eval-scalar($n<value>, $env);
    my $bop = $op.subst(/'='$/, '');
    my $v = apply-compound($bop, $cur, $rhs, $target, $env);
    lvalue-set($target, $v, $env);
    $v;
}

sub apply-compound($bop, $cur, $rhs, $target, $env) {
    given $bop {
        when '+'  { return to-num($cur) + to-num($rhs) }
        when '-'  { return to-num($cur) - to-num($rhs) }
        when '*'  { return to-num($cur) * to-num($rhs) }
        when '/'  { return to-num($cur) / to-num($rhs) }
        when '%'  { return to-num($cur).Int % to-num($rhs).Int }
        when '**' { return to-num($cur) ** to-num($rhs) }
        when '.'  { return to-str($cur) ~ to-str($rhs) }
        when 'x'  { return to-str($cur) x to-num($rhs).Int }
        when '||' { return truthy($cur) ?? $cur !! $rhs }
        when '&&' { return truthy($cur) ?? $rhs !! $cur }
        when '//' { return is-undef($cur) ?? $rhs !! $cur }
    }
}

# ---------- lvalues -----------------------------------------------------
sub lvalue-get($n, $env) {
    given $n<t> {
        when 'var'  { my $v = $env.get('$' ~ $n<name>); return $v.defined ?? $v !! UNDEF }
        when 'elem' { return elem-get($n, $env) }
        default { return eval-scalar($n, $env) }
    }
}
sub lvalue-set($n, $v, $env) {
    given $n<t> {
        when 'var'  { $env.set('$' ~ $n<name>, $v) }
        when 'elem' { elem-set($n, $v, $env) }
        default { perl-die("cannot assign to this expression") }
    }
}

sub elem-get($n, $env) {
    if $n<kind> eq 'idx' {
        my @a = get-array($env, $n<name>);
        my $i = to-num(eval-scalar($n<key>, $env)).Int;
        $i += @a.elems if $i < 0;
        return $i >= 0 && $i < @a.elems && @a[$i].defined ?? @a[$i] !! UNDEF;
    }
    else {
        my %h = get-hash($env, $n<name>);
        my $k = to-str(eval-scalar($n<key>, $env));
        return %h{$k}:exists ?? %h{$k} !! UNDEF;
    }
}
sub elem-set($n, $v, $env) {
    if $n<kind> eq 'idx' {
        my $a = get-array($env, $n<name>);
        my $i = to-num(eval-scalar($n<key>, $env)).Int;
        $i += $a.elems if $i < 0;
        $a[$i] = $v;
    }
    else {
        my $h = get-hash($env, $n<name>);
        $h{to-str(eval-scalar($n<key>, $env))} = $v;
    }
}

# ---------- list-context evaluation -------------------------------------
sub eval-list($n, $env) {
    return [] unless $n;
    given $n<t> {
        when 'list'  { my @out; for @($n<items>) -> $it { @out.append(eval-list($it, $env)) }; return @out }
        when 'var'   {
            given $n<sigil> {
                when '$' { return [eval-scalar($n, $env)] }
                when '@' { return get-array($env, $n<name>).Array }
                when '%' {
                    my %h = get-hash($env, $n<name>);
                    my @out; for %h.kv -> $k, $v { @out.push($k); @out.push($v) }
                    return @out;
                }
            }
        }
        when 'range' {
            my $a = to-num(eval-scalar($n<from>, $env)).Int;
            my $b = to-num(eval-scalar($n<to>, $env)).Int;
            return ($a <= $b ?? ($a .. $b) !! ()).Array;
        }
        when 'call'   { return eval-call($n, $env, 'list') }
        when 'higherop' { return eval-higherop($n, $env) }
        when 'bin' {
            if $n<op> eq 'x' && $n<l><t> eq 'list' {   # (LIST) x N — list repetition
                my @l = eval-list($n<l>, $env);
                my $count = to-num(eval-scalar($n<r>, $env)).Int;
                my @out; for ^($count max 0) { @out.append(@l) }
                return @out;
            }
            return [eval-scalar($n, $env)];
        }
        when 'bind'   { return arr-of(eval-bind($n, $env, 'list')) }
        when 'rx'     { return arr-of(eval-rx-standalone($n, $env, 'list')) }
        when 'assign' { eval-assign($n, $env); return eval-list-of-assign($n, $env) }
        when 'cond'   { return truthy(eval-scalar($n<cond>, $env)) ?? eval-list($n<then>, $env) !! eval-list($n<else>, $env) }
        default { return [eval-scalar($n, $env)] }
    }
}
sub eval-list-of-assign($n, $env) {
    # after a list assignment the value is already applied; return RHS as list
    eval-list($n<value>, $env);
}

# =========================================================================
# STRING INTERPOLATION
# =========================================================================
sub unescape-sq(Str $s) {
    # single-quoted: only \\ and \' are special
    $s.subst(/'\\' (<["'\\]>)/, -> $/ { ~$0 }, :g);
}

sub interpolate(Str $s, $env) {
    my @c = $s.comb;
    my $n = @c.elems;
    my $out = '';
    my $i = 0;
    while $i < $n {
        my $c = @c[$i];
        if $c eq '\\' && $i + 1 < $n {
            my $e = @c[$i + 1];
            given $e {
                when 'n' { $out ~= "\n" }
                when 't' { $out ~= "\t" }
                when 'r' { $out ~= "\r" }
                when '0' { $out ~= "\0" }
                when 'e' { $out ~= "\e" }
                when '"' { $out ~= '"' }
                when '\\' { $out ~= '\\' }
                when '$' { $out ~= '$' }
                when '@' { $out ~= '@' }
                default { $out ~= $e }
            }
            $i += 2;
            next;
        }
        if ($c eq '$' || $c eq '@') && $i + 1 < $n {
            my ($text, $adv) = interp-var(@c, $i, $n, $env);
            if $adv > 0 { $out ~= $text; $i += $adv; next }
        }
        $out ~= $c;
        $i++;
    }
    $out;
}

# parse a $var / @var / $var[..] / $var{..} / ${name} starting at $i; returns (text, chars-consumed)
sub interp-var(@c, $i, $n, $env) {
    my $sigil = @c[$i];
    my $j = $i + 1;
    # $#array — last index
    if $sigil eq '$' && $j < $n && @c[$j] eq '#' {
        $j++;
        my $nm = '';
        while $j < $n && (@c[$j] ~~ /<[A..Za..z0..9_]>/) { $nm ~= @c[$j]; $j++ }
        return ('', 0) if $nm eq '';
        return (to-str(get-array($env, $nm).elems - 1), $j - $i);
    }
    # ${name}
    my $braced = False;
    if $j < $n && @c[$j] eq '{' { $braced = True; $j++ }
    my $name = '';
    while $j < $n && (@c[$j] ~~ /<[A..Za..z0..9_]>/) { $name ~= @c[$j]; $j++ }
    return ('', 0) if $name eq '';
    if $braced {
        return ('', 0) unless $j < $n && @c[$j] eq '}';
        $j++;
    }
    # index / key
    if !$braced && $j < $n && (@c[$j] eq '[' || @c[$j] eq '{') {
        my $open = @c[$j];
        my $close = $open eq '[' ?? ']' !! '}';
        my $depth = 0;
        my $inner = '';
        my $k = $j;
        while $k < $n {
            my $ch = @c[$k];
            $depth++ if $ch eq $open;
            if $ch eq $close { $depth--; last if $depth == 0 }
            $inner ~= $ch if !($k == $j);
            $k++;
        }
        $k++;   # past close
        my $key = $inner.trim;
        my $node;
        if $open eq '[' {
            $node = %( t => 'elem', kind => 'idx', name => $name, key => parse-index($key) );
        }
        else {
            my $keynode = ($key ~~ /^ <[A..Za..z_]> <[A..Za..z0..9_]>* $/) ?? %( t => 'str', body => $key, interp => False ) !! parse-index($key);
            $node = %( t => 'elem', kind => 'key', name => $name, key => $keynode );
        }
        return (to-str(elem-get($node, $env)), $k - $i);
    }
    if $sigil eq '@' {
        my @a = get-array($env, $name);
        return (@a.map({ to-str($_) }).join(' '), $j - $i);
    }
    else {
        my $v = $env.get('$' ~ $name);
        return (to-str($v.defined ?? $v !! UNDEF), $j - $i);
    }
}

# tiny index expression parser for interpolation: a number, $var, or -number
sub parse-index(Str $s) {
    if $s ~~ /^ '-'? \d+ $/ { return %( t => 'num', v => $s.Int ) }
    if $s ~~ /^ '$' (<[A..Za..z_]> <[A..Za..z0..9_]>*) $/ { return %( t => 'var', sigil => '$', name => ~$0 ) }
    # fall back: a bareword string
    %( t => 'str', body => $s, interp => False );
}

# =========================================================================
# CALLS: user subs + builtins
# =========================================================================
sub arg-nodes($argsnode) {
    return [] unless $argsnode;
    $argsnode<t> eq 'list' ?? @($argsnode<items>) !! [$argsnode];
}
sub resolve-array($node, $env) {
    perl-die("expected an array") unless $node && $node<t> eq 'var' && $node<sigil> eq '@';
    get-array($env, $node<name>);
}
sub default-array($env) {
    $env.lookup('@_').defined ?? get-array($env, '_') !! get-array($env, 'ARGV');
}

sub eval-call($n, $env, $ctx) {
    my $name = $n<name>;
    perl-die("cannot call a non-subroutine") unless $name.defined;
    my @items = arg-nodes($n<args>);
    given $name {
        when 'push'|'unshift'|'pop'|'shift'|'splice' { return array-op($name, @items, $env) }
        when 'keys'   { my @k = hash-ish(@items[0], $env, 'keys');   return $ctx eq 'scalar' ?? [@k.elems] !! @k }
        when 'values' { my @v = hash-ish(@items[0], $env, 'values'); return $ctx eq 'scalar' ?? [@v.elems] !! @v }
        when 'exists' { return [exists-op(@items[0], $env)] }
        when 'delete' { return [delete-op(@items[0], $env)] }
        when 'chomp'  { return [chomp-op(@items[0], $env)] }
        when 'split'  { return split-op(@items, $env) }
        when 'scalar' { return [@items ?? eval-scalar(@items[0], $env) !! UNDEF] }
        default {
            my $sub = $env.get('&' ~ $name);
            my @args = eval-list($n<args>, $env);
            return $sub ~~ PerlSub ?? call-sub($sub, @args, $ctx) !! call-builtin($name, @args, $env, $ctx);
        }
    }
}

sub array-op($name, @items, $env) {
    given $name {
        when 'push' {
            my $arr = resolve-array(@items[0], $env);
            for @items[1 .. *] -> $it { $arr.append(eval-list($it, $env)) }
            return [$arr.elems];
        }
        when 'unshift' {
            my $arr = resolve-array(@items[0], $env);
            my @vals; for @items[1 .. *] -> $it { @vals.append(eval-list($it, $env)) }
            $arr.prepend(@vals);
            return [$arr.elems];
        }
        when 'pop'   { my $arr = @items ?? resolve-array(@items[0], $env) !! default-array($env); return [$arr.elems ?? $arr.pop !! UNDEF] }
        when 'shift' { my $arr = @items ?? resolve-array(@items[0], $env) !! default-array($env); return [$arr.elems ?? $arr.shift !! UNDEF] }
        when 'splice' {
            my $arr = resolve-array(@items[0], $env);
            my $off = @items > 1 ?? to-num(eval-scalar(@items[1], $env)).Int !! 0;
            $off += $arr.elems if $off < 0;
            my $len = @items > 2 ?? to-num(eval-scalar(@items[2], $env)).Int !! ($arr.elems - $off);
            my @repl; for @items[3 .. *] -> $it { @repl.append(eval-list($it, $env)) }
            my @removed = $arr.splice($off, $len, @repl);
            return @removed.Array;
        }
    }
}

sub hash-ish($node, $env, $which) {
    if $node && $node<t> eq 'var' && $node<sigil> eq '%' {
        my %h = get-hash($env, $node<name>);
        return $which eq 'keys' ?? %h.keys.Array !! %h.values.Array;
    }
    if $node && $node<t> eq 'var' && $node<sigil> eq '@' {
        my @a = get-array($env, $node<name>);
        return $which eq 'keys' ?? (^@a.elems).Array !! @a.Array;
    }
    [];
}
sub exists-op($node, $env) {
    return '' unless $node && $node<t> eq 'elem';
    if $node<kind> eq 'key' {
        my %h = get-hash($env, $node<name>);
        return %h{to-str(eval-scalar($node<key>, $env))}:exists ?? 1 !! '';
    }
    my @a = get-array($env, $node<name>);
    my $i = to-num(eval-scalar($node<key>, $env)).Int;
    return (0 <= $i < @a.elems) ?? 1 !! '';
}
sub delete-op($node, $env) {
    return UNDEF unless $node && $node<t> eq 'elem' && $node<kind> eq 'key';
    my $h = get-hash($env, $node<name>);
    my $k = to-str(eval-scalar($node<key>, $env));
    my $v = $h{$k}:exists ?? $h{$k} !! UNDEF;
    $h{$k}:delete;
    $v;
}
sub chomp-op($node, $env) {
    return 0 unless $node && ($node<t> eq 'var' || $node<t> eq 'elem');
    my $s = to-str(lvalue-get($node, $env));
    if $s.ends-with("\n") { lvalue-set($node, $s.chop, $env); return 1 }
    0;
}

# ---------- map / grep / sort with a block ------------------------------
sub run-block-scalar(@stmts, $env) { eval-stmts(@stmts, $env) }
sub run-block-list(@stmts, $env) {
    return [] unless @stmts;
    for @stmts[0 ..^ @stmts.end] -> $s { eval-stmt($s, $env) }
    eval-list(@stmts[*-1], $env);
}
sub eval-higherop($n, $env) {
    my @list = eval-list($n<args>, $env);
    given $n<op> {
        when 'map' {
            my @out;
            for @list -> $item {
                my $loop = Env.new(parent => $env);
                $loop.declare('$_', $item);
                @out.append(run-block-list($n<block>, $loop));
            }
            return @out;
        }
        when 'grep' {
            my @out;
            for @list -> $item {
                my $loop = Env.new(parent => $env);
                $loop.declare('$_', $item);
                @out.push($item) if truthy(run-block-scalar($n<block>, $loop));
            }
            return @out;
        }
        when 'sort' {
            if $n<block> {
                return @list.sort(-> $x, $y {
                    my $loop = Env.new(parent => $env);
                    $loop.declare('$a', $x);
                    $loop.declare('$b', $y);
                    my $r = to-num(run-block-scalar($n<block>, $loop));
                    $r < 0 ?? Less !! ($r > 0 ?? More !! Same);
                }).Array;
            }
            return @list.map({ to-str($_) }).sort.Array;
        }
    }
}

sub call-sub(PerlSub $sub, @args, $ctx) {
    my $local = Env.new(parent => $sub.env);
    my @a = @args;
    $local.declare('@_', @a);
    my $result = [];
    {
        my $last = eval-stmts($sub.body, $local);
        $result = [$last];
        CATCH {
            when RetX { $result = .value }
        }
    }
    $result;
}

# ---------- builtins ----------------------------------------------------
my $BUILTINS = set <print say printf sprintf warn die
                    length scalar defined exists delete
                    push pop shift unshift splice reverse sort
                    keys values join split
                    uc lc ucfirst lcfirst index substr
                    abs int sqrt chomp chr ord wantarray>;

sub call-builtin(Str $name, @args, $env, $ctx) {
    # many unary builtins default to $_ when called with no argument
    if !@args && $name eq any(<length uc lc ucfirst lcfirst abs int sqrt chr ord chomp>) {
        my $u = $env.get('$_');
        @args = [ $u.defined ?? $u !! UNDEF ];
    }
    given $name {
        when 'print' { print @args.map({ to-str($_) }).join(''); return [1] }
        when 'say'   { say   @args.map({ to-str($_) }).join(''); return [1] }
        when 'warn'  { note  @args.map({ to-str($_) }).join(''); return [1] }
        when 'die'   { perl-die(@args.map({ to-str($_) }).join('') || 'Died') }
        when 'length' { my $v = @args ?? @args[0] !! UNDEF; return [is-undef($v) ?? UNDEF !! to-str($v).chars] }
        when 'uc'    { return [to-str(@args[0] // '').uc] }
        when 'lc'    { return [to-str(@args[0] // '').lc] }
        when 'ucfirst' { return [to-str(@args[0] // '').tc] }
        when 'lcfirst' { my $s = to-str(@args[0] // ''); return [$s.chars ?? $s.substr(0,1).lc ~ $s.substr(1) !! ''] }
        when 'abs'   { return [to-num(@args[0] // 0).abs] }
        when 'int'   { return [to-num(@args[0] // 0).Int] }
        when 'sqrt'  { return [sqrt(to-num(@args[0] // 0))] }
        when 'chr'   { return [chr(to-num(@args[0] // 0).Int)] }
        when 'ord'   { return [to-str(@args[0] // '').chars ?? to-str(@args[0]).ord !! 0] }
        when 'join'  { my $sep = to-str(@args.shift // ''); return [@args.map({ to-str($_) }).join($sep)] }
        when 'reverse' { return $ctx eq 'scalar' ?? [@args.map({ to-str($_) }).join('').flip] !! @args.reverse.Array }
        when 'sort'  { return @args.map({ to-str($_) }).sort.Array }
        when 'scalar' { return [@args.elems] }
        when 'defined' { return [is-undef(@args[0] // UNDEF) ?? '' !! 1] }
        when 'sprintf' { return [do-sprintf(@args)] }
        when 'printf'  { print do-sprintf(@args); return [1] }
        when 'substr' {
            my $s = to-str(@args[0] // ''); my $off = to-num(@args[1] // 0).Int;
            $off += $s.chars if $off < 0;
            my $len = @args > 2 ?? to-num(@args[2]).Int !! $s.chars - $off;
            $len = $s.chars - $off + $len if $len < 0;
            return [$s.substr($off max 0, $len max 0)];
        }
        when 'index' {
            my $s = to-str(@args[0] // ''); my $sub = to-str(@args[1] // '');
            my $from = @args > 2 ?? to-num(@args[2]).Int !! 0;
            my $i = $s.index($sub, $from max 0);
            return [$i.defined ?? $i !! -1];
        }
        when 'rindex' {
            my $s = to-str(@args[0] // ''); my $sub = to-str(@args[1] // '');
            my $i = @args > 2 ?? $s.rindex($sub, to-num(@args[2]).Int) !! $s.rindex($sub);
            return [$i.defined ?? $i !! -1];
        }
        default { perl-die("Undefined subroutine &$name called") }
    }
}

sub do-sprintf(@args) {
    my $fmt = to-str(@args.shift // '');
    # delegate to Raku sprintf with best-effort argument coercion
    my @a = @args.map(-> $x { $x ~~ Numeric ?? $x !! to-str($x) });
    return sprintf($fmt, |@a);
    CATCH { default { return $fmt } }
}

# =========================================================================
# REGEX  — a small backtracking engine for a Perl-regex subset.
# rakupp can't build a regex from a runtime string, so we roll our own:
# literals, . ^ $ \b, char classes [...] with ranges and \d\w\s (+negations),
# groups (capturing and (?:...)), alternation |, and greedy/lazy * + ? {n,m}.
# Flags: i (fold case), g (global), m (^$ per line), s (. matches newline).
# Out of scope: lookaround, backreferences, named captures, tr///.
# =========================================================================
class RxState { has @.c; has $.i is rw; has $.n; has $.ncaps is rw; }

sub rx-compile(Str $pat, Str $flags) {
    my $st = RxState.new(c => $pat.comb.Array, i => 0, n => $pat.chars, ncaps => 0);
    my $alt = rx-parse-alt($st);
    %( alt => $alt, ncaps => $st.ncaps );
}
sub rx-parse-alt($st) {
    my @opts = [ rx-parse-seq($st) ];
    while $st.i < $st.n && $st.c[$st.i] eq '|' { $st.i = $st.i + 1; @opts.push(rx-parse-seq($st)) }
    %( k => 'alt', opts => @opts );
}
sub rx-parse-seq($st) {
    my @nodes;
    while $st.i < $st.n && $st.c[$st.i] ne '|' && $st.c[$st.i] ne ')' {
        @nodes.push(rx-parse-quant($st, rx-parse-atom($st)));
    }
    %( k => 'seq', nodes => @nodes );
}
sub rx-parse-atom($st) {
    my $ch = $st.c[$st.i];
    given $ch {
        when '(' {
            $st.i = $st.i + 1;
            my $cap = True;
            if $st.i + 1 < $st.n && $st.c[$st.i] eq '?' && $st.c[$st.i + 1] eq ':' { $cap = False; $st.i = $st.i + 2 }
            my $idx = 0;
            if $cap { $st.ncaps = $st.ncaps + 1; $idx = $st.ncaps }
            my $alt = rx-parse-alt($st);
            $st.i = $st.i + 1 if $st.i < $st.n && $st.c[$st.i] eq ')';
            return %( k => 'group', cap => $cap, idx => $idx, alt => $alt );
        }
        when '[' { return rx-parse-class($st) }
        when '.' { $st.i = $st.i + 1; return %( k => 'any' ) }
        when '^' { $st.i = $st.i + 1; return %( k => 'anchS' ) }
        when '$' { $st.i = $st.i + 1; return %( k => 'anchE' ) }
        when '\\' {
            $st.i = $st.i + 1;
            my $e = $st.i < $st.n ?? $st.c[$st.i] !! '';
            $st.i = $st.i + 1;
            given $e {
                when 'd' | 'w' | 's' | 'D' | 'W' | 'S' { return %( k => 'class', neg => False, set => [ %( cls => $e ) ] ) }
                when 'b' { return %( k => 'wordb' ) }
                when 'B' { return %( k => 'nwordb' ) }
                when 'n' { return %( k => 'lit', c => "\n" ) }
                when 't' { return %( k => 'lit', c => "\t" ) }
                default  { return %( k => 'lit', c => $e ) }
            }
        }
        default { $st.i = $st.i + 1; return %( k => 'lit', c => $ch ) }
    }
}
sub rx-parse-class($st) {
    $st.i = $st.i + 1;                       # '['
    my $neg = False;
    if $st.i < $st.n && $st.c[$st.i] eq '^' { $neg = True; $st.i = $st.i + 1 }
    my @set;
    while $st.i < $st.n && $st.c[$st.i] ne ']' {
        my $c = $st.c[$st.i];
        if $c eq '\\' {
            $st.i = $st.i + 1;
            my $e = $st.c[$st.i]; $st.i = $st.i + 1;
            given $e {
                when 'd' | 'w' | 's' | 'D' | 'W' | 'S' { @set.push(%( cls => $e )) }
                when 'n' { @set.push(%( c => "\n" )) }
                when 't' { @set.push(%( c => "\t" )) }
                default  { @set.push(%( c => $e )) }
            }
        }
        elsif $st.i + 2 < $st.n && $st.c[$st.i + 1] eq '-' && $st.c[$st.i + 2] ne ']' {
            @set.push(%( a => $c, b => $st.c[$st.i + 2] )); $st.i = $st.i + 3;
        }
        else { @set.push(%( c => $c )); $st.i = $st.i + 1 }
    }
    $st.i = $st.i + 1 if $st.i < $st.n;      # ']'
    %( k => 'class', neg => $neg, set => @set );
}
sub rx-parse-quant($st, $atom) {
    return $atom if $st.i >= $st.n;
    my $ch = $st.c[$st.i];
    my $min; my $max;
    if    $ch eq '*' { $min = 0; $max = -1; $st.i = $st.i + 1 }
    elsif $ch eq '+' { $min = 1; $max = -1; $st.i = $st.i + 1 }
    elsif $ch eq '?' { $min = 0; $max = 1;  $st.i = $st.i + 1 }
    elsif $ch eq '{' {
        my $j = $st.i + 1; my $lo = ''; my $hi = ''; my $comma = False;
        while $j < $st.n && $st.c[$j] ~~ /\d/ { $lo ~= $st.c[$j]; $j++ }
        if $j < $st.n && $st.c[$j] eq ',' { $comma = True; $j++ }
        while $j < $st.n && $st.c[$j] ~~ /\d/ { $hi ~= $st.c[$j]; $j++ }
        return $atom unless $j < $st.n && $st.c[$j] eq '}';
        $st.i = $j + 1;
        $min = $lo eq '' ?? 0 !! $lo.Int;
        $max = $comma ?? ($hi eq '' ?? -1 !! $hi.Int) !! $min;
    }
    else { return $atom }
    my $lazy = False;
    if $st.i < $st.n && $st.c[$st.i] eq '?' { $lazy = True; $st.i = $st.i + 1 }
    %( k => 'rep', node => $atom, min => $min, max => $max, lazy => $lazy );
}

# ---- matcher (CPS with threaded captures) ----
sub cls-match($cls, $ch) {
    given $cls {
        when 'd' { return ?($ch ~~ /^ \d $/) }
        when 'D' { return !($ch ~~ /^ \d $/) }
        when 'w' { return ?($ch ~~ /^ <[A..Za..z0..9_]> $/) }
        when 'W' { return !($ch ~~ /^ <[A..Za..z0..9_]> $/) }
        when 's' { return ?($ch ~~ /^ \s $/) }
        when 'S' { return !($ch ~~ /^ \s $/) }
    }
    False;
}
sub class-match($node, $ch, $flags) {
    my $ci = $flags.contains('i');
    my $matched = False;
    for @($node<set>) -> $it {
        if    $it<cls>:exists { $matched = True if cls-match($it<cls>, $ch) }
        elsif $it<a>:exists   {
            my $c = $ci ?? $ch.lc !! $ch;
            my $lo = $ci ?? $it<a>.lc !! $it<a>;
            my $hi = $ci ?? $it<b>.lc !! $it<b>;
            $matched = True if $lo le $c le $hi;
        }
        else                  { $matched = True if ($ci ?? $it<c>.lc eq $ch.lc !! $it<c> eq $ch) }
    }
    $node<neg> ?? !$matched !! $matched;
}
sub is-word-char($c) { ?($c ~~ /^ <[A..Za..z0..9_]> $/) }
sub word-boundary($str, $pos, $len) {
    my $before = $pos > 0    ?? is-word-char($str.substr($pos - 1, 1)) !! False;
    my $after  = $pos < $len ?? is-word-char($str.substr($pos, 1))     !! False;
    $before != $after;
}

sub rx-match-node($node, $str, $pos, @caps, &cont, $flags, $len) {
    given $node<k> {
        when 'lit' {
            return False unless $pos < $len;
            my $s = $str.substr($pos, 1);
            my $ok = $flags.contains('i') ?? $s.lc eq $node<c>.lc !! $s eq $node<c>;
            return $ok ?? cont($pos + 1, @caps) !! False;
        }
        when 'any' {
            return False unless $pos < $len;
            return False if !$flags.contains('s') && $str.substr($pos, 1) eq "\n";
            return cont($pos + 1, @caps);
        }
        when 'class' {
            return False unless $pos < $len;
            return class-match($node, $str.substr($pos, 1), $flags) ?? cont($pos + 1, @caps) !! False;
        }
        when 'anchS' {
            my $ok = $pos == 0 || ($flags.contains('m') && $pos > 0 && $str.substr($pos - 1, 1) eq "\n");
            return $ok ?? cont($pos, @caps) !! False;
        }
        when 'anchE' {
            my $ok = $pos == $len
                  || ($flags.contains('m') && $str.substr($pos, 1) eq "\n")
                  || ($pos == $len - 1 && $str.substr($pos, 1) eq "\n");
            return $ok ?? cont($pos, @caps) !! False;
        }
        when 'wordb'  { return word-boundary($str, $pos, $len) ?? cont($pos, @caps) !! False }
        when 'nwordb' { return !word-boundary($str, $pos, $len) ?? cont($pos, @caps) !! False }
        when 'group' {
            my $gstart = $pos;
            return rx-match-alt($node<alt>, $str, $pos, @caps, -> $p2, @c2 {
                my @c3 = @c2;
                @c3[$node<idx>] = $str.substr($gstart, $p2 - $gstart) if $node<cap>;
                cont($p2, @c3);
            }, $flags, $len);
        }
        when 'rep' { return rx-match-rep($node, 0, $str, $pos, @caps, &cont, $flags, $len) }
    }
    False;
}
sub rx-match-rep($node, $count, $str, $pos, @caps, &cont, $flags, $len) {
    my $canMore = $node<max> < 0 || $count < $node<max>;
    my $canStop = $count >= $node<min>;
    my &more = -> {
        rx-match-node($node<node>, $str, $pos, @caps, -> $p2, @c2 {
            $p2 > $pos ?? rx-match-rep($node, $count + 1, $str, $p2, @c2, &cont, $flags, $len) !! False
        }, $flags, $len)
    };
    if $node<lazy> {
        return True if $canStop && cont($pos, @caps);
        return $canMore ?? more() !! False;
    }
    return True if $canMore && more();
    return $canStop ?? cont($pos, @caps) !! False;
}
sub rx-match-seq(@nodes, $idx, $str, $pos, @caps, &cont, $flags, $len) {
    return cont($pos, @caps) if $idx >= @nodes.elems;
    rx-match-node(@nodes[$idx], $str, $pos, @caps,
        -> $p2, @c2 { rx-match-seq(@nodes, $idx + 1, $str, $p2, @c2, &cont, $flags, $len) },
        $flags, $len);
}
sub rx-match-alt($alt, $str, $pos, @caps, &cont, $flags, $len) {
    for @($alt<opts>) -> $seq {
        return True if rx-match-seq($seq<nodes>, 0, $str, $pos, @caps, &cont, $flags, $len);
    }
    False;
}
sub rx-search($compiled, $str, $start, $flags) {
    my $len = $str.chars;
    loop (my $sp = $start; $sp <= $len; $sp++) {
        my $end = -1;
        my @final;
        my $ok = rx-match-alt($compiled<alt>, $str, $sp, [], -> $p2, @c2 { $end = $p2; @final = @c2; True }, $flags, $len);
        return %( start => $sp, end => $end, match => $str.substr($sp, $end - $sp), caps => @final ) if $ok;
    }
    Nil;
}

sub set-captures(%m, $env) {
    $env.set('$&', %m<match>);
    my @caps = %m<caps>;
    for 1 .. 9 -> $i {
        $env.set('$' ~ $i, ($i < @caps.elems && @caps[$i].defined) ?? @caps[$i] !! UNDEF);
    }
}

sub eval-rx-standalone($n, $env, $ctx) {
    my $bind = %( t => 'bind', neg => False, target => %( t => 'var', sigil => '$', name => '_' ), rx => $n );
    eval-bind($bind, $env, $ctx);
}

sub eval-bind($n, $env, $ctx) {
    my $rx = $n<rx>;
    unless $rx && $rx<t> eq 'rx' {
        my $pat = to-str(eval-scalar($rx, $env));
        $rx = %( t => 'rx', kind => 'm', pat => $pat, flags => '' );
    }
    return do-subst($n<target>, $rx, $env) if $rx<kind> eq 's';
    do-match($n<target>, $rx, $env, $ctx, $n<neg>);
}

sub do-match($target, $rx, $env, $ctx, $neg) {
    my $str = to-str(eval-scalar($target, $env));
    my $compiled = rx-compile($rx<pat>, $rx<flags>);
    my $global = $rx<flags>.contains('g');
    if $global && $ctx eq 'list' && !$neg {
        my @out;
        my $pos = 0; my $len = $str.chars;
        while $pos <= $len {
            my $m = rx-search($compiled, $str, $pos, $rx<flags>);
            last unless $m;
            set-captures($m, $env);
            if $compiled<ncaps> > 0 {
                for 1 .. $compiled<ncaps> -> $i { @out.push(($m<caps>[$i] // UNDEF)) }
            }
            else { @out.push($m<match>) }
            $pos = $m<end> > $m<start> ?? $m<end> !! $m<end> + 1;
        }
        return @out;
    }
    my $m = rx-search($compiled, $str, 0, $rx<flags>);
    my $hit = $m.defined;
    set-captures($m, $env) if $hit;
    my $bool = $neg ?? ($hit ?? '' !! 1) !! ($hit ?? 1 !! '');
    $ctx eq 'list' ?? [$bool] !! $bool;
}

sub do-subst($target, $rx, $env) {
    my $str = to-str(eval-scalar($target, $env));
    my $compiled = rx-compile($rx<pat>, $rx<flags>);
    my $global = $rx<flags>.contains('g');
    my $len = $str.chars;
    my $out = ''; my $pos = 0; my $count = 0;
    loop {
        my $m = rx-search($compiled, $str, $pos, $rx<flags>);
        unless $m { $out ~= $str.substr($pos); last }
        $out ~= $str.substr($pos, $m<start> - $pos);
        set-captures($m, $env);
        $out ~= interpolate($rx<repl>, $env);
        $count++;
        if $m<end> > $m<start> { $pos = $m<end> }
        else { $out ~= $str.substr($m<end>, 1) if $m<end> < $len; $pos = $m<end> + 1 }
        if !$global { $out ~= $str.substr($pos); last }
        last if $pos > $len;
    }
    lvalue-set($target, $out, $env);
    $count;
}

sub split-op(@items, $env) {
    return [] unless @items;
    my $sepnode = @items[0];
    my $str = @items > 1 ?? to-str(eval-scalar(@items[1], $env)) !! '';
    my $limit = @items > 2 ?? to-num(eval-scalar(@items[2], $env)).Int !! 0;
    my @parts;
    if $sepnode<t> eq 'rx' {
        my $compiled = rx-compile($sepnode<pat>, $sepnode<flags>);
        my $pos = 0; my $len = $str.chars;
        while $pos <= $len {
            last if $limit > 0 && @parts.elems == $limit - 1;
            my $m = rx-search($compiled, $str, $pos, $sepnode<flags>);
            last unless $m && $m<start> < $len;
            if $m<end> == $m<start> {                  # zero-width: split each char boundary
                @parts.push($str.substr($pos, $m<start> - $pos + 1));
                $pos = $m<start> + 1;
            }
            else {
                @parts.push($str.substr($pos, $m<start> - $pos));
                $pos = $m<end>;
            }
        }
        @parts.push($str.substr($pos));
    }
    else {
        my $sep = to-str(eval-scalar($sepnode, $env));
        @parts = $sep eq ' ' ?? $str.trim.split(/\s+/).Array !! $str.split($sep, :skip-empty(False)).Array;
    }
    @parts.pop while $limit <= 0 && @parts.elems && to-str(@parts[*-1]) eq '';   # drop trailing empties
    @parts;
}

sub arr-of($v) { $v ~~ Positional ?? $v.Array !! [$v] }

# =========================================================================
# DRIVER
# =========================================================================
sub parse-perl(Str $src) {
    my $clean = strip-comments($src);
    my $m = PerlGrammar.parse($clean, actions => Actions.new);
    perl-die("Perl parse error") unless $m;
    $m.made;
}

sub make-env(@argv) {
    my $env = Env.new;
    $env.declare('@ARGV', @argv.Array);
    $env;
}
sub hoist-subs($ast, $env) {
    # Perl compiles named subs before running, so a sub may be called before its
    # textual definition. Hoist top-level subdefs first.
    for @($ast<stmts>) -> $s {
        $env.declare('&' ~ $s<name>, PerlSub.new(name => $s<name>, body => $s<body>, env => $env)) if $s<t> eq 'subdef';
    }
}

sub run-perl(Str $src, @argv = []) {
    my $env = make-env(@argv);
    my $ast = parse-perl($src);
    hoist-subs($ast, $env);
    {
        eval-stmts($ast<stmts>, $env);
        CATCH {
            when DieX { note .msg; exit 1 }
            when RetX { }
        }
    }
}

sub repl {
    my $env = make-env([]);
    say "perl.raku — a Perl 5 interpreter in Raku (rakupp). Ctrl-D to exit.";
    while (my $line = prompt('perl> ')).defined {
        my $src = $line.trim;
        next if $src eq '';
        $src ~= ';' unless $src.ends-with(';') || $src.ends-with('}');
        {
            my $ast = parse-perl($src);
            hoist-subs($ast, $env);
            eval-stmts($ast<stmts>, $env);
            CATCH {
                when DieX { note .msg }
                when RetX { }
                default   { note .message }
            }
        }
    }
    say '';
}

sub MAIN($file?, *@args, Str :$ast) {
    if $ast.defined {
        say parse-perl($ast.IO.slurp).raku;
    }
    elsif $file.defined {
        run-perl($file.IO.slurp, @args);
    }
    else {
        repl;
    }
}
