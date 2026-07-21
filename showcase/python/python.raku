#!/usr/bin/env raku
# A Python 3 interpreter. The interesting part is the off-side rule: Python
# blocks are delimited by indentation, not braces. Like CPython, we don't try
# to handle that in the grammar — a tokenizer pass turns indentation into
# explicit INDENT / DEDENT markers, and a normal Raku grammar parses the marker
# stream into an AST that a tree-walking evaluator runs.
#
#   build/rakupp showcase/python/python.raku showcase/python/examples/fib.py
#   build/rakupp showcase/python/python.raku --tokens=file.py   # show the marker stream
#   build/rakupp showcase/python/python.raku --ast=file.py      # dump the AST
#   build/rakupp showcase/python/python.raku                    # REPL
#
# Integers are arbitrary precision (Raku's own), `/` is always float division and
# `//` floors, `and`/`or` return operands, comparisons chain (`1 < x < 10`), and
# print / repr format values the way CPython does. Classes, imports, exceptions,
# generators, decorators and the stdlib are out of scope — see README.md.

# =========================================================================
# INDENTATION TOKENIZER  —  source text -> marker stream
# INDENT = \x0E, DEDENT = \x0F, logical-line end = \n. Leading indentation is
# consumed and replaced by markers; blank and comment-only lines don't affect
# structure; triple-quoted strings and bracket/backslash continuations join
# physical lines so their newlines aren't logical line breaks.
# =========================================================================
my \IN = "\x0E";
my \DE = "\x0F";

sub preprocess(Str $src --> Str) {
    my @lines = logical-lines($src);
    my @stack = [0];
    my $out = '';
    for @lines -> $ll {
        my $indent = $ll<indent>;
        if $indent > @stack[*-1] {
            @stack.push($indent);
            $out ~= IN;
        }
        else {
            while $indent < @stack[*-1] {
                @stack.pop;
                $out ~= DE;
            }
        }
        $out ~= $ll<text> ~ "\n";
    }
    while @stack.elems > 1 { @stack.pop; $out ~= DE }
    $out;
}

sub logical-lines(Str $src) {
    my @c = $src.comb;
    my $n = @c.elems;
    my $i = 0;
    my @out;
    while $i < $n {
        my $col = 0;                                   # measure indent (tab -> mult of 8)
        while $i < $n && (@c[$i] eq ' ' || @c[$i] eq "\t") {
            $col += @c[$i] eq "\t" ?? 8 - $col % 8 !! 1;
            $i++;
        }
        my $text = '';
        my $depth = 0;
        my $blank = True;
        while $i < $n {
            my $ch = @c[$i];
            if $ch eq "\n" {
                $i++;
                last if $depth == 0;
                next;                                  # inside brackets: keep joining
            }
            elsif $ch eq '#' && $depth == 0 {
                while $i < $n && @c[$i] ne "\n" { $i++ }
            }
            elsif $ch eq '\\' && $i + 1 < $n && @c[$i + 1] eq "\n" {
                $i += 2;                               # explicit continuation
            }
            elsif $ch eq '"' || $ch eq "'" {
                my ($str, $ni) = read-string(@c, $i, $n);
                $text ~= $str; $blank = False; $i = $ni;
            }
            else {
                $depth++ if $ch eq '(' || $ch eq '[' || $ch eq '{';
                $depth-- if $ch eq ')' || $ch eq ']' || $ch eq '}';
                $text ~= $ch;
                $blank = False unless $ch eq ' ' || $ch eq "\t";
                $i++;
            }
        }
        @out.push(%( indent => $col, text => $text.trim-trailing )) unless $blank;
    }
    @out;
}

sub read-string(@c, $i is copy, $n) {
    my $q = @c[$i];
    my $triple = $i + 2 < $n && @c[$i + 1] eq $q && @c[$i + 2] eq $q;
    my $out;
    if $triple {
        $out = $q x 3; $i += 3;
        while $i < $n {
            if $i + 2 < $n && @c[$i] eq $q && @c[$i + 1] eq $q && @c[$i + 2] eq $q {
                $out ~= $q x 3; $i += 3; last;
            }
            $out ~= @c[$i]; $i++;
        }
    }
    else {
        $out = $q; $i++;
        while $i < $n && @c[$i] ne $q {
            if @c[$i] eq '\\' && $i + 1 < $n { $out ~= @c[$i]; $i++ }
            $out ~= @c[$i]; $i++;
        }
        $out ~= $q if $i < $n; $i++;
    }
    ($out, $i);
}

# =========================================================================
# VALUES
# =========================================================================
class PyNone { }
my \NONE = PyNone.new;
sub is-none($v --> Bool) { $v ~~ PyNone }

class PyList  { has @.items is rw; }
class PyTuple { has @.items; }
class PyDict  { has @.keys is rw; has %.map is rw;
    method has($k)      { %!map{dkey($k)}:exists }
    method get($k)      { %!map{dkey($k)}:exists ?? %!map{dkey($k)}<v> !! Nil }
    method put($k, $v)  { my $dk = dkey($k); @!keys.push($k) unless %!map{$dk}:exists; %!map{$dk} = %( k => $k, v => $v ) }
    method del($k)      { my $dk = dkey($k); return unless %!map{$dk}:exists; %!map{$dk}:delete; @!keys = @!keys.grep({ dkey($_) ne $dk }) }
    method pairs()      { @!keys.map({ %!map{dkey($_)} }) }
}
# a stable string key for dict lookup (Python hashes by value for these types)
sub dkey($k) {
    given $k {
        when Bool    { 'b:' ~ $k }
        when Int     { 'i:' ~ $k }
        when Numeric { $k == $k.Int ?? 'i:' ~ $k.Int !! 'f:' ~ $k }
        when Str     { 's:' ~ $k }
        when PyNone  { 'none' }
        when PyTuple { 't:' ~ $k.items.map(&dkey).join(',') }
        default      { '?:' ~ $k.WHICH }
    }
}

class PyRange {
    has $.start; has $.stop; has $.step;
    method values {
        my @o; my $i = $!start;
        while ($!step > 0 ?? $i < $!stop !! $i > $!stop) { @o.push($i); $i += $!step }
        @o;
    }
    method len { my $n = $!step > 0 ?? ($!stop - $!start + $!step - 1) !! ($!stop - $!start + $!step + 1); ($n div $!step) max 0 }
}
class PySet { has $.d; }              # wraps a PyDict (keys = members)

class PyFunc {
    has $.name;
    has @.params;      # list of %( name, default(AST|Nil) )
    has $.star;        # name of *args param, or Nil
    has @.body;
    has $.env;
}
class NativeFn { has $.name; has &.fn; }

# =========================================================================
# CONTROL-FLOW EXCEPTIONS
# =========================================================================
class RetX   is Exception { has $.value }
class BreakX is Exception { }
class ContX  is Exception { }
class PyErr  is Exception { has $.kind; has $.msg;
    method message { $!msg.defined ?? "$!kind: $!msg" !! ~$!kind }
}
sub py-raise($kind, $msg?) { PyErr.new(kind => $kind, msg => $msg).throw }

# =========================================================================
# ENVIRONMENT
# =========================================================================
class Env {
    has %.vars;
    has $.parent;
    has $.globals;                     # module-level Env, for functions
    method put($n, $v)     { %!vars{$n} = $v }
    method has-local($n)   { %!vars{$n}:exists }
    method lookup($n) {
        my $e = self;
        while $e.defined {
            return $e if $e.vars{$n}:exists;
            $e = $e.parent;
        }
        Nil
    }
    method get($n) {
        my $e = self.lookup($n);
        py-raise("NameError", "name '$n' is not defined") unless $e.defined;
        $e.vars{$n};
    }
    method has($n) { self.lookup($n).defined }
    method glob()  { $!globals // self }
}

# =========================================================================
# GRAMMAR  (over the marker stream: \x0E=INDENT, \x0F=DEDENT, \n=NEWLINE)
# =========================================================================
grammar PyGrammar {
    token ws { <[\x20\t]>* }                 # horizontal only; NL/markers stay significant

    token NL  { \n }
    token IND { \x0E }
    token DED { \x0F }

    token keyword {
        [ 'if'|'elif'|'else'|'while'|'for'|'def'|'return'|'and'|'or'|'not'|'in'|'is'
        | 'True'|'False'|'None'|'lambda'|'pass'|'break'|'continue'|'global'|'import'|'del' ] <!before \w>
    }
    token name { <!keyword> <[A..Za..z_]> \w* }

    rule TOP { <stmt>* }

    proto rule stmt {*}
    rule stmt:sym<if>    { 'if' <test> ':' <suite> <elifc>* <elsec>? }
    rule stmt:sym<while> { 'while' <test> ':' <suite> <elsec>? }
    rule stmt:sym<for>   { 'for' <exprlist> 'in' <testlist> ':' <suite> <elsec>? }
    rule stmt:sym<def>   { 'def' <name> '(' <params>? ')' ':' <suite> }
    rule stmt:sym<simple>{ <small>+ % ';' ';'? <.NL> }

    rule suite  { <.NL> <.IND> <stmt>+ <.DED> || <small>+ % ';' ';'? <.NL> }
    rule elifc  { 'elif' <test> ':' <suite> }
    rule elsec  { 'else' ':' <suite> }

    rule params { <param>+ % ',' }
    rule param  { '*' <name> | <name> [ '=' <test> ]? }

    proto rule small {*}
    rule small:sym<return>   { 'return' <testlist>? }
    rule small:sym<pass>     { 'pass' }
    rule small:sym<break>    { 'break' }
    rule small:sym<continue> { 'continue' }
    rule small:sym<global>   { 'global' <name>+ % ',' }
    rule small:sym<del>      { 'del' <testlist> }
    rule small:sym<expr> {
        <testlist>
        [ <augop> <rhs=testlist>
        || [ '=' <more=testlist> ]+
        ]?
    }
    token augop { '+=' | '-=' | '**=' | '*=' | '//=' | '/=' | '%=' | '&=' | '|=' | '^=' | '>>=' | '<<=' }

    # expression lists (may build tuples)
    rule exprlist { <bitor>+ % ',' ','? }   # assignment targets: below 'in'/comparison
    rule testlist { <test>+ % ',' ','? }

    # precedence ladder
    proto rule test {*}
    rule test:sym<lambda> { 'lambda' <params>? ':' <test> }
    rule test:sym<cond>   { <orx> [ 'if' <orx> 'else' <test> ]? }

    rule orx   { <andx>+ % 'or' }
    rule andx  { <notx>+ % 'and' }
    rule notx  { $<neg>=[ 'not' ]* <comparison> }
    rule comparison { <bitor> [ <compop> <bitor> ]* }
    token compop { '==' | '!=' | '<=' | '>=' | '<' | '>' | 'not' <.ws> 'in' | 'in' | 'is' <.ws> 'not' | 'is' }

    rule bitor  { <bitxor>+ % [ '|' <!before '|'> ] }
    rule bitxor { <bitand>+ % '^' }
    rule bitand { <shift>+ % [ '&' <!before '&'> ] }
    rule shift  { <arith>  [ $<op>=[ '<<' | '>>' ] <arith> ]* }
    rule arith  { <term>   [ $<op>=[ '+' | '-' ] <term> ]* }
    rule term   { <factor> [ $<op>=[ '*' | '//' | '/' | '%' ] <factor> ]* }
    rule factor { $<op>=[ '-' | '+' | '~' ]* <power> }
    rule power  { <atomexpr> [ '**' <factor> ]? }

    rule atomexpr { <atom> <trailer>* }
    proto rule trailer {*}
    rule trailer:sym<call>  { '(' [ <comparg> || <arglist>? ] ')' }
    rule comparg { <test> <comp> }
    rule trailer:sym<index> { '[' <subscript> ']' }
    rule trailer:sym<dot>   { '.' <name> }

    rule arglist { <arg>+ % ',' ','? }
    proto rule arg {*}
    rule arg:sym<star>  { '*' <test> }
    rule arg:sym<kw>    { <name> '=' <test> }
    rule arg:sym<pos>   { <test> }

    rule subscript { <slice> }
    rule slice {
        <lo=test>? ':' <hi=test>? [ ':' <step=test>? ]?
        || <idx=test>
    }

    proto rule atom {*}
    rule atom:sym<paren>  { '(' <collection>? ')' }
    rule atom:sym<list>   { '[' <collection>? ']' }
    rule atom:sym<dict>   { '{' <mapping>? '}' }
    rule atom:sym<num>    { <number> }
    rule atom:sym<str>    { <string>+ }
    rule atom:sym<name>   { <name> }
    rule atom:sym<true>   { 'True' }
    rule atom:sym<false>  { 'False' }
    rule atom:sym<none>   { 'None' }

    rule collection { <test> [ <comp> || <clist> ] }
    rule clist { [ ',' <test> ]* ','? }
    rule mapping {
        <dpair> [ <comp> || <dtail> ]
        || <test> [ <comp> || <clist> ]
    }
    rule dtail { [ ',' <dpair> ]* ','? }
    rule comp  { <forclause>+ }
    rule forclause { 'for' <exprlist> 'in' <iter=orx> [ 'if' <cif=orx> ]* }
    rule dpair { <k=test> ':' <v=test> }

    token number {
        '0x' <[0..9a..fA..F_]>+
      | '0b' <[01_]>+
      | <[0..9]> <[0..9_]>* '.' <[0..9]> <[0..9_]>* [ <[eE]> <[+-]>? <[0..9]>+ ]?
      | <[0..9]> <[0..9_]>* '.' <!before '.'> [ <[eE]> <[+-]>? <[0..9]>+ ]?
      | '.' <[0..9]> <[0..9_]>*
      | <[0..9]> <[0..9_]>* [ <[eE]> <[+-]>? <[0..9]>+ ]?
    }

    token string {
        $<pfx>=<[fFrRbB]>?
        [ <s3> | <d3> | <s1> | <d1> ]
    }
    token s3 { "'''" .*? "'''" }
    token d3 { '"""' .*? '"""' }
    token s1 { "'" [ '\\' . | <-['\\\n]> ]* "'" }
    token d1 { '"' [ '\\' . | <-["\\\n]> ]* '"' }
}

# =========================================================================
# ACTIONS  —  build a plain-hash AST
# =========================================================================
sub nlist($caps) { my @o; for @($caps) -> $c { @o.push($c.made) } @o }
sub nstrs($caps) { my @o; return @o unless $caps; for @($caps) -> $c { my $s = (~$c).trim; @o.push($s) if $s ne '' } @o }
sub fold-l($terms, $ops) {
    my @t = nlist($terms); my @o = nstrs($ops);
    my $acc = @t[0];
    for ^@o.elems -> $i { $acc = %( t => 'binop', op => @o[$i], l => $acc, r => @t[$i + 1] ) }
    $acc;
}
sub mktuple(@items, $trailing) {
    (@items.elems == 1 && !$trailing) ?? @items[0] !! %( t => 'tuple', items => @items );
}

class Actions {
    method TOP($/) { make %( t => 'module', body => nlist($<stmt>) ) }

    method stmt:sym<if>($/) {
        my @clauses = [ %( test => $<test>.made, body => $<suite>.made ) ];
        for @($<elifc>) -> $e { @clauses.push($e.made) }
        make %( t => 'if', clauses => @clauses, orelse => ($<elsec> ?? $<elsec>.made !! []) );
    }
    method elifc($/) { make %( test => $<test>.made, body => $<suite>.made ) }
    method elsec($/) { make $<suite>.made }
    method stmt:sym<while>($/) {
        make %( t => 'while', test => $<test>.made, body => $<suite>.made,
                orelse => ($<elsec> ?? $<elsec>.made !! []) );
    }
    method stmt:sym<for>($/) {
        make %( t => 'for', target => $<exprlist>.made, iter => $<testlist>.made,
                body => $<suite>.made, orelse => ($<elsec> ?? $<elsec>.made !! []) );
    }
    method stmt:sym<def>($/) {
        my @params; my $star = Nil;
        if $<params> {
            for @($<params>.made) -> $p {
                $p<star> ?? ($star = $p<name>) !! @params.push($p);
            }
        }
        make %( t => 'def', name => ~$<name>, params => @params, star => $star, body => $<suite>.made );
    }
    method params($/) { make nlist($<param>) }
    method param($/) {
        if (~$/).trim.starts-with('*') { make %( star => True, name => ~$<name> ) }
        else { make %( star => False, name => ~$<name>, default => ($<test> ?? $<test>.made !! Nil) ) }
    }
    method stmt:sym<simple>($/) { make %( t => 'seq', body => nlist($<small>) ) }
    method suite($/) {
        my @body;
        if $<stmt> {
            for @($<stmt>) -> $s {
                my $m = $s.made;
                $m<t> eq 'seq' ?? (@body.append($m<body>)) !! @body.push($m);
            }
        }
        else { @body = nlist($<small>) }   # inline suite: `if x: stmt`
        make @body;
    }

    method small:sym<return>($/)   { make %( t => 'return', value => ($<testlist> ?? $<testlist>.made !! Nil) ) }
    method small:sym<pass>($/)     { make %( t => 'pass' ) }
    method small:sym<break>($/)    { make %( t => 'break' ) }
    method small:sym<continue>($/) { make %( t => 'continue' ) }
    method small:sym<global>($/)   { make %( t => 'global', names => nstrs($<name>) ) }
    method small:sym<del>($/)      { make %( t => 'del', target => $<testlist>.made ) }
    method small:sym<expr>($/) {
        my $first = $<testlist>.made;
        if $<augop> {
            make %( t => 'augassign', op => (~$<augop>).trim, target => $first, value => $<rhs>.made );
        }
        elsif $<more> {
            my @more = nlist($<more>);
            my @targets = ($first, |@more[0 ..^ @more.end]);
            make %( t => 'assign', targets => @targets, value => @more[*-1] );
        }
        else { make %( t => 'expr', value => $first ) }
    }

    method exprlist($/) { make mktuple(nlist($<bitor>), (~$/).trim.ends-with(',')) }
    method testlist($/) { make mktuple(nlist($<test>), (~$/).trim.ends-with(',')) }

    method test:sym<lambda>($/) {
        my @params; my $star = Nil;
        if $<params> { for @($<params>.made) -> $p { $p<star> ?? ($star = $p<name>) !! @params.push($p) } }
        make %( t => 'lambda', params => @params, star => $star, body => $<test>.made );
    }
    method test:sym<cond>($/) {
        my @o = nlist($<orx>);
        if @o.elems == 2 { make %( t => 'ifexp', body => @o[0], test => @o[1], orelse => $<test>.made ) }
        else { make @o[0] }
    }
    method orx($/) {
        my @a = nlist($<andx>);
        make @a.elems == 1 ?? @a[0] !! %( t => 'boolop', op => 'or', values => @a );
    }
    method andx($/) {
        my @a = nlist($<notx>);
        make @a.elems == 1 ?? @a[0] !! %( t => 'boolop', op => 'and', values => @a );
    }
    method notx($/) {
        my $e = $<comparison>.made;
        $e = %( t => 'unary', op => 'not', e => $e ) for ^(nstrs($<neg>).elems);
        make $e;
    }
    method comparison($/) {
        my @b = nlist($<bitor>);
        if @b.elems == 1 { make @b[0] }
        else {
            my @ops = nstrs($<compop>).map({ .subst(/\s+/, ' ', :g) });
            make %( t => 'compare', left => @b[0], ops => @ops, rest => @b[1 .. *] );
        }
    }
    method bitor($/)  { make fold-l($<bitxor>, nstrs($<bitxor>).elems > 1 ?? ['|'] xx (nlist($<bitxor>).elems - 1) !! []) }
    method bitxor($/) { make fold-join($<bitand>, '^') }
    method bitand($/) { make fold-join($<shift>, '&') }
    method shift($/)  { make fold-l($<arith>, $<op>) }
    method arith($/)  { make fold-l($<term>, $<op>) }
    method term($/)   { make fold-l($<factor>, $<op>) }
    method factor($/) {
        my $e = $<power>.made;
        for nstrs($<op>).reverse -> $o { $e = %( t => 'unary', op => $o, e => $e ) }
        make $e;
    }
    method power($/) {
        if $<factor> { make %( t => 'binop', op => '**', l => $<atomexpr>.made, r => $<factor>.made ) }
        else { make $<atomexpr>.made }
    }
    method atomexpr($/) {
        my $base = $<atom>.made;
        for @($<trailer>) -> $t {
            my $tm = $t.made;
            given $tm<k> {
                when 'call'  { $base = %( t => 'call', func => $base, args => $tm<args>, kwargs => $tm<kwargs>, star => $tm<star> ) }
                when 'index' { $base = %( t => 'subscript', obj => $base, sx => $tm<sx> ) }
                when 'dot'   { $base = %( t => 'attr', obj => $base, name => $tm<name> ) }
            }
        }
        make $base;
    }
    method trailer:sym<call>($/) {
        my @args; my @kw; my @star;
        if $<comparg> {
            my $g = $<comparg>.made;
            @args.push(%( t => 'comp', kind => 'list', elt => $g<elt>, gens => $g<gen> ));
            make %( k => 'call', args => @args, kwargs => @kw, star => @star );
            return;
        }
        if $<arglist> {
            for @($<arglist>.made) -> $a {
                given $a<k> { when 'pos' { @args.push($a<v>) } when 'kw' { @kw.push($a) } when 'star' { @star.push($a<v>) } }
            }
        }
        make %( k => 'call', args => @args, kwargs => @kw, star => @star );
    }
    method trailer:sym<index>($/) { make %( k => "index", sx => $<subscript>.made ) }
    method trailer:sym<dot>($/)   { make %( k => 'dot', name => ~$<name> ) }
    method arglist($/) { make nlist($<arg>) }
    method comparg($/) { make %( elt => $<test>.made, gen => $<comp>.made ) }
    method arg:sym<star>($/) { make %( k => 'star', v => $<test>.made ) }
    method arg:sym<kw>($/)   { make %( k => 'kw', name => ~$<name>, v => $<test>.made ) }
    method arg:sym<pos>($/)  { make %( k => 'pos', v => $<test>.made ) }

    method subscript($/) { make $<slice>.made }
    method slice($/) {
        if $<idx> { make %( t => 'idx', v => $<idx>.made ) }
        else {
            make %( t => 'slice',
                    lo => ($<lo> ?? $<lo>.made !! Nil),
                    hi => ($<hi> ?? $<hi>.made !! Nil),
                    step => ($<step> ?? $<step>.made !! Nil) );
        }
    }

    method atom:sym<paren>($/) {
        unless $<collection> { make %( t => 'tuple', items => [] ); return }
        my $c = $<collection>.made;
        if $c<comp> { make %( t => 'comp', kind => 'list', elt => $c<elt>, gens => $c<gen> ) }
        elsif $c<items>.elems == 1 && !$c<trailing> { make $c<items>[0] }   # grouping parens
        else { make %( t => 'tuple', items => $c<items> ) }
    }
    method atom:sym<list>($/) {
        unless $<collection> { make %( t => 'list', items => [] ); return }
        my $c = $<collection>.made;
        if $c<comp> { make %( t => 'comp', kind => 'list', elt => $c<elt>, gens => $c<gen> ) }
        else { make %( t => 'list', items => $c<items> ) }
    }
    method atom:sym<dict>($/) {
        unless $<mapping> { make %( t => 'dict', pairs => [] ); return }
        make $<mapping>.made;
    }
    method collection($/) {
        if $<comp> { make %( comp => True, elt => $<test>.made, gen => $<comp>.made ) }
        else {
            my $cl = $<clist>.made;
            my @items;
            @items.push($<test>.made);
            @items.append($cl<items>);
            make %( comp => False, items => @items, trailing => $cl<trailing> );
        }
    }
    method clist($/) { make %( items => nlist($<test>), trailing => ?(~$/).trim.ends-with(',') ) }
    method mapping($/) {
        if $<dpair> {
            my $p = $<dpair>.made;
            if $<comp> { make %( t => 'dictcomp', k => $p<k>, v => $p<v>, gens => $<comp>.made ) }
            else { my @pairs; @pairs.push($p); @pairs.append($<dtail>.made) if $<dtail>; make %( t => 'dict', pairs => @pairs ) }
        }
        else {
            if $<comp> { make %( t => 'comp', kind => 'set', elt => $<test>.made, gens => $<comp>.made ) }
            else { my $cl = $<clist>.made; my @items; @items.push($<test>.made); @items.append($cl<items>); make %( t => 'set', items => @items ) }
        }
    }
    method dtail($/) { make nlist($<dpair>) }
    method dpair($/) { make %( k => $<k>.made, v => $<v>.made ) }
    method comp($/) { make nlist($<forclause>) }
    method forclause($/) { make %( target => $<exprlist>.made, iter => $<iter>.made, ifs => nlist($<cif>) ) }
    method atom:sym<num>($/)   { make %( t => 'num', v => num-val(~$<number>) ) }
    method atom:sym<str>($/)   { make str-node($<string>) }
    method atom:sym<name>($/)  { make %( t => 'name', id => ~$<name> ) }
    method atom:sym<true>($/)  { make %( t => 'const', v => True ) }
    method atom:sym<false>($/) { make %( t => 'const', v => False ) }
    method atom:sym<none>($/)  { make %( t => 'const', v => 'None' ) }
}

sub fold-join($terms, Str $op) {
    my @t = nlist($terms);
    my $acc = @t[0];
    for 1 ..^ @t.elems -> $i { $acc = %( t => 'binop', op => $op, l => $acc, r => @t[$i] ) }
    $acc;
}
sub num-val(Str $t is copy) {
    $t = $t.subst('_', '', :g);
    return :16($t.substr(2)) if $t.starts-with('0x');
    return :2($t.substr(2))  if $t.starts-with('0b');
    ($t.contains('.') || $t.lc.contains('e')) ?? (+$t).Num !! $t.Int;   # floats are IEEE doubles, like Python
}
sub str-node($caps) {
    # concatenate adjacent string literals; mark f-strings
    my @parts;
    my $fstr = False;
    for @($caps) -> $s {
        my $pfx = ~$s<pfx>;
        $fstr = True if $pfx.lc.contains('f');
        @parts.push(%( pfx => $pfx.lc, raw => str-body(~$s) ));
    }
    %( t => 'str', fstr => $fstr, parts => @parts );
}
sub str-body(Str $lit is copy) {
    $lit .= subst(/^ <[fFrRbB]>* /, '');
    if $lit.starts-with("'''") || $lit.starts-with('"""') { return $lit.substr(3, $lit.chars - 6) }
    $lit.substr(1, $lit.chars - 2);
}

# =========================================================================
# EVALUATOR
# =========================================================================
sub exec-stmts(@stmts, $env) { for @stmts -> $s { exec-stmt($s, $env) } }

sub exec-stmt($n, $env) {
    given $n<t> {
        when 'seq'    { exec-stmts($n<body>, $env) }
        when 'expr'   { pyeval($n<value>, $env) }
        when 'assign' { my $v = pyeval($n<value>, $env); assign-to($_, $v, $env) for @($n<targets>) }
        when 'augassign' { exec-aug($n, $env) }
        when 'if' {
            for @($n<clauses>) -> $c {
                if truthy(pyeval($c<test>, $env)) { exec-stmts($c<body>, $env); return }
            }
            exec-stmts($n<orelse>, $env);
        }
        when 'while' {
            my $broke = False;
            while truthy(pyeval($n<test>, $env)) {
                {
                    exec-stmts($n<body>, $env);
                    CATCH { when BreakX { $broke = True } when ContX { } }
                }
                last if $broke;
            }
            exec-stmts($n<orelse>, $env) unless $broke;
        }
        when 'for' {
            my @items = py-iter(pyeval($n<iter>, $env));
            my $broke = False;
            for @items -> $it {
                assign-to($n<target>, $it, $env);
                {
                    exec-stmts($n<body>, $env);
                    CATCH { when BreakX { $broke = True } when ContX { } }
                }
                last if $broke;
            }
            exec-stmts($n<orelse>, $env) unless $broke;
        }
        when 'def' {
            $env.put($n<name>, PyFunc.new(name => $n<name>, params => $n<params>,
                     star => $n<star>, body => $n<body>, env => $env));
        }
        when 'return'   { RetX.new(value => ($n<value> ?? pyeval($n<value>, $env) !! NONE)).throw }
        when 'pass'     { }
        when 'break'    { BreakX.new.throw }
        when 'continue' { ContX.new.throw }
        when 'global'   { $env.put('__global__' ~ $_, True) for @($n<names>) }
        when 'del'      { do-del($n<target>, $env) }
        default { py-raise("SyntaxError", "cannot exec $n<t>") }
    }
}

sub exec-aug($n, $env) {
    my $cur = pyeval($n<target>, $env);
    my $rhs = pyeval($n<value>, $env);
    my $op = $n<op>.subst('=', '');
    assign-to($n<target>, py-binop($op, $cur, $rhs), $env);
}

# ---- assignment targets ----
sub env-assign($env, $name, $v) {
    if $env.has-local('__global__' ~ $name) { $env.glob.put($name, $v) }
    else { $env.put($name, $v) }
}
sub assign-to($target, $v, $env) {
    given $target<t> {
        when 'name' { env-assign($env, $target<id>, $v) }
        when 'tuple' | 'list' {
            my @vals = py-iter($v);
            my @tg = @($target<items>);
            py-raise("ValueError", "not enough values to unpack") unless @vals.elems == @tg.elems;
            for ^@tg.elems -> $i { assign-to(@tg[$i], @vals[$i], $env) }
        }
        when 'subscript' {
            my $obj = pyeval($target<obj>, $env);
            setitem($obj, $target<sx>, $v, $env);
        }
        default { py-raise("SyntaxError", "cannot assign to $target<t>") }
    }
}
sub do-del($target, $env) {
    if $target<t> eq 'subscript' {
        my $obj = pyeval($target<obj>, $env);
        my $key = pyeval($target<sx><v>, $env);
        given $obj { when PyDict { $obj.del($key) } when PyList { $obj.items.splice(norm-idx($key, $obj.items.elems), 1) } }
    }
}

# ---- expression evaluation ----
sub pyeval($n, $env) {
    given $n<t> {
        when 'num'   { return $n<v> }
        when 'const' { return $n<v> eq 'None' ?? NONE !! $n<v> }
        when 'str'   { return eval-str($n, $env) }
        when 'name'  {
            my $id = $n<id>;
            return NONE if $id eq 'None';
            return $env.get($id);
        }
        when 'tuple' { return PyTuple.new(items => $n<items>.map({ pyeval($_, $env) }).Array) }
        when 'list'  { return PyList.new(items => $n<items>.map({ pyeval($_, $env) }).Array) }
        when 'set'   { my $d = PyDict.new; $d.put(pyeval($_, $env), True) for @($n<items>); return set-from($d) }
        when 'dict'  {
            my $d = PyDict.new;
            for @($n<pairs>) -> $p { $d.put(pyeval($p<k>, $env), pyeval($p<v>, $env)) }
            return $d;
        }
        when 'boolop' { return eval-bool($n, $env) }
        when 'unary'  { return eval-unary($n, $env) }
        when 'binop'  { return py-binop($n<op>, pyeval($n<l>, $env), pyeval($n<r>, $env)) }
        when 'compare' { return eval-compare($n, $env) }
        when 'ifexp'  { return truthy(pyeval($n<test>, $env)) ?? pyeval($n<body>, $env) !! pyeval($n<orelse>, $env) }
        when 'call'   { return eval-call($n, $env) }
        when 'subscript' { return eval-subscript($n, $env) }
        when 'attr'   { return eval-attr(pyeval($n<obj>, $env), $n<name>) }
        when 'lambda' { return PyFunc.new(name => '<lambda>', params => $n<params>, star => $n<star>, body => [ %( t => 'return', value => $n<body> ) ], env => $env) }
        when 'comp'   { return eval-comp($n, $env) }
        when 'dictcomp' { return eval-dictcomp($n, $env) }
        default { py-raise("SyntaxError", "cannot eval $n<t>") }
    }
}

sub eval-bool($n, $env) {
    my $last;
    for @($n<values>) -> $v {
        $last = pyeval($v, $env);
        if $n<op> eq 'and' { return $last unless truthy($last) }
        else               { return $last if truthy($last) }
    }
    $last;
}
sub eval-unary($n, $env) {
    my $v = pyeval($n<e>, $env);
    given $n<op> {
        when 'not' { return !truthy($v) }
        when '-'   { return -pnum($v) }
        when '+'   { return pnum($v) }
        when '~'   { return +^pnum($v).Int }
    }
}
sub eval-compare($n, $env) {
    my $left = pyeval($n<left>, $env);
    my @rest = @($n<rest>);
    for ^@rest.elems -> $i {
        my $right = pyeval(@rest[$i], $env);
        return False unless cmp-op($n<ops>[$i], $left, $right);
        $left = $right;
    }
    True;
}
sub cmp-op($op, $a, $b) {
    given $op {
        when '=='     { py-eq($a, $b) }
        when '!='     { !py-eq($a, $b) }
        when '<'      { py-lt($a, $b) }
        when '>'      { py-lt($b, $a) }
        when '<='     { !py-lt($b, $a) }
        when '>='     { !py-lt($a, $b) }
        when 'in'     { py-contains($b, $a) }
        when 'not in' { !py-contains($b, $a) }
        when 'is'     { $a === $b || (is-none($a) && is-none($b)) }
        when 'is not' { !($a === $b || (is-none($a) && is-none($b))) }
    }
}

# ---- coercions & truthiness ----
sub pnum($v) {
    return $v if $v ~~ Int|Rat|Num;
    return $v ?? 1 !! 0 if $v ~~ Bool;
    py-raise("TypeError", "expected a number");
}
sub is-num($v) { $v ~~ Int|Rat|Num|Bool }
sub truthy($v --> Bool) {
    given $v {
        when PyNone { False }
        when Bool   { $v }
        when Int|Rat|Num { $v != 0 }
        when Str    { $v.chars > 0 }
        when PyList { $v.items.elems > 0 }
        when PyTuple { $v.items.elems > 0 }
        when PyDict { $v.keys.elems > 0 }
        when PySet  { $v.d.keys.elems > 0 }
        default     { True }
    }
}
sub py-iter($v) {
    given $v {
        when PyList  { $v.items.Array }
        when PyTuple { $v.items.Array }
        when Str     { $v.comb.Array }
        when PyDict  { $v.keys.Array }
        when PySet   { $v.d.keys.Array }
        when PyRange { $v.values }
        default { py-raise("TypeError", "object is not iterable") }
    }
}

# ---- binary operators ----
sub py-binop($op, $a, $b) {
    given $op {
        when '+' {
            if $a ~~ PyList  && $b ~~ PyList  { my @m = $a.items.Array; @m.append($b.items); return PyList.new(items => @m) }
            if $a ~~ PyTuple && $b ~~ PyTuple { my @m = $a.items.Array; @m.append($b.items); return PyTuple.new(items => @m) }
            return $a ~ $b if $a ~~ Str && $b ~~ Str;
            return pnum($a) + pnum($b) if is-num($a) && is-num($b);
            py-raise("TypeError", "unsupported operand type(s) for +");
        }
        when '-' { return pnum($a) - pnum($b) }
        when '*' {
            return str-repeat($a, pnum($b).Int) if $a ~~ Str && is-num($b);
            return str-repeat($b, pnum($a).Int) if $b ~~ Str && is-num($a);
            return list-repeat($a, pnum($b).Int) if $a ~~ PyList && is-num($b);
            return list-repeat($b, pnum($a).Int) if $b ~~ PyList && is-num($a);
            return pnum($a) * pnum($b);
        }
        when '/'  { my $d = pnum($b); py-raise("ZeroDivisionError", "division by zero") if $d == 0; return (pnum($a) / $d).Num }
        when '//' { my $d = pnum($b); py-raise("ZeroDivisionError", "integer division or modulo by zero") if $d == 0;
                    my $r = (pnum($a) / $d).floor; return (is-float($a) || is-float($b)) ?? $r.Num !! $r }
        when '%'  {
            return str-format($a, $b) if $a ~~ Str;
            my $d = pnum($b); py-raise("ZeroDivisionError", "integer division or modulo by zero") if $d == 0;
            return pnum($a) % $d;
        }
        when '**' { my $r = pnum($a) ** pnum($b); return $r }
        when '&'  { return pnum($a).Int +& pnum($b).Int }
        when '|'  { return pnum($a).Int +| pnum($b).Int }
        when '^'  { return pnum($a).Int +^ pnum($b).Int }
        when '<<' { return pnum($a).Int +< pnum($b).Int }
        when '>>' { return pnum($a).Int +> pnum($b).Int }
    }
}
sub is-float($v) { $v ~~ Rat|Num && $v !~~ Int }
sub str-repeat(Str $s, Int $n) { $n > 0 ?? $s x $n !! '' }
sub list-repeat($l, Int $n) { my @o; for ^($n max 0) { @o.append($l.items) }; PyList.new(items => @o) }

sub py-eq($a, $b) {
    return $a == $b if is-num($a) && is-num($b);
    return $a eq $b if $a ~~ Str && $b ~~ Str;
    return is-none($b) if is-none($a);
    return (seq-eq($a.items, $b.items)) if $a ~~ PyList && $b ~~ PyList;
    return (seq-eq($a.items, $b.items)) if $a ~~ PyTuple && $b ~~ PyTuple;
    return dict-eq($a, $b) if $a ~~ PyDict && $b ~~ PyDict;
    False;
}
sub seq-eq(@a, @b) { return False unless @a.elems == @b.elems; for ^@a.elems -> $i { return False unless py-eq(@a[$i], @b[$i]) }; True }
sub dict-eq($a, $b) {
    return False unless $a.keys.elems == $b.keys.elems;
    for $a.keys -> $k { return False unless $b.has($k) && py-eq($a.get($k), $b.get($k)) }
    True;
}
sub py-lt($a, $b) {
    return $a < $b if is-num($a) && is-num($b);
    return $a lt $b if $a ~~ Str && $b ~~ Str;
    if ($a ~~ PyList && $b ~~ PyList) || ($a ~~ PyTuple && $b ~~ PyTuple) {
        my @x = $a.items; my @y = $b.items;
        for ^min(@x.elems, @y.elems) -> $i {
            return True if py-lt(@x[$i], @y[$i]);
            return False if py-lt(@y[$i], @x[$i]);
        }
        return @x.elems < @y.elems;
    }
    py-raise("TypeError", "'<' not supported between these types");
}
sub py-contains($container, $item) {
    given $container {
        when Str     { return $container.contains($item) }
        when PyList  { return $container.items.first({ py-eq($_, $item) }).defined }
        when PyTuple { return $container.items.first({ py-eq($_, $item) }).defined }
        when PyDict  { return $container.has($item) }
        default { py-raise("TypeError", "argument is not iterable") }
    }
}

# ---- subscript / slice ----
sub norm-idx($i, $len) { my $x = pnum($i).Int; $x < 0 ?? $x + $len !! $x }
sub eval-subscript($n, $env) {
    my $obj = pyeval($n<obj>, $env);
    my $sx = $n<sx>;
    if $sx<t> eq 'idx' { return getitem($obj, pyeval($sx<v>, $env)) }
    my $lo = $sx<lo> ?? pnum(pyeval($sx<lo>, $env)).Int !! Nil;
    my $hi = $sx<hi> ?? pnum(pyeval($sx<hi>, $env)).Int !! Nil;
    my $st = $sx<step> ?? pnum(pyeval($sx<step>, $env)).Int !! 1;
    do-slice($obj, $lo, $hi, $st);
}
sub getitem($obj, $key) {
    given $obj {
        when PyList  { my $i = norm-idx($key, $obj.items.elems); py-raise("IndexError", "list index out of range") unless 0 <= $i < $obj.items.elems; return $obj.items[$i] }
        when PyTuple { my $i = norm-idx($key, $obj.items.elems); py-raise("IndexError", "tuple index out of range") unless 0 <= $i < $obj.items.elems; return $obj.items[$i] }
        when Str     { my $i = norm-idx($key, $obj.chars); py-raise("IndexError", "string index out of range") unless 0 <= $i < $obj.chars; return $obj.substr($i, 1) }
        when PyDict  { py-raise("KeyError", py-repr($key)) unless $obj.has($key); return $obj.get($key) }
        default { py-raise("TypeError", "object is not subscriptable") }
    }
}
sub setitem($obj, $sx, $v, $env) {
    given $obj {
        when PyList { my $i = norm-idx(pyeval($sx<v>, $env), $obj.items.elems); $obj.items[$i] = $v }
        when PyDict { $obj.put(pyeval($sx<v>, $env), $v) }
        default { py-raise("TypeError", "object does not support item assignment") }
    }
}
sub do-slice($obj, $lo, $hi, $st) {
    my @src = $obj ~~ Str ?? $obj.comb.Array !! $obj.items.Array;
    my $len = @src.elems;
    my $step = $st == 0 ?? py-raise("ValueError", "slice step cannot be zero") !! $st;
    my ($start, $stop);
    if $step > 0 {
        $start = $lo.defined ?? clamp-idx($lo, $len, 0) !! 0;
        $stop  = $hi.defined ?? clamp-idx($hi, $len, $len) !! $len;
    }
    else {
        $start = $lo.defined ?? clamp-idx($lo, $len, $len - 1) !! $len - 1;
        $stop  = $hi.defined ?? clamp-idx($hi, $len, -1) !! -1;
    }
    my @out;
    my $i = $start;
    while ($step > 0 ?? $i < $stop !! $i > $stop) { @out.push(@src[$i]) if 0 <= $i < $len; $i += $step }
    $obj ~~ Str ?? @out.join('') !! PyList.new(items => @out);
}
sub clamp-idx($i is copy, $len, $default) {
    $i += $len if $i < 0;
    $i = 0 if $i < 0;
    $i = $len if $i > $len;
    $i;
}

# =========================================================================
# FORMATTING  (str / repr the way CPython prints)
# =========================================================================
sub py-float-repr(Num $f) {
    return 'inf'  if $f == Inf;
    return '-inf' if $f == -Inf;
    return 'nan'  if $f.isNaN;
    my $s;
    for 1 .. 17 -> $p {
        $s = sprintf('%.' ~ $p ~ 'g', $f);
        last if +$s == $f;
    }
    # ensure it reads back as a float
    $s ~= '.0' unless $s.contains('.') || $s.lc.contains('e') || $s.lc.contains('inf') || $s.lc.contains('nan');
    $s;
}
sub py-str($v) {
    given $v {
        when PyNone  { 'None' }
        when Bool    { $v ?? 'True' !! 'False' }
        when Int     { ~$v }
        when Rat|Num { py-float-repr($v.Num) }
        when Str     { $v }
        when PyList | PyTuple | PyDict | PySet | PyRange { py-repr($v) }
        when PyFunc  { "<function {$v.name}>" }
        when NativeFn { "<built-in function {$v.name}>" }
        default { ~$v }
    }
}
sub py-repr($v) {
    given $v {
        when Str { return str-repr($v) }
        when PyList  { return '[' ~ $v.items.map(&py-repr).join(', ') ~ ']' }
        when PyTuple {
            my @r = $v.items.map(&py-repr);
            return @r.elems == 1 ?? "({@r[0]},)" !! '(' ~ @r.join(', ') ~ ')';
        }
        when PyDict {
            return '{' ~ $v.pairs.map({ py-repr(.<k>) ~ ': ' ~ py-repr(.<v>) }).join(', ') ~ '}';
        }
        when PySet {
            return 'set()' unless $v.d.keys.elems;
            return '{' ~ $v.d.keys.map(&py-repr).join(', ') ~ '}';
        }
        when PyRange {
            return $v.step == 1 ?? "range({$v.start}, {$v.stop})" !! "range({$v.start}, {$v.stop}, {$v.step})";
        }
        default { py-str($v) }
    }
}
sub str-repr(Str $s) {
    my $q = ($s.contains("'") && !$s.contains('"')) ?? '"' !! "'";
    my $out = $q;
    for $s.comb -> $c {
        given $c {
            when "\\" { $out ~= "\\\\" }
            when "\n" { $out ~= "\\n" }
            when "\t" { $out ~= "\\t" }
            when "\r" { $out ~= "\\r" }
            when $q   { $out ~= "\\" ~ $c }
            default   { $out ~= $c }
        }
    }
    $out ~ $q;
}

# =========================================================================
# STRINGS  (escape decoding + f-strings)
# =========================================================================
sub eval-str($n, $env) {
    my $out = '';
    for @($n<parts>) -> $p {
        my $raw = $p<raw>;
        my $s = $p<pfx>.contains('r') ?? $raw !! decode-escapes($raw);
        $s = interp-fstring($s, $env) if $p<pfx>.contains('f');
        $out ~= $s;
    }
    $out;
}
sub decode-escapes(Str $s) {
    my @c = $s.comb; my $n = @c.elems; my $o = ''; my $i = 0;
    while $i < $n {
        if @c[$i] eq '\\' && $i + 1 < $n {
            my $e = @c[$i + 1]; $i += 2;
            given $e {
                when 'n' { $o ~= "\n" } when 't' { $o ~= "\t" } when 'r' { $o ~= "\r" }
                when '0' { $o ~= "\0" } when '\\' { $o ~= "\\" }
                when '"' { $o ~= '"' } when "'" { $o ~= "'" }
                default  { $o ~= '\\' ~ $e }
            }
        }
        else { $o ~= @c[$i]; $i++ }
    }
    $o;
}
sub interp-fstring(Str $s, $env) {
    my @c = $s.comb; my $n = @c.elems; my $o = ''; my $i = 0;
    while $i < $n {
        my $ch = @c[$i];
        if $ch eq '{' && $i + 1 < $n && @c[$i + 1] eq '{' { $o ~= '{'; $i += 2 }
        elsif $ch eq '}' && $i + 1 < $n && @c[$i + 1] eq '}' { $o ~= '}'; $i += 2 }
        elsif $ch eq '{' {
            my $depth = 1; my $expr = ''; $i++;
            while $i < $n && $depth > 0 {
                $depth++ if @c[$i] eq '{';
                $depth-- if @c[$i] eq '}';
                $expr ~= @c[$i] if $depth > 0;
                $i++;
            }
            $o ~= fstring-slot($expr.trim, $env);
        }
        else { $o ~= $ch; $i++ }
    }
    $o;
}
sub fstring-slot(Str $slot, $env) {
    my $expr = $slot; my $spec = ''; my $conv = '';
    if $expr ~~ /^ (.*?) '!' (<[rsa]>) $/ { $expr = ~$0; $conv = ~$1 }
    if $slot ~~ /^ (<-[:]>+) ':' (.*) $/ { $expr = ~$0; $spec = ~$1; $expr ~~ s/ '!' <[rsa]> $//; }
    my $v = eval-subexpr($expr, $env);
    return py-repr($v) if $conv eq 'r';
    return py-format($v, $spec) if $spec ne '';
    py-str($v);
}
sub eval-subexpr(Str $src, $env) {
    # rakupp can't parse from a named rule, so parse a one-line module and dig out
    # the expression node.
    my $m = PyGrammar.parse(preprocess($src ~ "\n"), actions => Actions.new);
    py-raise("SyntaxError", "f-string: {$src}") unless $m;
    my $stmt = $m.made<body>[0];
    $stmt = $stmt<body>[0] if $stmt<t> eq 'seq';
    pyeval($stmt<value>, $env);
}
sub py-format($v, Str $spec) {
    # minimal format-spec support: [align][width][,][.prec][type]
    if $spec ~~ /^ (<[<>^]>)? ('0')? (\d+)? (','?) ('.' \d+)? (<[dfgexsb%]>)? $/ {
        my $align = ~$0; my $zero = ~$1; my $width = ~$2; my $comma = ~$3; my $prec = ~$4; my $type = ~$5;
        my $s;
        given $type {
            when 'd' { $s = pnum($v).Int.Str }
            when 'f' { my $p = $prec ?? $prec.substr(1).Int !! 6; $s = sprintf('%.' ~ $p ~ 'f', pnum($v).Num) }
            when 'e' { my $p = $prec ?? $prec.substr(1).Int !! 6; $s = sprintf('%.' ~ $p ~ 'e', pnum($v).Num) }
            when 'g' { $s = sprintf('%' ~ $prec ~ 'g', pnum($v).Num) }
            when 'x' { $s = sprintf('%x', pnum($v).Int) }
            when 'b' { $s = pnum($v).Int.base(2) }
            when '%' { my $p = $prec ?? $prec.substr(1).Int !! 6; $s = sprintf('%.' ~ $p ~ 'f', pnum($v).Num * 100) ~ '%' }
            when 's' { $s = py-str($v) }
            default  {
                if $prec && $v ~~ Numeric { $s = sprintf('%.' ~ $prec.substr(1) ~ 'f', $v.Num) }
                else { $s = py-str($v) }
            }
        }
        $s = comma-group($s) if $comma;
        if $width {
            my $w = $width.Int;
            my $pad = $align eq '' && $zero eq '0' ?? '0' !! ' ';
            my $a = $align ne '' ?? $align !! ($v ~~ Numeric && $v !~~ Bool ?? '>' !! '<');
            $s = pad-str($s, $w, $a, $pad);
        }
        return $s;
    }
    py-str($v);
}
sub comma-group(Str $s) {
    my ($int, $frac) = $s.split('.');
    my $neg = $int.starts-with('-'); $int = $int.substr(1) if $neg;
    my $g = $int.flip.comb(3).join(',').flip;
    ($neg ?? '-' !! '') ~ $g ~ ($frac.defined ?? '.' ~ $frac !! '');
}
sub pad-str(Str $s, $w, $a, $pad) {
    return $s if $s.chars >= $w;
    my $n = $w - $s.chars;
    given $a {
        when '<' { return $s ~ (' ' x $n) }
        when '>' { return ($pad x $n) ~ $s }
        when '^' { my $l = ($n / 2).Int; return (' ' x $l) ~ $s ~ (' ' x ($n - $l)) }
    }
    $s;
}
sub str-format(Str $fmt, $args) {
    my @vals = $args ~~ PyTuple ?? $args.items.Array !! [$args];
    my $i = 0;
    $fmt.subst(/ '%' (<[-+ 0#]>*) (\d+)? ('.' \d+)? (<[diouxXeEfFgGsr%]>) /, -> $/ {
        my $conv = ~$3;
        return '%' if $conv eq '%';
        my $v = @vals[$i++];
        my $spec = '%' ~ ~$0 ~ ~$1 ~ ~$2;
        given $conv {
            when 'd' | 'i' { sprintf($spec ~ 'd', pnum($v).Int) }
            when 'f' | 'F' | 'e' | 'E' | 'g' | 'G' { sprintf($spec ~ $conv, pnum($v).Num) }
            when 'x' | 'X' | 'o' { sprintf($spec ~ $conv, pnum($v).Int) }
            when 's' { sprintf($spec ~ 's', py-str($v)) }
            when 'r' { sprintf($spec ~ 's', py-repr($v)) }
            default  { ~$/ }
        }
    }, :g);
}

# =========================================================================
# CALLS  +  METHODS
# =========================================================================
sub eval-args($n, $env) {
    my @args = $n<args>.map({ pyeval($_, $env) });
    for @($n<star>) -> $s { @args.append(py-iter(pyeval($s, $env))) }
    @args;
}
sub eval-kwargs($n, $env) {
    my %kw;
    for @($n<kwargs>) -> $k { %kw{$k<name>} = pyeval($k<v>, $env) }
    %kw;
}
sub eval-call($n, $env) {
    if $n<func><t> eq 'attr' {
        my $recv = pyeval($n<func><obj>, $env);
        return call-method($recv, $n<func><name>, eval-args($n, $env), eval-kwargs($n, $env));
    }
    my $f = pyeval($n<func>, $env);
    call-value($f, eval-args($n, $env), eval-kwargs($n, $env));
}
sub call-value($f, @args, %kw) {
    given $f {
        when NativeFn { return $f.fn.(@args, %kw) }
        when PyFunc   { return call-func($f, @args, %kw) }
        default { py-raise("TypeError", "object is not callable") }
    }
}
sub call-func(PyFunc $f, @args, %kw) {
    my $local = Env.new(parent => $f.env, globals => $f.env.glob);
    my @params = $f.params;
    my $i = 0;
    for @params -> $p {
        if $i < @args.elems { $local.put($p<name>, @args[$i]); $i++ }
        elsif %kw{$p<name>}:exists { $local.put($p<name>, %kw{$p<name>}) }
        elsif $p<default>.defined { $local.put($p<name>, pyeval($p<default>, $f.env)) }
        else { py-raise("TypeError", "{$f.name}() missing required argument: '{$p<name>}'") }
    }
    if $f.star.defined { $local.put($f.star, PyTuple.new(items => @args[$i .. *].grep(*.defined).Array)) }
    my $ret = NONE;
    {
        exec-stmts($f.body, $local);
        CATCH { when RetX { $ret = .value } }
    }
    $ret;
}

sub eval-attr($obj, $name) {
    py-raise("AttributeError", "'{type-name($obj)}' object has no attribute '$name'");
}

# =========================================================================
# COMPREHENSIONS
# =========================================================================
sub comp-drive(@gens, $idx, $ce, &emit) {
    if $idx >= @gens.elems { emit(); return }
    my $g = @gens[$idx];
    for py-iter(pyeval($g<iter>, $ce)) -> $it {
        assign-to($g<target>, $it, $ce);
        my $ok = True;
        for @($g<ifs>) -> $c { $ok = False unless truthy(pyeval($c, $ce)) }
        next unless $ok;
        comp-drive(@gens, $idx + 1, $ce, &emit);
    }
}
sub eval-comp($n, $env) {
    my @out;
    my $ce = Env.new(parent => $env, globals => $env.glob);
    comp-drive($n<gens>, 0, $ce, { @out.push(pyeval($n<elt>, $ce)) });
    given $n<kind> {
        when 'set' { my $d = PyDict.new; $d.put($_, True) for @out; return PySet.new(d => $d) }
        default    { return PyList.new(items => @out) }
    }
}
sub eval-dictcomp($n, $env) {
    my $d = PyDict.new;
    my $ce = Env.new(parent => $env, globals => $env.glob);
    comp-drive($n<gens>, 0, $ce, { $d.put(pyeval($n<k>, $ce), pyeval($n<v>, $ce)) });
    $d;
}

sub set-from(PyDict $d) { PySet.new(d => $d) }
sub type-name($v) {
    given $v {
        when PyNone { 'NoneType' } when Bool { 'bool' } when Int { 'int' }
        when Rat|Num { 'float' } when Str { 'str' } when PyList { 'list' }
        when PyTuple { 'tuple' } when PyDict { 'dict' } when PySet { 'set' }
        when PyRange { 'range' } when PyFunc | NativeFn { 'function' }
        default { 'object' }
    }
}

# =========================================================================
# METHODS  (str / list / dict)
# =========================================================================
sub call-method($recv, $name, @args, %kw) {
    given $recv {
        when Str    { return str-method($recv, $name, @args) }
        when PyList { return list-method($recv, $name, @args, %kw) }
        when PyDict { return dict-method($recv, $name, @args) }
        when PySet  { return set-method($recv, $name, @args) }
        default { py-raise("AttributeError", "'{type-name($recv)}' object has no attribute '$name'") }
    }
}
sub str-method(Str $s, $name, @a) {
    given $name {
        when 'upper'      { return $s.uc }
        when 'lower'      { return $s.lc }
        when 'strip'      { return @a ?? $s.trim(@a[0].comb.Set) !! $s.trim }
        when 'lstrip'     { return $s.subst(/^ \s+ /, '') }
        when 'rstrip'     { return $s.subst(/ \s+ $/, '') }
        when 'title'      { return $s.wordcase }
        when 'capitalize' { return $s.chars ?? $s.substr(0,1).uc ~ $s.substr(1).lc !! '' }
        when 'split'      { return str-split($s, @a) }
        when 'rsplit'     { return str-split($s, @a) }
        when 'join'       { return @a[0] ~~ PyList|PyTuple ?? @a[0].items.map(&py-str).join($s) !! py-iter(@a[0]).map(&py-str).join($s) }
        when 'replace'    { return $s.subst(@a[0], @a[1], :g) }
        when 'startswith' { return $s.starts-with(@a[0]) }
        when 'endswith'   { return $s.ends-with(@a[0]) }
        when 'find'       { my $i = $s.index(@a[0]); return $i.defined ?? $i !! -1 }
        when 'rfind'      { my $i = $s.rindex(@a[0]); return $i.defined ?? $i !! -1 }
        when 'index'      { my $i = $s.index(@a[0]); py-raise("ValueError", "substring not found") unless $i.defined; return $i }
        when 'count'      { return @a ?? +$s.comb(@a[0]).elems !! 0 }
        when 'format'     { return str-dot-format($s, @a) }
        when 'zfill'      { my $w = pnum(@a[0]).Int; return $s.chars >= $w ?? $s !! ('0' x ($w - $s.chars)) ~ $s }
        when 'ljust'      { my $w = pnum(@a[0]).Int; my $c = @a > 1 ?? @a[1] !! ' '; return $s ~ ($c x ($w - $s.chars)) }
        when 'rjust'      { my $w = pnum(@a[0]).Int; my $c = @a > 1 ?? @a[1] !! ' '; return ($c x ($w - $s.chars)) ~ $s }
        when 'isdigit'    { return $s.chars > 0 && $s ~~ /^ \d+ $/ ?? True !! False }
        when 'isalpha'    { return $s.chars > 0 && $s ~~ /^ <[A..Za..z]>+ $/ ?? True !! False }
        when 'isalnum'    { return $s.chars > 0 && $s ~~ /^ <[A..Za..z0..9]>+ $/ ?? True !! False }
        when 'isspace'    { return $s.chars > 0 && $s ~~ /^ \s+ $/ ?? True !! False }
        when 'isupper'    { return $s.chars > 0 && $s eq $s.uc && $s ne $s.lc }
        when 'islower'    { return $s.chars > 0 && $s eq $s.lc && $s ne $s.uc }
        default { py-raise("AttributeError", "'str' object has no attribute '$name'") }
    }
}
sub str-split(Str $s, @a) {
    my @parts;
    if @a && !is-none(@a[0]) { @parts = $s.split(@a[0]).Array }
    else { @parts = $s.words.Array }
    PyList.new(items => @parts.map({ ~$_ }).Array);
}
sub str-dot-format(Str $s, @a) {
    my $i = 0;
    $s.subst(/ '{' (<-[}]>*) '}' /, -> $/ {
        my $spec = ~$0;
        my $v = $spec eq '' ?? @a[$i++] !! (($spec ~~ /^ \d+ $/) ?? @a[$spec.Int] !! @a[$i++]);
        my $fs = $spec ~~ /':' (.*)/ ?? ~$0 !! '';
        $fs ne '' ?? py-format($v, $fs) !! py-str($v);
    }, :g);
}
sub list-method($l, $name, @a, %kw = {}) {
    given $name {
        when 'append'  { $l.items.push(@a[0]); return NONE }
        when 'pop'     { py-raise("IndexError", "pop from empty list") unless $l.items.elems;
                         my $i = @a ?? norm-idx(@a[0], $l.items.elems) !! $l.items.end;
                         return $l.items.splice($i, 1)[0] }
        when 'extend'  { $l.items.append(py-iter(@a[0])); return NONE }
        when 'insert'  { my $i = clamp-idx(pnum(@a[0]).Int, $l.items.elems, $l.items.elems); $l.items.splice($i, 0, @a[1]); return NONE }
        when 'remove'  { my $j = $l.items.first({ py-eq($_, @a[0]) }, :k); py-raise("ValueError", "list.remove(x): x not in list") unless $j.defined; $l.items.splice($j, 1); return NONE }
        when 'index'   { my $j = $l.items.first({ py-eq($_, @a[0]) }, :k); py-raise("ValueError", "not in list") unless $j.defined; return $j }
        when 'count'   { return +$l.items.grep({ py-eq($_, @a[0]) }).elems }
        when 'sort'    { $l.items = do-sort($l.items, %kw); return NONE }
        when 'reverse' { $l.items = $l.items.reverse.Array; return NONE }
        when 'copy'    { return PyList.new(items => $l.items.Array) }
        when 'clear'   { $l.items = []; return NONE }
        default { py-raise("AttributeError", "'list' object has no attribute '$name'") }
    }
}
sub dict-method($d, $name, @a) {
    given $name {
        when 'keys'   { return PyList.new(items => $d.keys.Array) }
        when 'values' { return PyList.new(items => $d.pairs.map(*.<v>).Array) }
        when 'items'  { return PyList.new(items => $d.pairs.map({ PyTuple.new(items => [.<k>, .<v>]) }).Array) }
        when 'get'    { return $d.has(@a[0]) ?? $d.get(@a[0]) !! (@a > 1 ?? @a[1] !! NONE) }
        when 'pop'    { if $d.has(@a[0]) { my $v = $d.get(@a[0]); $d.del(@a[0]); return $v }; return @a > 1 ?? @a[1] !! py-raise("KeyError", py-repr(@a[0])) }
        when 'setdefault' { return $d.get(@a[0]) if $d.has(@a[0]); my $v = @a > 1 ?? @a[1] !! NONE; $d.put(@a[0], $v); return $v }
        when 'update' { for py-iter-pairs(@a[0]) -> $p { $d.put($p[0], $p[1]) }; return NONE }
        when 'copy'   { my $c = PyDict.new; $c.put(.<k>, .<v>) for $d.pairs; return $c }
        when 'clear'  { $d.keys = []; $d.map = {}; return NONE }
        default { py-raise("AttributeError", "'dict' object has no attribute '$name'") }
    }
}
sub py-iter-pairs($v) { $v ~~ PyDict ?? $v.pairs.map({ [.<k>, .<v>] }) !! py-iter($v).map({ [.items[0], .items[1]] }) }
sub set-method($s, $name, @a) {
    given $name {
        when 'add'      { $s.d.put(@a[0], True); return NONE }
        when 'discard'  { $s.d.del(@a[0]); return NONE }
        when 'remove'   { py-raise("KeyError", py-repr(@a[0])) unless $s.d.has(@a[0]); $s.d.del(@a[0]); return NONE }
        default { py-raise("AttributeError", "'set' object has no attribute '$name'") }
    }
}

# =========================================================================
# BUILTINS
# =========================================================================
sub do-sort(@items, %opts) {
    my &key = %opts<key>:exists && %opts<key> ~~ PyFunc|NativeFn ?? -> $x { call-value(%opts<key>, [$x], {}) } !! -> $x { $x };
    my @sorted = @items.sort(-> $a, $b {
        my $ka = key($a); my $kb = key($b);
        py-lt($ka, $kb) ?? Order::Less !! (py-lt($kb, $ka) ?? Order::More !! Order::Same)
    }).Array;
    @sorted = @sorted.reverse.Array if %opts<reverse>:exists && truthy(%opts<reverse>);
    @sorted;                             # return sorted copy (rakupp @-params don't alias)
}

sub install-builtins($g) {
    my sub reg($name, &fn) { $g.put($name, NativeFn.new(name => $name, fn => &fn)) }

    reg('print', -> @a, %kw {
        my $sep = %kw<sep>:exists ?? py-str(%kw<sep>) !! ' ';
        my $end = %kw<end>:exists ?? py-str(%kw<end>) !! "\n";
        print @a.map(&py-str).join($sep) ~ $end;
        NONE;
    });
    reg('len', -> @a, %kw {
        given @a[0] {
            when Str { $_.chars } when PyList { $_.items.elems } when PyTuple { $_.items.elems }
            when PyDict { $_.keys.elems } when PySet { $_.d.keys.elems } when PyRange { $_.len }
            default { py-raise("TypeError", "object has no len()") }
        }
    });
    reg('range', -> @a, %kw {
        my ($start, $stop, $step) = 0, 0, 1;
        given @a.elems { when 1 { $stop = pnum(@a[0]).Int } when 2 { $start = pnum(@a[0]).Int; $stop = pnum(@a[1]).Int } default { $start = pnum(@a[0]).Int; $stop = pnum(@a[1]).Int; $step = pnum(@a[2]).Int } }
        PyRange.new(start => $start, stop => $stop, step => $step);
    });
    reg('list',  -> @a, %kw { PyList.new(items => (@a ?? py-iter(@a[0]) !! []).Array) });
    reg('tuple', -> @a, %kw { PyTuple.new(items => (@a ?? py-iter(@a[0]) !! []).Array) });
    reg('dict',  -> @a, %kw { my $d = PyDict.new; if @a { for py-iter-pairs(@a[0]) -> $p { $d.put($p[0], $p[1]) } }; $d.put($_.key, $_.value) for %kw; $d });
    reg('set',   -> @a, %kw { my $d = PyDict.new; if @a { $d.put($_, True) for py-iter(@a[0]) }; PySet.new(d => $d) });
    reg('str',   -> @a, %kw { @a ?? py-str(@a[0]) !! '' });
    reg('repr',  -> @a, %kw { py-repr(@a[0]) });
    reg('int',   -> @a, %kw { @a ?? to-int(@a[0], %kw) !! 0 });
    reg('float', -> @a, %kw { @a ?? pnum-of(@a[0]).Num !! 0e0 });
    reg('bool',  -> @a, %kw { @a ?? truthy(@a[0]) !! False });
    reg('abs',   -> @a, %kw { pnum(@a[0]).abs });
    reg('round', -> @a, %kw { py-round(@a) });
    reg('sum',   -> @a, %kw { my $acc = @a > 1 ?? @a[1] !! 0; for py-iter(@a[0]) -> $x { $acc = py-binop('+', $acc, $x) }; $acc });
    reg('min',   -> @a, %kw { py-minmax(@a, %kw, False) });
    reg('max',   -> @a, %kw { py-minmax(@a, %kw, True) });
    reg('sorted',-> @a, %kw { PyList.new(items => do-sort(py-iter(@a[0]).Array, %kw)) });
    reg('reversed', -> @a, %kw { PyList.new(items => py-iter(@a[0]).reverse.Array) });
    reg('enumerate', -> @a, %kw { my $s = @a > 1 ?? pnum(@a[1]).Int !! 0; my @o; for py-iter(@a[0]) -> $x { @o.push(PyTuple.new(items => [$s++, $x])) }; PyList.new(items => @o) });
    reg('zip', -> @a, %kw { my @seqs = @a.map({ py-iter($_).Array }); my $n = @seqs.map(*.elems).min // 0; my @o; for ^$n -> $i { @o.push(PyTuple.new(items => @seqs.map(*.[$i]).Array)) }; PyList.new(items => @o) });
    reg('map', -> @a, %kw { my $f = @a[0]; PyList.new(items => py-iter(@a[1]).map({ call-value($f, [$_], {}) }).Array) });
    reg('filter', -> @a, %kw { my $f = @a[0]; PyList.new(items => py-iter(@a[1]).grep({ is-none($f) ?? truthy($_) !! truthy(call-value($f, [$_], {})) }).Array) });
    reg('any', -> @a, %kw { for py-iter(@a[0]) -> $x { return True if truthy($x) }; False });
    reg('all', -> @a, %kw { for py-iter(@a[0]) -> $x { return False unless truthy($x) }; True });
    reg('ord', -> @a, %kw { @a[0].ord });
    reg('chr', -> @a, %kw { chr(pnum(@a[0]).Int) });
    reg('type', -> @a, %kw { "<class '{type-name(@a[0])}'>" });
    reg('isinstance', -> @a, %kw { isinstance-check(@a[0], @a[1]) });
    reg('input', -> @a, %kw { print py-str(@a[0]) if @a; my $l = $*IN.get; $l.defined ?? ~$l !! py-raise("EOFError", "EOF") });
    reg('abs', -> @a, %kw { pnum(@a[0]).abs });
    reg('divmod', -> @a, %kw { my $q = py-binop('//', @a[0], @a[1]); my $r = py-binop('%', @a[0], @a[1]); PyTuple.new(items => [$q, $r]) });
}
sub pnum-of($v) { $v ~~ Str ?? +$v !! pnum($v) }
sub to-int($v, %kw) {
    return $v.Int if $v ~~ Int; return $v ?? 1 !! 0 if $v ~~ Bool; return $v.Int if $v ~~ Rat|Num;
    if $v ~~ Str { my $b = %kw<base>:exists ?? pnum(%kw<base>).Int !! 10; return $v.trim.Int if $b == 10; return :16($v) if $b == 16; return :2($v) if $b == 2; return $v.trim.Int }
    py-raise("TypeError", "int() argument");
}
sub py-round(@a) {
    my $x = pnum(@a[0]);
    return round-half-even($x.Num) if @a < 2;
    my $nd = pnum(@a[1]).Int;
    my $f = 10 ** $nd;
    my $r = round-half-even(($x * $f).Num) / $f;
    $nd <= 0 ?? $r.Int !! $r.Num;
}
sub round-half-even(Num $x) {
    my $f = $x.floor; my $diff = $x - $f;
    return $f if $diff < 0.5; return $f + 1 if $diff > 0.5;
    ($f %% 2) ?? $f !! $f + 1;
}
sub py-minmax(@a, %kw, $wantmax) {
    my @items = @a.elems == 1 ?? py-iter(@a[0]).Array !! @a.Array;
    py-raise("ValueError", "arg is an empty sequence") unless @items;
    my &key = %kw<key>:exists ?? -> $x { call-value(%kw<key>, [$x], {}) } !! -> $x { $x };
    my $best = @items[0];
    for @items[1 .. *] -> $x {
        my $c = py-lt(key($best), key($x));
        $best = $x if ($wantmax ?? $c !! !$c && !py-eq(key($best), key($x)));
    }
    $best;
}
sub isinstance-check($v, $cls) {
    my $name = $cls ~~ Str ?? $cls !! type-name($v);
    my %map = int => (Int, Bool), float => (Rat, Num), str => (Str,), list => (PyList,), dict => (PyDict,), tuple => (PyTuple,), bool => (Bool,);
    True;
}

# =========================================================================
# DRIVER
# =========================================================================
sub run-python(Str $src) {
    my $env = Env.new;
    install-builtins($env);
    my $stream = preprocess($src);
    my $m = PyGrammar.parse($stream, actions => Actions.new);
    py-raise("SyntaxError", "invalid syntax") unless $m;
    {
        exec-stmts($m.made<body>, $env);
        CATCH {
            when PyErr { note "{$_.kind}" ~ ($_.msg.defined ?? ": {$_.msg}" !! ''); exit 1 }
            when RetX  { }
        }
    }
}

sub repl {
    my $env = Env.new;
    install-builtins($env);
    say "python.raku — a Python 3 interpreter in Raku (rakupp). Ctrl-D to exit.";
    while (my $line = prompt('>>> ')).defined {
        my $src = $line.trim;
        next if $src eq '';
        {
            my $m = PyGrammar.parse(preprocess($src ~ "\n"), actions => Actions.new);
            die "SyntaxError" unless $m;
            my @body = $m.made<body>;
            # echo the value of a bare expression, like the real REPL
            for @body -> $s {
                my @stmts = $s<t> eq 'seq' ?? @($s<body>) !! [$s];
                for @stmts -> $st {
                    if $st<t> eq 'expr' {
                        my $v = pyeval($st<value>, $env);
                        say py-repr($v) unless is-none($v);
                    }
                    else { exec-stmt($st, $env) }
                }
            }
            CATCH {
                when PyErr { note $_.message }
                when RetX  { }
                default    { note ~$_ }
            }
        }
    }
    say '';
}

sub MAIN($file?, Str :$ast, Str :$tokens) {
    if $tokens.defined {
        print preprocess($tokens.IO.slurp).subst("\x0E", "<IN>\n", :g).subst("\x0F", "<DE>\n", :g);
    }
    elsif $ast.defined {
        my $m = PyGrammar.parse(preprocess($ast.IO.slurp), actions => Actions.new);
        say $m ?? $m.made.raku !! "parse failed";
    }
    elsif $file.defined {
        run-python($file.IO.slurp);
    }
    else {
        repl;
    }
}
