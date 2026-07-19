#!/usr/bin/env raku
# A JavaScript/TypeScript interpreter. A Raku grammar parses a practical slice
# of the language — functions, closures, arrows, classes with inheritance,
# template literals, try/catch, for/for-of — into an AST, and a tree-walking
# evaluator runs it. TypeScript is handled the way tsc handles it: type
# annotations, interfaces, type aliases and generics are parsed and erased;
# enums (the one TS construct with runtime output) become objects. One grammar
# serves both languages, so .js and .ts files run alike.
#
#   build/rakupp showcase/js/js.raku showcase/js/examples/fib.js
#   build/rakupp showcase/js/js.raku showcase/js/examples/bank.ts
#   build/rakupp showcase/js/js.raku --ast=program.js       # dump the AST
#   build/rakupp showcase/js/js.raku                        # REPL
#
# Numbers are IEEE doubles (like JS), `%` truncates toward zero (like JS),
# `+` concatenates when either side is a string, `==` coerces and `===`
# doesn't. Semicolons are required; regex literals, async and modules are out
# of scope. See README.md for the exact subset.

# ---------- values ------------------------------------------------------
# JS null and undefined are distinct singletons, separate from Raku's Nil.
class JSNull  { }
class JSUndef { }
# Sentinel for a short-circuited optional chain (`a?.b.c` when a is nullish):
# it propagates up the member/index/call chain and becomes undefined at the root.
class ShortCircuit { }
my \NULL  = JSNull.new;
my \UNDEF = JSUndef.new;
my \SHORT = ShortCircuit.new;

# JS objects preserve insertion order; a Raku Hash doesn't, so keep a key list.
class JSObject {
    has @.names;
    has %.props;
    has $.cls;                  # JSClass for instances, else Nil
    method has-key($k)  { %!props{$k}:exists }
    method get($k)      { %!props{$k}:exists ?? %!props{$k} !! Nil }
    method set($k, $v)  { @!names.push($k) unless %!props{$k}:exists; %!props{$k} = $v }
    method del($k)      { @!names = @!names.grep(-> $n { $n ne $k }); %!props{$k}:delete }
}

class JSFunc {
    has $.name;
    has @.params;               # list of %( name, default(AST|Nil), prop )
    has $.body;                 # list of statements, or expression AST for arrows
    has $.expr-body;            # True for `x => x + 1`
    has $.env;
    has $.kind;                 # 'fn' | 'arrow' | 'method' | 'ctor'
}

class NativeFn {
    has $.name;
    has $.fn;                   # closure (@args, $this) -> value
}

class JSClass {
    has $.name;
    has $.parent;               # JSClass or Nil
    has $.ctor;                 # JSFunc or Nil
    has %.methods;
    has @.fields;               # list of %( name, init(AST|Nil) ) run at `new`
    has %.statics;
    has $.env;                  # lexical scope of the class declaration
    # Returns [method, defining-class] so `super` can be resolved, or Nil.
    method lookup($n) {
        my $c = self;
        while $c.defined {
            if $c.methods{$n}:exists { my @r = $c.methods{$n}, $c; return @r }
            $c = $c.parent;
        }
        Nil
    }
    method lookup-ctor {
        my $c = self;
        while $c.defined {
            if $c.ctor.defined { my @r = $c.ctor, $c; return @r }
            $c = $c.parent;
        }
        Nil
    }
    method find-static($n) {
        my $c = self;
        while $c.defined {
            return $c.statics{$n} if $c.statics{$n}:exists;
            $c = $c.parent;
        }
        Nil
    }
}

# A method plucked off an object without being called: remember the receiver.
class Bound {
    has $.fn;
    has $.receiver;   # not named `self`: .self is special in Raku
}

# ---------- control-flow exceptions -------------------------------------
class RetX is Exception { has $.value }
class BrkX is Exception { }
class CntX is Exception { }
class ThrX is Exception { has $.value }   # JS `throw`

sub die-js(Str $msg) { ThrX.new(value => $msg).throw }

# ---------- environment -------------------------------------------------
class Env {
    has %.vars;
    has %.consts;
    has $.parent;
    method declare($n, $v, Bool :$const = False) {
        %!vars{$n} = $v;
        %!consts{$n} = True if $const;
    }
    method has-name($n) {
        my $e = self;
        while $e.defined {
            return True if $e.vars{$n}:exists;
            $e = $e.parent;
        }
        False
    }
    method get($n) {
        my $e = self;
        while $e.defined {
            return $e.vars{$n} if $e.vars{$n}:exists;
            $e = $e.parent;
        }
        die-js "ReferenceError: $n is not defined";
    }
    method set-here($n, $v) { %!vars{$n} = $v }
    method set($n, $v) {
        my $e = self;
        while $e.defined {
            if $e.vars{$n}:exists {
                die-js "TypeError: assignment to constant '$n'" if $e.consts{$n}:exists;
                $e.set-here($n, $v);
                return;
            }
            $e = $e.parent;
        }
        die-js "ReferenceError: $n is not defined";
    }
}

# ---------- comment stripping -------------------------------------------
# rakupp does not yet honour a user-defined `ws` token, so comments are blanked
# out (newlines kept, for line numbers) before the grammar ever sees the text.
sub strip-comments(Str $src --> Str) {
    my @c = $src.comb;
    my $out = '';
    my $i = 0;
    my $n = @c.elems;
    while $i < $n {
        my $ch = @c[$i];
        if $ch eq '"' || $ch eq "'" || $ch eq '`' {
            my $q = $ch;
            $out ~= $ch; $i++;
            while $i < $n {
                if @c[$i] eq '\\' && $i + 1 < $n { $out ~= @c[$i] ~ @c[$i + 1]; $i += 2 }
                elsif @c[$i] eq $q { $out ~= $q; $i++; last }
                else { $out ~= @c[$i]; $i++ }
            }
        }
        elsif $ch eq '/' && $i + 1 < $n && @c[$i + 1] eq '/' {
            $i += 2;
            $i++ while $i < $n && @c[$i] ne "\n";
        }
        elsif $ch eq '/' && $i + 1 < $n && @c[$i + 1] eq '*' {
            $i += 2;
            while $i < $n {
                if @c[$i] eq '*' && $i + 1 < $n && @c[$i + 1] eq '/' { $i += 2; last }
                $out ~= "\n" if @c[$i] eq "\n";   # keep line count
                $i++;
            }
        }
        else { $out ~= $ch; $i++ }
    }
    $out;
}

# ---------- automatic semicolon insertion -------------------------------
# Newlines are invisible to the grammar (the builtin `ws` eats them), so ASI
# is done here: a token-boundary scan inserts a real `;` at each newline where
# a JavaScript statement ends, plus before a block-closing `}` and at EOF.
# This is JS's own heuristic — end a statement at a line break unless the break
# is "obviously" a continuation: inside `(`/`[`, inside an object literal, after
# a trailing operator, or before a leading continuation token (`.`, a binary
# operator, `)`…). It is a heuristic, not the full spec (see README), but it
# covers idiomatic semicolon-free code. Runs after strip-comments.

# Keywords that cannot end a statement (a newline right after them continues it).
# return/break/continue are NOT here: a bare one ends the statement (JS's
# "restricted productions"), so a line break after them inserts a `;`.
my $ASI-NONEND = ' new typeof void delete instanceof in of throw case extends default'
               ~ ' if else for while do switch function class try catch finally var let const ';
sub asi-word-ending(Str $w --> Bool) { !$ASI-NONEND.contains(" $w ") }

# Keywords that continue the previous statement, so a break before them never
# inserts (`else`/`catch`/`finally` bind back; `in`/`of`/`instanceof` are infix).
my $ASI-CONT-KW = ' else catch finally instanceof in of ';
sub asi-cont-kw(Str $w --> Bool) { $ASI-CONT-KW.contains(" $w ") }

# First char of the next token that means "continue the previous statement":
# member `.`, closers/openers that bind, and binary operators (incl. + and -,
# which JS treats as continuation). `!`, `~`, `{`, `}` are deliberately absent.
sub asi-cont-char(Str $c --> Bool) { ".,?:)]([*/%<>=&|^+-".contains($c) }

# Is a `{` (or a `function` keyword) in expression position, given the token
# before it? Drives object-literal and function-expression classification.
sub asi-exprpos(Str $t --> Bool) {
    $t eq '(' || $t eq '[' || $t eq ',' || $t eq '#op' || $t eq 'return' || $t eq '=>'
}

# Can $lastTok end a statement? Sentinels (#num/#str/#op) start with '#' and
# ')ctrl' with ')', so neither collides with a real identifier.
sub asi-ending(Str $lastTok --> Bool) {
    return False if $lastTok eq '' || $lastTok eq ';' || $lastTok eq '#op' || $lastTok eq ')ctrl';
    return True  if $lastTok eq '#num' || $lastTok eq '#str'
                 || $lastTok eq ')' || $lastTok eq ']' || $lastTok eq '}'
                 || $lastTok eq '++' || $lastTok eq '--';
    return asi-word-ending($lastTok) if $lastTok ~~ /^ <[A..Za..z_$]> /;
    return False;
}

sub asi-should-insert($lastTok, $nk, $nt, $round, $square, $braceTop --> Bool) {
    return False unless asi-ending($lastTok);
    return False if $round > 0 || $square > 0;      # inside (...) or [...]
    return False if $braceTop eq 'object';          # inside an object literal
    if $nk eq 'word' {
        # infix keyword operators always continue the expression
        return False if $nt eq 'in' || $nt eq 'of' || $nt eq 'instanceof';
        # else/catch/finally bind to a preceding *block*; after an unbraced
        # statement (`if (x) foo()\nelse …`) the statement still needs its `;`
        if $nt eq 'else' || $nt eq 'catch' || $nt eq 'finally' {
            return $lastTok ne '}block';
        }
        return False if $nt eq 'while' && ($lastTok eq '}' || $lastTok eq '}block');   # } while (...)
        return True;
    }
    elsif $nk eq 'eof' {
        return True;
    }
    else {
        return False if $nt eq '{' && $lastTok eq ')';       # Allman-style `f()\n{`
        return False if asi-cont-char($nt);
        return True;
    }
}

# Look at the first significant token at or after index $start: ('word', text),
# ('char', c), or ('eof', '').
sub asi-peek(@c, $start, $n) {
    my $j = $start;
    while $j < $n && (@c[$j] eq ' ' || @c[$j] eq "\t" || @c[$j] eq "\n" || @c[$j] eq "\r") { $j++ }
    return ('eof', '') if $j >= $n;
    my $ch = @c[$j];
    if $ch ~~ /^ <[A..Za..z_$]> / {
        my $w = '';
        while $j < $n && @c[$j] ~~ /^ <[A..Za..z0..9_$]> / { $w ~= @c[$j]; $j++ }
        return ('word', $w);
    }
    ('char', $ch)
}

sub insert-asi(Str $src --> Str) {
    my @c = $src.comb;
    my $n = @c.elems;
    my $out = '';
    my $i = 0;

    my $lastTok = '';       # symbolic previous token; see asi-ending for the alphabet
    my @braces;             # 'block' | 'exprfn' | 'object', innermost last
    my @parens;             # 'ctrl'  | 'expr'   | 'func',   innermost last
    my $round  = 0;
    my $square = 0;
    my @depths;             # saved (round, square) per `{` — bracket depth is brace-local
    my $pendingBrace = '';  # category to give the next `{` (a function body)
    my $funcHeader   = False;   # just saw the `function` keyword
    my $funcExpr     = False;   # …and it was in expression position
    my @doDepths;           # brace-nesting of each pending `do` (to find its `while`)
    my $whileIsDo    = False;   # the `while` just read closes a do-while

    my $OP = "+-*/%=<>!&|^~?:";

    while $i < $n {
        my $ch = @c[$i];

        # --- whitespace: the one place a `;` may be inserted ---
        if $ch eq ' ' || $ch eq "\t" || $ch eq "\n" || $ch eq "\r" {
            my $j = $i;
            my $hasNL = False;
            while $j < $n && (@c[$j] eq ' ' || @c[$j] eq "\t" || @c[$j] eq "\n" || @c[$j] eq "\r") {
                $hasNL = True if @c[$j] eq "\n";
                $j++;
            }
            if $hasNL {
                my ($nk, $nt) = asi-peek(@c, $j, $n);
                my $braceTop = @braces ?? @braces[*-1] !! '';
                if asi-should-insert($lastTok, $nk, $nt, $round, $square, $braceTop) {
                    $out ~= ';';
                    $lastTok = ';';
                }
            }
            $out ~= @c[$i ..^ $j].join;
            $i = $j;
            next;
        }

        # --- string literal ---
        if $ch eq '"' || $ch eq "'" {
            my $q = $ch;
            $out ~= $ch; $i++;
            while $i < $n {
                if @c[$i] eq '\\' && $i + 1 < $n { $out ~= @c[$i] ~ @c[$i + 1]; $i += 2; }
                elsif @c[$i] eq $q { $out ~= $q; $i++; last; }
                else { $out ~= @c[$i]; $i++; }
            }
            $lastTok = '#str';
            next;
        }

        # --- template literal (may span lines; track ${ } depth) ---
        if $ch eq '`' {
            $out ~= $ch; $i++;
            my $depth = 0;
            while $i < $n {
                my $t = @c[$i];
                if $t eq '\\' && $i + 1 < $n { $out ~= $t ~ @c[$i + 1]; $i += 2; }
                elsif $depth == 0 && $t eq '`' { $out ~= $t; $i++; last; }
                elsif $t eq '$' && $i + 1 < $n && @c[$i + 1] eq '{' { $depth++; $out ~= '${'; $i += 2; }
                elsif $depth > 0 && $t eq '{' { $depth++; $out ~= $t; $i++; }
                elsif $depth > 0 && $t eq '}' { $depth--; $out ~= $t; $i++; }
                else { $out ~= $t; $i++; }
            }
            $lastTok = '#str';
            next;
        }

        # --- identifier / keyword / number ---
        if $ch ~~ /^ <[A..Za..z0..9_$]> / {
            my $isNum = ?($ch ~~ /^ \d /);
            my $w = '';
            while $i < $n && @c[$i] ~~ /^ <[A..Za..z0..9_$]> / { $w ~= @c[$i]; $i++; }
            $out ~= $w;
            if !$isNum && $w eq 'function' {
                $funcHeader = True;
                $funcExpr   = asi-exprpos($lastTok);   # expr vs. declaration
            }
            # an `enum` body has comma-separated members, like an object literal —
            # never terminate its members with `;`
            $pendingBrace = 'object' if !$isNum && $w eq 'enum';
            # match `while` to its `do`: a do-while `while (cond)` ends a statement,
            # a loop-starting `while (cond)` does not (a body follows)
            if !$isNum && $w eq 'do' { @doDepths.push(@braces.elems); }
            if !$isNum && $w eq 'while' {
                $whileIsDo = @doDepths && @doDepths[*-1] == @braces.elems
                          && ($lastTok eq '}block' || $lastTok eq '}' || $lastTok eq ';');
                @doDepths.pop if $whileIsDo;
            }
            $lastTok = $isNum ?? '#num' !! $w;
            next;
        }

        # --- brackets and structural punctuation ---
        if $ch eq '(' {
            # a do-while's `while (cond)` takes an ordinary (statement-ending) paren
            my $ctrl = ($lastTok eq 'if' || $lastTok eq 'for'
                        || ($lastTok eq 'while' && !$whileIsDo));
            my $kind = $funcHeader ?? 'func' !! $ctrl ?? 'ctrl' !! 'expr';
            $funcHeader = False;
            $whileIsDo  = False;
            @parens.push($kind);
            $round++;
            $out ~= '('; $i++;
            $lastTok = '(';
            next;
        }
        if $ch eq ')' {
            my $kind = @parens ?? @parens.pop !! 'expr';
            $round-- if $round > 0;
            $out ~= ')'; $i++;
            # a function header's `)` fixes what its body `{` will be
            $pendingBrace = ($funcExpr ?? 'exprfn' !! 'block') if $kind eq 'func';
            $lastTok = $kind eq 'ctrl' ?? ')ctrl' !! ')';
            next;
        }
        if $ch eq '[' { $square++;               $out ~= '['; $i++; $lastTok = '['; next; }
        if $ch eq ']' { $square-- if $square > 0; $out ~= ']'; $i++; $lastTok = ']'; next; }

        if $ch eq '{' {
            my $cat = $pendingBrace ne ''    ?? $pendingBrace     # a function/arrow body
                   !! $lastTok eq '=>'        ?? 'exprfn'          # arrow body
                   !! asi-exprpos($lastTok)   ?? 'object'          # object literal
                   !!                             'block';         # statement block
            $pendingBrace = '';
            @braces.push($cat);
            # inside a `{…}`, the enclosing (…)/[…] depth is irrelevant to where
            # statements end (e.g. an IIFE body lives inside a `(`), so reset it
            @depths.push([$round, $square]);
            $round = 0; $square = 0;
            $out ~= '{'; $i++;
            $lastTok = '{';
            next;
        }
        if $ch eq '}' {
            my $cat = @braces ?? @braces.pop !! 'block';
            if @depths { my $d = @depths.pop; $round = $d[0]; $square = $d[1]; }
            else       { $round = 0; $square = 0; }
            # a block / function body needs its final statement terminated even
            # on one line (`{ return 1 }`); an object literal does not.
            if $cat ne 'object' && asi-ending($lastTok) && $lastTok ne '{' {
                $out ~= ';';
            }
            $out ~= '}'; $i++;
            # object/function-expression `}` can end a statement; a plain block can't
            $lastTok = $cat eq 'block' ?? '}block' !! '}';
            next;
        }

        if $ch eq ';' { $out ~= ';'; $i++; $lastTok = ';'; next; }
        if $ch eq ',' { $out ~= ','; $i++; $lastTok = ','; next; }
        if $ch eq '.' { $out ~= '.'; $i++; $lastTok = '.'; next; }

        # --- operator run (=>, ++, --, ===, &&, …) ---
        if $OP.contains($ch) {
            my $run = '';
            while $i < $n && $OP.contains(@c[$i]) { $run ~= @c[$i]; $i++; }
            $out ~= $run;
            $lastTok = ($run eq '++' || $run eq '--') ?? $run
                    !! $run eq '=>'                   ?? '=>'
                    !!                                   '#op';
            next;
        }

        # --- anything else: copy verbatim ---
        $out ~= $ch; $i++;
        $lastTok = $ch;
    }

    # trailing statement with no closing newline
    $out ~= ';' if asi-ending($lastTok);
    $out;
}

# ---------- grammar -----------------------------------------------------
grammar JSGrammar {
    token kw($k) { $k <!before <[A..Za..z0..9_$]>> }
    token ident  { <!rword> <[A..Za..z_$]> <[A..Za..z0..9_$]>* }
    # a bare `return`/`break`/`continue` must not parse as an identifier
    # expression, or it ties with its own statement rule (ASI emits them bare)
    token rword  { [ 'return' | 'break' | 'continue' ] <!before <[A..Za..z0..9_$]>> }

    rule TOP { <.ws> <statement>* }

    rule block { '{' <statement>* '}' }

    proto rule statement {*}
    rule statement:sym<empty>    { ';' }
    rule statement:sym<decl>     { $<kind>=['let'|'const'|'var']<!before <[A..Za..z0..9_$]>> <declarator>+ % ',' ';' }
    rule statement:sym<func>     { <.kw('function')> <ident> <tparams>? '(' [ <param>* % ',' ] ')' <rtann>? <block> }
    rule statement:sym<class>    { <.kw('class')> <ident> <tparams>? <extendsc>? [ <.kw('implements')> <type>+ % ',' ]? '{' <clsmember>* '}' }
    rule statement:sym<if>       { <.kw('if')> '(' <expr> ')' <statement> <elsec>? }
    rule statement:sym<while>    { <.kw('while')> '(' <expr> ')' <statement> }
    rule statement:sym<dowhile>  { <.kw('do')> <block> <.kw('while')> '(' <expr> ')' ';' }
    rule statement:sym<forof>    { <.kw('for')> '(' $<kind>=['let'|'const'|'var']<!before <[A..Za..z0..9_$]>> [ <ident> | '[' <ident>+ % ',' ']' ] <.kw('of')> <expr> ')' <statement> }
    rule statement:sym<for>      { <.kw('for')> '(' <forinit>? ';' <fcond>? ';' <fupd>? ')' <statement> }
    rule statement:sym<return>   { <.kw('return')> <expr>? ';' }
    rule statement:sym<break>    { <.kw('break')> ';' }
    rule statement:sym<continue> { <.kw('continue')> ';' }
    rule statement:sym<throw>    { <.kw('throw')> <expr> ';' }
    rule statement:sym<switch>   { <.kw('switch')> '(' <expr> ')' '{' <swcase>* '}' }
    proto rule swcase {*}
    rule swcase:sym<case>    { <.kw('case')> <expr> ':' <statement>* }
    rule swcase:sym<default> { <.kw('default')> ':' <statement>* }
    rule statement:sym<try>      { <.kw('try')> <block> <catchc>? <finallyc>? }
    rule extendsc  { <.kw('extends')> <ident> }
    rule elsec     { <.kw('else')> <statement> }
    rule fcond     { <expr> }
    rule fupd      { <expr> }
    rule catchc    { <.kw('catch')> [ '(' <ident> [ ':' <type> ]? ')' ]? <block> }
    rule finallyc  { <.kw('finally')> <block> }
    rule statement:sym<interface> { <.kw('interface')> <ident> <tparams>? [ <.kw('extends')> <type>+ % ',' ]? <tobj> }
    rule statement:sym<typealias> { <.kw('type')> <ident> <tparams>? '=' <type> ';' }
    rule statement:sym<enum>     { <.kw('enum')> <ident> '{' [ <enummember>* % ',' ] ','? '}' }
    rule statement:sym<block>    { <block> }
    rule statement:sym<expr>     { <expr> ';' }

    rule declarator { <ident> [ ':' <type> ]? [ '=' <assign> ]? }
    rule forinit    { $<kind>=['let'|'const'|'var']<!before <[A..Za..z0..9_$]>> <declarator>+ % ',' | <expr> }
    rule enummember { <ident> [ '=' <assign> ]? }

    rule param { $<mods>=[ ['public'|'private'|'protected'|'readonly']<!before <[A..Za..z0..9_$]>> <.ws> ]* <ident> '?'? [ ':' <type> ]? [ '=' <assign> ]? }
    rule rtann { ':' <type> }

    rule clsmember { $<mods>=[ ['public'|'private'|'protected'|'readonly'|'static']<!before <[A..Za..z0..9_$]>> <.ws> ]*
                     [ <ident> '(' [ <param>* % ',' ] ')' <rtann>? <block>
                     | <ident> '?'? [ ':' <type> ]? [ '=' <assign> ]? ';' ] }

    # ----- types: parsed, never evaluated (TypeScript erasure) -----
    rule tparams { '<' [ <ident> [ <.kw('extends')> <type> ]? ]+ % ',' '>' }
    rule type    { <tunit> [ '[' ']' ]* [ [ '|' | '&' ] <type> ]? }
    proto rule tunit {*}
    rule tunit:sym<fn>    { '(' [ <tfnparam>* % ',' ] ')' '=>' <type> }
    rule tunit:sym<paren> { '(' <type> ')' }
    rule tunit:sym<obj>   { <tobj> }
    rule tunit:sym<tuple> { '[' [ <type>* % ',' ] ']' }
    rule tunit:sym<str>   { <str> }
    rule tunit:sym<num>   { <num> }
    rule tunit:sym<name>  { <ident> [ '.' <ident> ]* [ '<' <type>+ % ',' '>' ]? }
    rule tfnparam { <ident> '?'? ':' <type> | <type> }
    rule tobj     { '{' [ <tmember> [ ';' | ',' ]? ]* '}' }
    rule tmember  { <ident> '?'? [ '(' [ <tfnparam>* % ',' ] ')' ]? ':' <type> }

    # ----- expressions: precedence ladder -----
    rule expr    { <assign> }
    rule assign  { <ternary> [ $<aop>=[ '=' <!before <[=>]>> | '+=' | '-=' | '**=' | '*=' | '/=' | '%=' | '&&=' | '||=' | '??=' | '&=' | '|=' | '^=' | '<<=' | '>>>=' | '>>=' ] <assign> ]? }
    rule ternary { <nullish> [ '?' <!before '.'> <assign> ':' <assign> ]? }
    rule nullish { <orx>  [ '??' <orx> ]* }
    rule orx     { <andx> [ '||' <andx> ]* }
    rule andx    { <bitor>  [ '&&' <bitor> ]* }
    rule bitor   { <bitxor> [ $<op>=[ '|' <!before <[|=]>> ] <bitxor> ]* }
    rule bitxor  { <bitand> [ $<op>=[ '^' <!before '='> ] <bitand> ]* }
    rule bitand  { <eqx>    [ $<op>=[ '&' <!before <[&=]>> ] <eqx> ]* }
    rule eqx     { <rel>  [ $<op>=[ '===' | '!==' | '==' | '!=' ] <rel> ]* }
    rule rel     { <shift> [ $<op>=[ '<=' | '>=' | '<' <!before '<'> | '>' <!before '>'> | 'instanceof'<!before <[A..Za..z0..9_$]>> | 'in'<!before <[A..Za..z0..9_$]>> ] <shift> ]* }
    rule shift   { <add>  [ $<op>=[ '<<' <!before '='> | '>>>' <!before '='> | '>>' <!before '='> ] <add> ]* }
    rule add     { <mul>  [ $<op>=[ '+' <!before <[+=]>> | '-' <!before <[-=]>> ] <mul> ]* }
    rule mul     { <unary> [ $<op>=[ '*' <!before <[*=]>> | '/' <!before '='> | '%' <!before '='> ] <unary> ]* }
    rule unary   { $<op>=[ [ '!' <!before '='> | '~' | '-' <!before <[-=]>> | '+' <!before <[+=]>> | 'typeof'<!before <[A..Za..z0..9_$]>> | 'void'<!before <[A..Za..z0..9_$]>> | 'delete'<!before <[A..Za..z0..9_$]>> ] <.ws> ]* <pow> }
    rule pow     { <postfix> [ '**' <pow> ]? }

    rule postfix { <primary> <ptail>* }
    proto rule ptail {*}
    rule ptail:sym<optcall>  { '?.(' [ <assign>* % ',' ] ','? ')' }
    rule ptail:sym<optindex> { '?.[' <expr> ']' }
    rule ptail:sym<optdot>   { '?.' <ident> }
    rule ptail:sym<dot>      { '.' <ident> }
    rule ptail:sym<call>     { '(' [ <assign>* % ',' ] ','? ')' }
    rule ptail:sym<index>    { '[' <expr> ']' }
    rule ptail:sym<as>       { <.kw('as')> <type> }
    rule ptail:sym<targs>    { '<' <type>+ % ',' '>' <?before '('> }   # f<T>(...) — erased
    rule ptail:sym<incdec>   { $<op>=[ '++' | '--' ] }
    rule ptail:sym<bang>     { '!' <!before '='> }

    proto rule primary {*}
    rule primary:sym<arrow>    { [ '(' [ <param>* % ',' ] ')' | <param> ] <rtann>? '=>' [ <block> | <assign> ] }
    rule primary:sym<funcexpr> { <.kw('function')> <ident>? '(' [ <param>* % ',' ] ')' <rtann>? <block> }
    rule primary:sym<new>      { <.kw('new')> <ident> [ '<' <type>+ % ',' '>' ]? [ '(' [ <assign>* % ',' ] ')' ]? }
    rule primary:sym<paren>    { '(' <expr> ')' }
    rule primary:sym<tmpl>     { <tmpl> }
    rule primary:sym<str>      { <str> }
    rule primary:sym<num>      { <num> }
    rule primary:sym<array>    { '[' [ <assign>* % ',' ] ','? ']' }
    rule primary:sym<object>   { '{' [ <objpair>* % ',' ] ','? '}' }
    rule primary:sym<supercall>   { <.kw('super')> '(' [ <assign>* % ',' ] ')' }
    rule primary:sym<supermethod> { <.kw('super')> '.' <ident> }
    rule primary:sym<ident>    { <ident> }

    rule objpair { <ident> '(' [ <param>* % ',' ] ')' <rtann>? <block>
                 | [ $<key>=<ident> | $<keystr>=<str> ] ':' <assign>
                 | <ident> }

    # ----- lexical pieces -----
    token num { [ '0x' <[0..9a..fA..F]>+ | \d+ [ '.' \d+ ]? [ <[eE]> <[+-]>? \d+ ]? ] }
    token str { '\'' $<chars>=[ <-['\\]> | '\\' . ]* '\''
              | '"'  $<chars>=[ <-["\\]> | '\\' . ]* '"' }

    token tmpl { '`' <tpart>* '`' }
    proto token tpart {*}
    token tpart:sym<expr>   { '${' <.ws> <expr> <.ws> '}' }
    token tpart:sym<esc>    { '\\' . }
    token tpart:sym<text>   { <-[`$\\]>+ }
    token tpart:sym<dollar> { '$' <!before '{'> }
}

# ---------- AST construction --------------------------------------------
# rakupp quirk: quantified captures must be pulled through a list assignment
# (`my @x = $<x>;`) before indexing inside an action method.

sub mklist($caps) {
    my @l = $caps // ();
    @l
}

sub fold-bin($first, $ops, $rest) {
    my @o = mklist($ops);
    my @r = mklist($rest);
    my $acc = $first.made;
    for ^@o.elems -> $i {
        $acc = %( t => 'bin', op => (~@o[$i]).trim, l => $acc, r => @r[$i].made );
    }
    $acc
}

sub fold-logic($first, $rest, Str $op) {
    my @r = mklist($rest);
    my $acc = $first.made;
    for ^@r.elems -> $i {
        $acc = %( t => 'logic', op => $op, l => $acc, r => @r[$i].made );
    }
    $acc
}

sub made-list($caps) {
    my @l = mklist($caps);
    my @out;
    for @l -> $c { @out.push($c.made) }
    @out
}

class JSActions {
    method TOP($/)  { make %( t => 'prog', stmts => made-list($<statement>) ) }
    method block($/) { make %( t => 'block', stmts => made-list($<statement>) ) }

    method statement:sym<empty>($/)  { make %( t => 'empty' ) }
    method statement:sym<decl>($/)   { make %( t => 'decl', kind => ~$<kind>, decls => made-list($<declarator>) ) }
    method declarator($/) {
        make %( name => ~$<ident>, init => ($<assign> ?? $<assign>.made !! Nil) )
    }
    method statement:sym<func>($/) {
        make %( t => 'func', name => ~$<ident>, params => made-list($<param>), body => $<block>.made<stmts> )
    }
    method statement:sym<class>($/) {
        make %( t => 'class', name => ~$<ident>, sup => ($<extendsc> ?? $<extendsc>.made !! Nil),
                members => made-list($<clsmember>) )
    }
    method extendsc($/) { make ~$<ident> }
    method elsec($/)    { make $<statement>.made }
    method fcond($/)    { make $<expr>.made }
    method fupd($/)     { make $<expr>.made }
    method catchc($/)   { make %( cvar => ($<ident> ?? ~$<ident> !! Nil), body => $<block>.made ) }
    method finallyc($/) { make $<block>.made }
    method clsmember($/) {
        my $mods = ~($<mods> // '');
        my $static = $mods.contains('static');
        my @props = mklist($<param>).map(-> $p { $p.made }).grep(-> $p { $p<prop> });
        if $<block> {
            make %( t => 'method', name => ~$<ident>, static => $static,
                    params => made-list($<param>), body => $<block>.made<stmts> )
        }
        else {
            make %( t => 'field', name => ~$<ident>, static => $static,
                    init => ($<assign> ?? $<assign>.made !! Nil) )
        }
    }
    method param($/) {
        my $mods = ~($<mods> // '');
        make %( name => ~$<ident>, init => ($<assign> ?? $<assign>.made !! Nil),
                prop => ($mods.trim ne '') )
    }
    method statement:sym<if>($/) {
        make %( t => 'if', cond => $<expr>.made, then => $<statement>.made,
                else => ($<elsec> ?? $<elsec>.made !! Nil) )
    }
    method statement:sym<while>($/)   { make %( t => 'while', cond => $<expr>.made, body => $<statement>.made ) }
    method statement:sym<dowhile>($/) { make %( t => 'dowhile', cond => $<expr>.made, body => $<block>.made ) }
    method statement:sym<forof>($/) {
        my @vars = mklist($<ident>).map(-> $x { ~$x });
        make %( t => 'forof', vars => @vars, kind => ~$<kind>, iter => $<expr>.made, body => $<statement>.made )
    }
    method statement:sym<for>($/) {
        make %( t => 'for',
                init => ($<forinit> ?? $<forinit>.made !! Nil),
                cond => ($<fcond> ?? $<fcond>.made !! Nil),
                upd  => ($<fupd>  ?? $<fupd>.made  !! Nil),
                body => $<statement>.made )
    }
    method forinit($/) {
        if $<declarator> {
            make %( t => 'decl', kind => ~$<kind>, decls => made-list($<declarator>) )
        }
        else {
            make %( t => 'exprstmt', expr => $<expr>.made )
        }
    }
    method statement:sym<return>($/)   { make %( t => 'return', expr => ($<expr> ?? $<expr>.made !! Nil) ) }
    method statement:sym<break>($/)    { make %( t => 'break' ) }
    method statement:sym<continue>($/) { make %( t => 'continue' ) }
    method statement:sym<throw>($/)    { make %( t => 'throw', expr => $<expr>.made ) }
    method statement:sym<switch>($/) {
        make %( t => 'switch', disc => $<expr>.made, cases => made-list($<swcase>) )
    }
    method swcase:sym<case>($/)    { make %( kind => 'case', test => $<expr>.made, body => made-list($<statement>) ) }
    method swcase:sym<default>($/) { make %( kind => 'default', body => made-list($<statement>) ) }
    method statement:sym<try>($/) {
        my $c = $<catchc> ?? $<catchc>.made !! Nil;
        make %( t => 'try', body => $<block>.made,
                cvar => ($c.defined ?? $c<cvar> !! Nil),
                cbody => ($c.defined ?? $c<body> !! Nil),
                fbody => ($<finallyc> ?? $<finallyc>.made !! Nil) )
    }
    method statement:sym<interface>($/) { make %( t => 'empty' ) }
    method statement:sym<typealias>($/) { make %( t => 'empty' ) }
    method statement:sym<enum>($/) {
        make %( t => 'enum', name => ~$<ident>, members => made-list($<enummember>) )
    }
    method enummember($/) { make %( name => ~$<ident>, init => ($<assign> ?? $<assign>.made !! Nil) ) }
    method statement:sym<block>($/) { make $<block>.made }
    method statement:sym<expr>($/)  { make %( t => 'exprstmt', expr => $<expr>.made ) }

    # ----- expressions -----
    method expr($/) { make $<assign>.made }
    method assign($/) {
        if $<assign> {
            make %( t => 'assign', op => (~$<aop>).trim, target => $<ternary>.made, value => $<assign>.made )
        }
        else { make $<ternary>.made }
    }
    method ternary($/) {
        my @a = mklist($<assign>);
        if @a.elems == 2 {
            make %( t => 'cond', cond => $<nullish>.made, then => @a[0].made, else => @a[1].made )
        }
        else { make $<nullish>.made }
    }
    method nullish($/) { make fold-logic($<orx>[0],  $<orx>[1..*],  '??') }
    method orx($/)     { make fold-logic($<andx>[0], $<andx>[1..*], '||') }
    method andx($/)    { make fold-logic($<bitor>[0], $<bitor>[1..*], '&&') }
    method bitor($/)   { make fold-bin($<bitxor>[0], $<op>, $<bitxor>[1..*]) }
    method bitxor($/)  { make fold-bin($<bitand>[0], $<op>, $<bitand>[1..*]) }
    method bitand($/)  { make fold-bin($<eqx>[0],   $<op>, $<eqx>[1..*]) }
    method eqx($/)     { make fold-bin($<rel>[0],   $<op>, $<rel>[1..*]) }
    method rel($/)     { make fold-bin($<shift>[0], $<op>, $<shift>[1..*]) }
    method shift($/)   { make fold-bin($<add>[0],   $<op>, $<add>[1..*]) }
    method add($/)     { make fold-bin($<mul>[0],   $<op>, $<mul>[1..*]) }
    method mul($/)     { make fold-bin($<unary>[0], $<op>, $<unary>[1..*]) }
    method unary($/) {
        # a zero-match $<op>=[...]* leaves one phantom empty capture: drop it
        my @ops = mklist($<op>).map(-> $o { (~$o).trim }).grep(-> $o { $o ne '' });
        my $acc = $<pow>.made;
        for @ops.reverse -> $o {
            $acc = %( t => 'unary', op => $o, e => $acc );
        }
        make $acc;
    }
    method pow($/) {
        my @p = mklist($<pow>);
        if @p.elems {
            make %( t => 'bin', op => '**', l => $<postfix>.made, r => @p[0].made )
        }
        else { make $<postfix>.made }
    }
    method postfix($/) {
        my @tails = mklist($<ptail>);
        my $acc = $<primary>.made;
        my $hasOpt = False;
        for @tails -> $tl {
            my $t = $tl.made;
            $hasOpt = True if $t<opt>;
            $acc = apply-tail($acc, $t);
        }
        # wrap a chain containing `?.` so a nullish short-circuit resolves to undefined
        $acc = %( t => 'optchain', e => $acc ) if $hasOpt;
        make $acc;
    }
    method ptail:sym<dot>($/)      { make %( t => 'dot', name => ~$<ident> ) }
    method ptail:sym<call>($/)     { make %( t => 'callargs', args => made-list($<assign>) ) }
    method ptail:sym<index>($/)    { make %( t => 'idx', e => $<expr>.made ) }
    method ptail:sym<optdot>($/)   { make %( t => 'dot', name => ~$<ident>, opt => True ) }
    method ptail:sym<optcall>($/)  { make %( t => 'callargs', args => made-list($<assign>), opt => True ) }
    method ptail:sym<optindex>($/) { make %( t => 'idx', e => $<expr>.made, opt => True ) }
    method ptail:sym<as>($/)       { make %( t => 'as' ) }
    method ptail:sym<targs>($/)    { make %( t => 'as' ) }
    method ptail:sym<incdec>($/)   { make %( t => 'incdec', op => (~$<op>).trim ) }
    method ptail:sym<bang>($/)     { make %( t => 'as' ) }   # non-null assertion: erased

    method primary:sym<arrow>($/) {
        my $body = $<block> ?? $<block>.made<stmts> !! $<assign>.made;
        make %( t => 'arrowfn', params => made-list($<param>), body => $body,
                expr-body => !$<block> )
    }
    method primary:sym<funcexpr>($/) {
        make %( t => 'funcexpr', name => ($<ident> ?? ~$<ident> !! Nil),
                params => made-list($<param>), body => $<block>.made<stmts> )
    }
    method primary:sym<new>($/) {
        make %( t => 'new', cls => ~$<ident>, args => made-list($<assign>) )
    }
    method primary:sym<paren>($/)  { make $<expr>.made }
    method primary:sym<tmpl>($/)   { make $<tmpl>.made }
    method primary:sym<str>($/)    { make $<str>.made }
    method primary:sym<num>($/)    { make $<num>.made }
    method primary:sym<array>($/)  { make %( t => 'array', items => made-list($<assign>) ) }
    method primary:sym<object>($/) { make %( t => 'object', pairs => made-list($<objpair>) ) }
    method primary:sym<supercall>($/)   { make %( t => 'supercall', args => made-list($<assign>) ) }
    method primary:sym<supermethod>($/) { make %( t => 'supermethod', name => ~$<ident> ) }
    # rakupp proto dispatch is longest-match, and `null` ties with a bare
    # identifier — so value keywords are classified here, not in the grammar.
    method primary:sym<ident>($/)  {
        my $n = ~$<ident>;
        make $n eq 'null'      ?? %( t => 'null' )
          !! $n eq 'undefined' ?? %( t => 'undef' )
          !! $n eq 'true'      ?? %( t => 'bool', v => True )
          !! $n eq 'false'     ?? %( t => 'bool', v => False )
          !! $n eq 'this'      ?? %( t => 'this' )
          !! $n eq 'NaN'       ?? %( t => 'num', v => NaN )
          !! $n eq 'Infinity'  ?? %( t => 'num', v => Inf )
          !!                      %( t => 'ident', name => $n );
    }

    method objpair($/) {
        if $<block> {
            make %( kind => 'method', key => ~$<ident>,
                    fn => %( t => 'funcexpr', name => ~$<ident>,
                             params => made-list($<param>), body => $<block>.made<stmts> ) )
        }
        elsif $<assign> {
            my $k = $<key> ?? ~$<key> !! $<keystr>.made<v>;
            make %( kind => 'pair', key => $k, value => $<assign>.made )
        }
        else {
            make %( kind => 'shorthand', key => ~$<ident> )
        }
    }

    method num($/) {
        my $s = ~$/;
        my $v = $s.starts-with('0x') ?? (:16($s.substr(2))).Num !! $s.Num;
        make %( t => 'num', v => $v );
    }
    method str($/) { make %( t => 'str', v => decode-escapes(~$<chars>) ) }

    method tmpl($/) { make %( t => 'tmpl', parts => made-list($<tpart>) ) }
    method tpart:sym<expr>($/)   { make %( kind => 'expr', e => $<expr>.made ) }
    method tpart:sym<esc>($/)    { make %( kind => 'text', s => decode-escapes(~$/) ) }
    method tpart:sym<text>($/)   { make %( kind => 'text', s => ~$/ ) }
    method tpart:sym<dollar>($/) { make %( kind => 'text', s => '$' ) }
}

# Fold one postfix tail onto an expression node.
sub apply-tail($base, $tail) {
    given $tail<t> {
        when 'dot'      { %( t => 'member', obj => $base, name => $tail<name>, opt => ?$tail<opt> ) }
        when 'callargs' { %( t => 'call', callee => $base, args => $tail<args>, opt => ?$tail<opt> ) }
        when 'idx'      { %( t => 'index', obj => $base, e => $tail<e>, opt => ?$tail<opt> ) }
        when 'incdec'   { %( t => 'postop', op => $tail<op>, target => $base ) }
        default         { $base }   # 'as' casts and non-null assertions: erased
    }
}

sub decode-escapes(Str $s --> Str) {
    my @c = $s.comb;
    my $out = '';
    my $i = 0;
    while $i < @c.elems {
        if @c[$i] eq '\\' && $i + 1 < @c.elems {
            given @c[$i + 1] {
                when 'n'  { $out ~= "\n"; $i += 2 }
                when 't'  { $out ~= "\t"; $i += 2 }
                when 'r'  { $out ~= "\r"; $i += 2 }
                when '0'  { $out ~= "\0"; $i += 2 }
                when 'u'  {
                    if @c[$i + 2] eq '{' {
                        my $j = $i + 3;
                        my $hex = '';
                        while @c[$j] ne '}' { $hex ~= @c[$j]; $j++ }
                        $out ~= chr(:16($hex));
                        $i = $j + 1;
                    }
                    else {
                        $out ~= chr(:16(@c[$i + 2 .. $i + 5].join));
                        $i += 6;
                    }
                }
                default   { $out ~= @c[$i + 1]; $i += 2 }
            }
        }
        else { $out ~= @c[$i]; $i++ }
    }
    $out;
}

# ---------- value helpers -----------------------------------------------
sub is-undef($v) { $v ~~ JSUndef }
sub is-null($v)  { $v ~~ JSNull }

sub truthy($v --> Bool) {
    given $v {
        when Bool     { $v }
        when Num      { !($v == 0e0 || $v.isNaN) }
        when Str      { $v ne '' }
        when JSNull   { False }
        when JSUndef  { False }
        default       { True }
    }
}

sub to-num($v --> Num) {
    given $v {
        when Num     { $v }
        when Bool    { $v ?? 1e0 !! 0e0 }
        when Str     {
            my $s = $v.trim;
            return 0e0 if $s eq '';
            my $n = Nil;
            { $n = $s.Num; CATCH { default { $n = Nil } } }
            $n // NaN
        }
        when JSNull  { 0e0 }
        when JSUndef { NaN }
        default      { NaN }
    }
}

sub js-num-str(Num $n --> Str) {
    return 'NaN'       if $n.isNaN;
    return 'Infinity'  if $n == Inf;
    return '-Infinity' if $n == -Inf;
    if $n == $n.Int && $n.abs < 1e15 {
        my $i = $n.Int;
        return ($i == 0 && 1e0 / $n < 0) ?? '0' !! ~$i;   # -0 prints as 0
    }
    ~$n
}

sub to-str($v --> Str) {
    given $v {
        when Str      { $v }
        when Num      { js-num-str($v) }
        when Bool     { $v ?? 'true' !! 'false' }
        when JSNull   { 'null' }
        when JSUndef  { 'undefined' }
        when Positional { $v.map(-> $x { is-undef($x) || is-null($x) ?? '' !! to-str($x) }).join(',') }
        when JSObject { '[object Object]' }
        when JSFunc   { 'function ' ~ ($v.name // '') ~ '() { ... }' }
        when NativeFn { 'function ' ~ $v.name ~ '() { [native code] }' }
        when JSClass  { 'class ' ~ $v.name }
        default       { ~$v }
    }
}

sub js-typeof($v --> Str) {
    given $v {
        when Num      { 'number' }
        when Str      { 'string' }
        when Bool     { 'boolean' }
        when JSUndef  { 'undefined' }
        when JSFunc   { 'function' }
        when NativeFn { 'function' }
        when Bound    { 'function' }
        when JSClass  { 'function' }
        default       { 'object' }
    }
}

# console.log / REPL display (node-flavoured)
sub display($v, Bool :$nested = False --> Str) {
    given $v {
        when Str { $nested ?? "'" ~ $v ~ "'" !! $v }
        when Positional {
            '[ ' ~ $v.map(-> $x { display($x // UNDEF, :nested) }).join(', ') ~ ' ]'
        }
        when JSObject {
            my @parts;
            for $v.names -> $k {
                @parts.push($k ~ ': ' ~ display($v.get($k), :nested));
            }
            ($v.cls.defined ?? $v.cls.name ~ ' ' !! '') ~ ('{ ' ~ @parts.join(', ') ~ ' }')
        }
        when JSFunc   { '[Function: ' ~ ($v.name // 'anonymous') ~ ']' }
        when NativeFn { '[Function: ' ~ $v.name ~ ']' }
        when Bound    { '[Function (bound)]' }
        when JSClass  { '[class ' ~ $v.name ~ ']' }
        default       { to-str($v) }
    }
}

# JSON.stringify
sub json-stringify($v, Int :$indent = 0, Int :$level = 0) {
    my $nl  = $indent ?? "\n" !! '';
    my $pad = $indent ?? (' ' x ($indent * ($level + 1))) !! '';
    my $end = $indent ?? (' ' x ($indent * $level)) !! '';
    my $sep = $indent ?? ': ' !! ':';
    given $v {
        when Str  { esc-json($v) }
        when Num  { $v.isNaN || $v == Inf || $v == -Inf ?? 'null' !! js-num-str($v) }
        when Bool { $v ?? 'true' !! 'false' }
        when JSNull { 'null' }
        when Positional {
            return '[]' unless $v.elems;
            my @items = $v.map(-> $x {
                my $j = json-stringify($x // UNDEF, :$indent, :level($level + 1));
                $pad ~ ($j // 'null')
            });
            '[' ~ $nl ~ @items.join(',' ~ $nl) ~ $nl ~ $end ~ ']'
        }
        when JSObject {
            my @items;
            for $v.names -> $k {
                my $j = json-stringify($v.get($k), :$indent, :level($level + 1));
                @items.push($pad ~ esc-json($k) ~ $sep ~ $j) if $j.defined;
            }
            return '{}' unless @items.elems;
            '{' ~ $nl ~ @items.join(',' ~ $nl) ~ $nl ~ $end ~ '}'
        }
        default { Nil }   # undefined / functions are dropped, like real JSON.stringify
    }
}

sub esc-json(Str $s --> Str) {
    my $out = $s.subst('\\', '\\\\', :g).subst('"', '\\"', :g)
                .subst("\n", '\\n', :g).subst("\t", '\\t', :g).subst("\r", '\\r', :g);
    '"' ~ $out ~ '"'
}

# ---------- operators ---------------------------------------------------
sub js-add($a, $b) {
    if $a ~~ Str || $b ~~ Str { to-str($a) ~ to-str($b) }
    else { to-num($a) + to-num($b) }
}

sub js-strict-eq($a, $b --> Bool) {
    given $a {
        when Num  { $b ~~ Num  && !$a.isNaN && !$b.isNaN && $a == $b }
        when Str  { $b ~~ Str  && $a eq $b }
        when Bool { $b ~~ Bool && $a == $b }
        when JSNull  { $b ~~ JSNull }
        when JSUndef { $b ~~ JSUndef }
        default   { $b ~~ ($a.WHAT) && $a.WHERE == $b.WHERE }   # reference identity
    }
}

sub js-loose-eq($a, $b --> Bool) {
    return True if (is-null($a) || is-undef($a)) && (is-null($b) || is-undef($b));
    return False if is-null($a) || is-undef($a) || is-null($b) || is-undef($b);
    if $a.WHAT === $b.WHAT { return js-strict-eq($a, $b) }
    if $a ~~ Bool { return js-loose-eq(to-num($a), $b) }
    if $b ~~ Bool { return js-loose-eq($a, to-num($b)) }
    if $a ~~ Num && $b ~~ Str { my $n = to-num($b); return !$a.isNaN && !$n.isNaN && $a == $n }
    if $a ~~ Str && $b ~~ Num { my $n = to-num($a); return !$n.isNaN && !$b.isNaN && $n == $b }
    js-strict-eq($a, $b)
}

sub js-compare($a, $b, Str $op --> Bool) {
    if $a ~~ Str && $b ~~ Str {
        my $c = $a leg $b;
        given $op {
            when '<'  { $c == Less }
            when '>'  { $c == More }
            when '<=' { $c != More }
            default   { $c != Less }
        }
    }
    else {
        my $x = to-num($a);
        my $y = to-num($b);
        return False if $x.isNaN || $y.isNaN;
        given $op {
            when '<'  { $x <  $y }
            when '>'  { $x >  $y }
            when '<=' { $x <= $y }
            default   { $x >= $y }
        }
    }
}

sub js-mod(Num $a, Num $b --> Num) {
    return NaN if $b == 0e0 || $a.isNaN || $b.isNaN || $a == Inf || $a == -Inf;
    return $a if $b == Inf || $b == -Inf;
    $a - $b * ($a / $b).truncate
}

# JS ToInt32 / ToUint32: a number truncated into the 32-bit ring, signed or not.
# accept a JS value or a raw Raku number (bitwise results feed back in as Int)
sub js-num-of($v --> Num) { $v ~~ Numeric ?? $v.Num !! to-num($v) }
sub to-int32($v --> Int) {
    my $n = js-num-of($v);
    return 0 if $n.isNaN || $n == Inf || $n == -Inf;
    my $i = $n.truncate % 4294967296;                 # Raku % yields 0 .. 2³²-1
    $i >= 2147483648 ?? $i - 4294967296 !! $i
}
sub to-uint32($v --> Int) {
    my $n = js-num-of($v);
    return 0 if $n.isNaN || $n == Inf || $n == -Inf;
    $n.truncate % 4294967296
}

sub js-binop(Str $op, $a, $b) {
    given $op {
        when '+'   { js-add($a, $b) }
        when '-'   { to-num($a) - to-num($b) }
        when '<'   { js-compare($a, $b, '<') }
        when '>'   { js-compare($a, $b, '>') }
        when '===' { js-strict-eq($a, $b) }
        when '*'   { to-num($a) * to-num($b) }
        when '/'   {
            my $x = to-num($a); my $y = to-num($b);
            $y == 0e0 ?? ($x == 0e0 || $x.isNaN ?? NaN !! ($x > 0e0 ?? Inf !! -Inf)) !! $x / $y
        }
        when '%'   { js-mod(to-num($a), to-num($b)) }
        when '**'  { to-num($a) ** to-num($b) }
        when '!==' { !js-strict-eq($a, $b) }
        when '=='  { js-loose-eq($a, $b) }
        when '!='  { !js-loose-eq($a, $b) }
        when '<='  { js-compare($a, $b, '<=') }
        when '>='  { js-compare($a, $b, '>=') }
        # bitwise: operands coerce to 32-bit integers; results are JS numbers
        when '&'   { (to-int32($a) +& to-int32($b)).Num }
        when '|'   { (to-int32($a) +| to-int32($b)).Num }
        when '^'   { (to-int32($a) +^ to-int32($b)).Num }
        when '<<'  { to-int32(to-int32($a) +< (to-uint32($b) % 32)).Num }
        when '>>'  { (to-int32($a) +> (to-uint32($b) % 32)).Num }
        when '>>>' { (to-uint32($a) +> (to-uint32($b) % 32)).Num }
        when 'instanceof' { js-instanceof($a, $b) }
        when 'in'         { js-in($a, $b) }
        default    { die-js "unsupported operator $op" }
    }
}

sub js-instanceof($obj, $ctor) {
    # tagged built-in constructors (Object, Array) — no real prototype chain
    if $ctor ~~ JSObject && $ctor.has-key('%instanceof%') {
        given $ctor.get('%instanceof%') {
            when 'Array'  { return $obj ~~ Positional }
            when 'Object' { return $obj ~~ JSObject || $obj ~~ Positional || $obj ~~ JSFunc || $obj ~~ NativeFn }
        }
    }
    return False unless $obj ~~ JSObject && $obj.cls.defined;
    my $target = $ctor ~~ JSClass ?? $ctor !! Nil;
    return False unless $target.defined;
    my $c = $obj.cls;
    while $c.defined { return True if $c === $target; $c = $c.parent }
    False
}

sub js-in($key, $obj) {
    given $obj {
        when JSObject   { $obj.has-key(to-str($key)) || ($obj.cls.defined && $obj.cls.lookup(to-str($key)).defined) }
        when Positional { my $i = to-num($key).Int; 0 <= $i < $obj.elems }
        default { die-js "TypeError: cannot use 'in' operator on " ~ js-typeof($obj) }
    }
}

# ---------- calling -----------------------------------------------------
sub call-value($f, @args, $this = UNDEF) {
    given $f {
        when JSFunc   { call-jsfunc($f, @args, $this) }
        when NativeFn { my $fn = $f.fn; $fn(@args, $this) }
        when Bound    { call-value($f.fn, @args, $f.receiver) }
        when JSObject {
            if $f.has-key('%call%') { call-value($f.get('%call%'), @args, $this) }
            else { die-js "TypeError: " ~ display($f) ~ " is not a function" }
        }
        default       { die-js "TypeError: " ~ display($f) ~ " is not a function" }
    }
}

sub call-jsfunc(JSFunc $f, @args, $this) {
    my $env = Env.new(parent => $f.env);
    if $f.kind ne 'arrow' {
        $env.declare('this', $this);
    }
    for ^$f.params.elems -> $i {
        my %p = $f.params[$i];
        my $v = $i < @args.elems ?? @args[$i] !! UNDEF;
        if is-undef($v) && %p<init>.defined {
            $v = eval-expr(%p<init>, $env);
        }
        $env.declare(%p<name>, $v);
        # TS parameter property: constructor(public x: number) assigns this.x
        if %p<prop> && $f.kind eq 'ctor' && $this ~~ JSObject {
            $this.set(%p<name>, $v);
        }
    }
    my $ret = UNDEF;
    if $f.expr-body {
        $ret = eval-expr($f.body, $env);
    }
    elsif simple-tail-return($f.body) {
        # straight-line body ending in `return`: no exception needed
        my @stmts = $f.body.list;
        for ^(@stmts.elems - 1) -> $i { eval-stmt(@stmts[$i], $env) }
        my $last = @stmts[@stmts.elems - 1];
        $ret = $last<expr>.defined ?? eval-expr($last<expr>, $env) !! UNDEF;
    }
    else {
        {
            eval-stmts($f.body, $env);
            CATCH {
                when RetX { $ret = .value }
            }
        }
    }
    $ret
}

# True when every statement is straight-line (no branches or loops that could
# hide a `return`) and the last one is the only `return`. Nested function
# bodies don't count: their returns are their own.
sub simple-tail-return($body --> Bool) {
    my @stmts = $body.list;
    return False unless @stmts.elems;
    return False unless @stmts[@stmts.elems - 1]<t> eq 'return';
    for ^(@stmts.elems - 1) -> $i {
        my $t = @stmts[$i]<t>;
        return False unless $t eq 'decl' || $t eq 'exprstmt' || $t eq 'empty';
    }
    True
}

sub instantiate(JSClass $cls, @args) {
    my $obj = JSObject.new(cls => $cls);
    # field initializers run base-class first, with `this` in scope
    my @chain;
    my $c = $cls;
    while $c.defined { @chain.unshift($c); $c = $c.parent }
    for @chain -> $k {
        for $k.fields.list -> %f {
            my $fenv = Env.new(parent => $k.env);
            $fenv.declare('this', $obj);
            $obj.set(%f<name>, %f<init>.defined ?? eval-expr(%f<init>, $fenv) !! UNDEF);
        }
    }
    my $r = $cls.lookup-ctor;
    call-value(with-class($r[0], $r[1]), @args, $obj) if $r.defined;
    $obj
}

# Wrap a method so `super` and `%class%` resolve against its defining class.
sub with-class(JSFunc $m, JSClass $cls) {
    my $wrap = Env.new(parent => $m.env);
    $wrap.declare('%class%', $cls);
    JSFunc.new(name => $m.name, params => $m.params.list, body => $m.body,
               expr-body => $m.expr-body, env => $wrap, kind => $m.kind)
}

# ---------- property access ---------------------------------------------
sub get-prop($obj, Str $name) {
    given $obj {
        when Str      { str-prop($obj, $name) }
        when Positional { arr-prop($obj, $name) }
        when JSObject {
            if $obj.has-key($name) { return $obj.get($name) }
            if $obj.cls.defined {
                my $r = $obj.cls.lookup($name);
                return Bound.new(fn => with-class($r[0], $r[1]), receiver => $obj) if $r.defined;
            }
            UNDEF
        }
        when JSClass  {
            my $s = $obj.find-static($name);
            $s.defined ?? $s !! ($name eq 'name' ?? $obj.name !! UNDEF)
        }
        when Num      { ($name eq 'toFixed' || $name eq 'toString') ?? num-method($obj, $name) !! UNDEF }
        when JSUndef  { die-js "TypeError: cannot read properties of undefined (reading '$name')" }
        when JSNull   { die-js "TypeError: cannot read properties of null (reading '$name')" }
        default       { UNDEF }
    }
}

sub set-prop($obj, Str $name, $v) {
    given $obj {
        when JSObject   { $obj.set($name, $v) }
        when Positional {
            if $name eq 'length' {
                my $len = to-num($v).Int;
                if $len < $obj.elems { $obj.pop while $obj.elems > $len }
                else { $obj.push(UNDEF) while $obj.elems < $len }
            }
            else { die-js "TypeError: cannot set property $name on array" }
        }
        when JSUndef { die-js "TypeError: cannot set properties of undefined" }
        when JSNull  { die-js "TypeError: cannot set properties of null" }
        default      { die-js "TypeError: cannot set property $name on " ~ js-typeof($obj) }
    }
}

sub native(Str $name, $fn) { NativeFn.new(name => $name, fn => $fn) }

sub num-method(Num $n, Str $name) {
    given $name {
        when 'toFixed' {
            native('toFixed', -> @a, $ {
                my $d = @a.elems ?? to-num(@a[0]).Int !! 0;
                sprintf('%.' ~ $d ~ 'f', $n)
            })
        }
        when 'toString' {
            native('toString', -> @a, $ {
                my $radix = @a.elems && !is-undef(@a[0]) ?? to-num(@a[0]).Int !! 10;
                $radix == 10 ?? js-num-str($n) !! int-to-base($n, $radix)
            })
        }
    }
}

# Integer part of a number rendered in an arbitrary base 2..36 (like JS toString(radix)).
sub int-to-base(Num $n, Int $radix --> Str) {
    return js-num-str($n) if $n.isNaN || $n == Inf || $n == -Inf;
    my $neg = $n < 0e0;
    my $i = $n.abs.truncate;
    return '0' if $i == 0;
    my $digits = '0123456789abcdefghijklmnopqrstuvwxyz';
    my $out = '';
    while $i > 0 { $out = $digits.substr($i % $radix, 1) ~ $out; $i = ($i / $radix).Int; }
    ($neg ?? '-' !! '') ~ $out
}

# One callback-arg helper: JS callbacks receive (elem, index) etc.
sub cb($f, *@args) { call-value($f, @args, UNDEF) }

sub str-prop(Str $s, Str $name) {
    given $name {
        when 'length'      { $s.chars.Num }
        when 'toUpperCase' { native($name, -> @a, $ { $s.uc }) }
        when 'toLowerCase' { native($name, -> @a, $ { $s.lc }) }
        when 'trim'        { native($name, -> @a, $ { $s.trim }) }
        when 'charAt'      { native($name, -> @a, $ { my $i = to-num(@a[0] // 0e0).Int; 0 <= $i < $s.chars ?? $s.substr($i, 1) !! '' }) }
        when 'charCodeAt'  { native($name, -> @a, $ { my $i = to-num(@a[0] // 0e0).Int; 0 <= $i < $s.chars ?? $s.substr($i, 1).ord.Num !! NaN }) }
        when 'indexOf'     { native($name, -> @a, $ { my $i = $s.index(to-str(@a[0] // UNDEF)); ($i // -1).Num }) }
        when 'includes'    { native($name, -> @a, $ { $s.contains(to-str(@a[0] // UNDEF)) }) }
        when 'startsWith'  { native($name, -> @a, $ { $s.starts-with(to-str(@a[0] // UNDEF)) }) }
        when 'endsWith'    { native($name, -> @a, $ { $s.ends-with(to-str(@a[0] // UNDEF)) }) }
        when 'slice'       { native($name, -> @a, $ { slice-str($s, @a) }) }
        when 'substring'   { native($name, -> @a, $ {
            my $b = @a.elems > 0 ?? to-num(@a[0]).Int !! 0;
            my $e = @a.elems > 1 ?? to-num(@a[1]).Int !! $s.chars;
            $b = 0 if $b < 0; $e = 0 if $e < 0;
            $b = $s.chars if $b > $s.chars; $e = $s.chars if $e > $s.chars;
            ($b, $e) = ($e, $b) if $b > $e;
            $s.substr($b, $e - $b)
        }) }
        when 'split'       { native($name, -> @a, $ {
            # explicit returns: --exe loses the value of a block-final if/else
            my $sep = @a.elems ?? to-str(@a[0]) !! Nil;
            if !$sep.defined { my @r = $s; return @r }
            if $sep eq ''    { my @r = $s.comb; return @r }
            my @r = $s.split($sep);
            @r
        }) }
        when 'repeat'      { native($name, -> @a, $ { $s x to-num(@a[0] // 0e0).Int }) }
        when 'replace'     { native($name, -> @a, $ {
            my $from = to-str(@a[0] // UNDEF);
            my $to   = to-str(@a[1] // UNDEF);
            my $i = $s.index($from);
            $i.defined ?? $s.substr(0, $i) ~ $to ~ $s.substr($i + $from.chars) !! $s
        }) }
        when 'replaceAll'  { native($name, -> @a, $ { $s.subst(to-str(@a[0] // UNDEF), to-str(@a[1] // UNDEF), :g) }) }
        when 'padStart'    { native($name, -> @a, $ {
            my $w = to-num(@a[0] // 0e0).Int;
            my $p = @a.elems > 1 ?? to-str(@a[1]) !! ' ';
            my $out = $s;
            $out = $p ~ $out while $out.chars < $w;
            $out.chars > $w ?? $out.substr($out.chars - $w) !! $out
        }) }
        when 'padEnd'      { native($name, -> @a, $ {
            my $w = to-num(@a[0] // 0e0).Int;
            my $p = @a.elems > 1 ?? to-str(@a[1]) !! ' ';
            my $out = $s;
            $out = $out ~ $p while $out.chars < $w;
            $out.chars > $w ?? $out.substr(0, $w) !! $out
        }) }
        when 'concat'      { native($name, -> @a, $ { $s ~ @a.map(-> $x { to-str($x) }).join }) }
        default            { UNDEF }
    }
}

sub slice-str(Str $s, @a) {
    my $n = $s.chars;
    my $b = @a.elems > 0 ?? to-num(@a[0]).Int !! 0;
    my $e = @a.elems > 1 && !is-undef(@a[1]) ?? to-num(@a[1]).Int !! $n;
    $b += $n if $b < 0;  $e += $n if $e < 0;
    $b = 0 if $b < 0;    $e = 0 if $e < 0;
    $b = $n if $b > $n;  $e = $n if $e > $n;
    $b < $e ?? $s.substr($b, $e - $b) !! ''
}

sub arr-prop($arr, Str $name) {
    given $name {
        when 'length'  { $arr.elems.Num }
        when 'push'    { native($name, -> @a, $ { for @a -> $x { $arr.push($x) }; $arr.elems.Num }) }
        when 'pop'     { native($name, -> @a, $ { $arr.elems ?? $arr.pop !! UNDEF }) }
        when 'shift'   { native($name, -> @a, $ { $arr.elems ?? $arr.shift !! UNDEF }) }
        when 'unshift' { native($name, -> @a, $ { for @a.reverse -> $x { $arr.unshift($x) }; $arr.elems.Num }) }
        when 'slice'   { native($name, -> @a, $ {
            my $n = $arr.elems;
            my $b = @a.elems > 0 ?? to-num(@a[0]).Int !! 0;
            my $e = @a.elems > 1 && !is-undef(@a[1]) ?? to-num(@a[1]).Int !! $n;
            $b += $n if $b < 0;  $e += $n if $e < 0;
            $b = 0 if $b < 0;    $e = 0 if $e < 0;
            $b = $n if $b > $n;  $e = $n if $e > $n;
            my @out;
            for $b ..^ $e -> $i { @out.push($arr[$i]) }
            @out
        }) }
        when 'indexOf' { native($name, -> @a, $ {
            my $needle = @a[0] // UNDEF;
            for ^$arr.elems -> $i { return $i.Num if js-strict-eq($arr[$i] // UNDEF, $needle) }
            -1e0
        }) }
        when 'includes' { native($name, -> @a, $ {
            my $needle = @a[0] // UNDEF;
            for ^$arr.elems -> $i { return True if js-strict-eq($arr[$i] // UNDEF, $needle) }
            False
        }) }
        when 'join'    { native($name, -> @a, $ {
            my $sep = @a.elems ?? to-str(@a[0]) !! ',';
            $arr.map(-> $x { is-undef($x // UNDEF) || is-null($x // UNDEF) ?? '' !! to-str($x) }).join($sep)
        }) }
        when 'map'     { native($name, -> @a, $ {
            my @out;
            for ^$arr.elems -> $i { @out.push(cb(@a[0], $arr[$i] // UNDEF, $i.Num)) }
            @out
        }) }
        when 'filter'  { native($name, -> @a, $ {
            my @out;
            for ^$arr.elems -> $i { @out.push($arr[$i]) if truthy(cb(@a[0], $arr[$i] // UNDEF, $i.Num)) }
            @out
        }) }
        when 'forEach' { native($name, -> @a, $ {
            for ^$arr.elems -> $i { cb(@a[0], $arr[$i] // UNDEF, $i.Num) }
            UNDEF
        }) }
        when 'reduce'  { native($name, -> @a, $ {
            my $acc;
            my $start = 0;
            if @a.elems > 1 { $acc = @a[1] }
            else {
                die-js "TypeError: reduce of empty array with no initial value" unless $arr.elems;
                $acc = $arr[0]; $start = 1;
            }
            for $start ..^ $arr.elems -> $i { $acc = call-value(@a[0], [$acc, $arr[$i] // UNDEF, $i.Num], UNDEF) }
            $acc
        }) }
        when 'find'    { native($name, -> @a, $ {
            for ^$arr.elems -> $i { return $arr[$i] if truthy(cb(@a[0], $arr[$i] // UNDEF, $i.Num)) }
            UNDEF
        }) }
        when 'findIndex' { native($name, -> @a, $ {
            for ^$arr.elems -> $i { return $i.Num if truthy(cb(@a[0], $arr[$i] // UNDEF, $i.Num)) }
            -1e0
        }) }
        when 'some'    { native($name, -> @a, $ {
            for ^$arr.elems -> $i { return True if truthy(cb(@a[0], $arr[$i] // UNDEF, $i.Num)) }
            False
        }) }
        when 'every'   { native($name, -> @a, $ {
            for ^$arr.elems -> $i { return False unless truthy(cb(@a[0], $arr[$i] // UNDEF, $i.Num)) }
            True
        }) }
        when 'concat'  { native($name, -> @a, $ {
            my @out;
            for @$arr -> $x { @out.push($x) }
            for @a -> $x {
                if $x ~~ Positional { for @$x -> $y { @out.push($y) } }
                else { @out.push($x) }
            }
            @out
        }) }
        when 'reverse' { native($name, -> @a, $ {
            my @r = $arr.reverse;
            $arr.pop while $arr.elems;
            for @r -> $x { $arr.push($x) }
            $arr
        }) }
        when 'sort'    { native($name, -> @a, $ {
            my @sorted;
            if @a.elems && !is-undef(@a[0]) {
                my $cmp = @a[0];
                # `<=> 0` not hand-built Less/More: --exe ignores constructed Order values
                @sorted = $arr.sort(-> $x, $y {
                    to-num(call-value($cmp, [$x, $y], UNDEF)) <=> 0e0
                });
            }
            else {
                @sorted = $arr.sort(-> $x, $y { to-str($x) leg to-str($y) });   # JS default: string order
            }
            $arr.pop while $arr.elems;
            for @sorted -> $x { $arr.push($x) }
            $arr
        }) }
        when 'flat'    { native($name, -> @a, $ {
            my @out;
            for @$arr -> $x {
                if $x ~~ Positional { for @$x -> $y { @out.push($y) } }
                else { @out.push($x) }
            }
            @out
        }) }
        default        { UNDEF }
    }
}
sub eval-stmts(@stmts, Env $env) {
    # hoist function declarations so mutual recursion works
    for @stmts -> $s {
        if $s<t> eq 'func' {
            $env.declare($s<name>, JSFunc.new(
                name => $s<name>, params => $s<params>.list, body => $s<body>,
                expr-body => False, env => $env, kind => 'fn'));
        }
    }
    my $last = UNDEF;
    for @stmts -> $s {
        $last = eval-stmt($s, $env);
    }
    $last
}

sub eval-stmt($n, Env $env) {
    given $n<t> {
        when 'exprstmt' { return eval-expr($n<expr>, $env) }
        when 'decl' {
            for $n<decls>.list -> $d {
                my $v = $d<init>.defined ?? eval-expr($d<init>, $env) !! UNDEF;
                $env.declare($d<name>, $v, const => ($n<kind> eq 'const'));
            }
        }
        when 'func' { }   # hoisted in eval-stmts
        when 'class' {
            my $parent = $n<sup>.defined ?? $env.get($n<sup>) !! Nil;
            die-js "TypeError: superclass is not a class" if $parent.defined && !($parent ~~ JSClass);
            my $ctor = Nil;
            my %methods;
            my %statics;
            my @fields;
            for $n<members>.list -> $m {
                if $m<t> eq 'method' {
                    my $f = JSFunc.new(
                        name => $m<name>, params => $m<params>.list, body => $m<body>,
                        expr-body => False, env => $env,
                        kind => ($m<name> eq 'constructor' ?? 'ctor' !! 'method'));
                    if $m<name> eq 'constructor' { $ctor = $f }
                    elsif $m<static> { %statics{$m<name>} = $f }
                    else { %methods{$m<name>} = $f }
                }
                else {
                    if $m<static> {
                        %statics{$m<name>} = $m<init>.defined ?? eval-expr($m<init>, $env) !! UNDEF;
                    }
                    else {
                        @fields.push(%( name => $m<name>, init => $m<init> ));
                    }
                }
            }
            my $cls = JSClass.new(name => $n<name>, parent => $parent,
                                  ctor => $ctor, methods => %methods,
                                  statics => %statics, fields => @fields, env => $env);
            $env.declare($n<name>, $cls);
        }
        when 'enum' {
            my $obj = JSObject.new;
            my $next = 0e0;
            for $n<members>.list -> $m {
                my $v = $m<init>.defined ?? eval-expr($m<init>, $env) !! $next;
                $obj.set($m<name>, $v);
                $obj.set(js-num-str($v), $m<name>) if $v ~~ Num;   # reverse mapping
                $next = ($v ~~ Num ?? $v !! $next) + 1e0;
            }
            $env.declare($n<name>, $obj, :const);
        }
        when 'if' {
            if truthy(eval-expr($n<cond>, $env)) { eval-stmt($n<then>, $env) }
            elsif $n<else>.defined { eval-stmt($n<else>, $env) }
        }
        when 'while' {
            while truthy(eval-expr($n<cond>, $env)) {
                my $brk = False;
                {
                    eval-stmt($n<body>, $env);
                    CATCH {
                        when BrkX { $brk = True }
                        when CntX { }
                    }
                }
                last if $brk;
            }
        }
        when 'switch' {
            my $disc = eval-expr($n<disc>, $env);
            my $senv = Env.new(parent => $env);
            my @cases = $n<cases>.list;
            # find the matching case (===) or default, then run from there with fall-through
            my $start = -1;
            for ^@cases.elems -> $i {
                my $c = @cases[$i];
                if $c<kind> eq 'case' && js-strict-eq($disc, eval-expr($c<test>, $senv)) { $start = $i; last }
            }
            if $start < 0 {
                for ^@cases.elems -> $i { if @cases[$i]<kind> eq 'default' { $start = $i; last } }
            }
            if $start >= 0 {
                {
                    for $start ..^ @cases.elems -> $i {
                        for @cases[$i]<body>.list -> $s { eval-stmt($s, $senv) }
                    }
                    CATCH {
                        when BrkX { }          # break leaves the switch
                    }
                }
            }
        }
        when 'dowhile' {
            my $go = True;
            while $go {
                my $brk = False;
                {
                    eval-stmt($n<body>, $env);
                    CATCH {
                        when BrkX { $brk = True }
                        when CntX { }
                    }
                }
                last if $brk;
                $go = truthy(eval-expr($n<cond>, $env));
            }
        }
        when 'for' {
            my $fenv = Env.new(parent => $env);
            eval-stmt($n<init>, $fenv) if $n<init>.defined;
            while !$n<cond>.defined || truthy(eval-expr($n<cond>, $fenv)) {
                my $brk = False;
                {
                    eval-stmt($n<body>, $fenv);
                    CATCH {
                        when BrkX { $brk = True }
                        when CntX { }
                    }
                }
                last if $brk;
                eval-expr($n<upd>, $fenv) if $n<upd>.defined;
            }
        }
        when 'forof' {
            my $it = eval-expr($n<iter>, $env);
            my @items;
            given $it {
                when Positional { @items = @$it }
                when Str        { @items = $it.comb }
                default { die-js "TypeError: " ~ display($it) ~ " is not iterable" }
            }
            my $brk = False;
            for @items -> $item {
                my $ienv = Env.new(parent => $env);
                my @vars = $n<vars>.list;
                if @vars.elems == 1 {
                    $ienv.declare(@vars[0], $item // UNDEF);
                }
                else {
                    # for (const [k, v] of pairs) — element-wise destructuring
                    for ^@vars.elems -> $vi {
                        my $el = ($item ~~ Positional && $vi < $item.elems) ?? $item[$vi] !! UNDEF;
                        $ienv.declare(@vars[$vi], $el // UNDEF);
                    }
                }
                {
                    eval-stmt($n<body>, $ienv);
                    CATCH {
                        when BrkX { $brk = True }
                        when CntX { }
                    }
                }
                last if $brk;
            }
        }
        when 'block' {
            eval-stmts($n<stmts>.list, Env.new(parent => $env));
        }
        when 'return'   { RetX.new(value => ($n<expr>.defined ?? eval-expr($n<expr>, $env) !! UNDEF)).throw }
        when 'break'    { BrkX.new.throw }
        when 'continue' { CntX.new.throw }
        when 'throw'    { ThrX.new(value => eval-expr($n<expr>, $env)).throw }
        when 'try' {
            my $pending = Nil;
            {
                eval-stmt($n<body>, $env);
                CATCH {
                    when ThrX {
                        my $exc = $_;
                        if $n<cbody>.defined {
                            my $cenv = Env.new(parent => $env);
                            $cenv.declare($n<cvar>, $exc.value) if $n<cvar>.defined;
                            {
                                eval-stmt($n<cbody>, $cenv);
                                CATCH { default { $pending = $_ } }
                            }
                        }
                        else { $pending = $exc }
                    }
                    default { $pending = $_ }
                }
            }
            eval-stmt($n<fbody>, $env) if $n<fbody>.defined;
            $pending.rethrow if $pending.defined;
        }
        when 'empty' { }
        default { die "internal: unknown statement type $n<t>" }
    }
    UNDEF
}

sub eval-call($n, Env $env) {
    my $callee = $n<callee>;
    my @args;
    for $n<args>.list -> $a { @args.push(eval-expr($a, $env)) }
    # obj.m(...) and obj[k](...) pass the receiver as `this`
    if $callee<t> eq 'member' {
        my $obj = eval-expr($callee<obj>, $env);
        return SHORT if $obj ~~ ShortCircuit;
        return SHORT if $callee<opt> && (is-null($obj) || is-undef($obj));
        my $f = get-prop($obj, $callee<name>);
        return SHORT if $n<opt> && (is-null($f) || is-undef($f));   # obj.m?.()
        die-js "TypeError: " ~ display($obj, :nested) ~ "." ~ $callee<name> ~ " is not a function"
            if is-undef($f);
        return call-value($f, @args, $obj);
    }
    if $callee<t> eq 'index' {
        my $obj = eval-expr($callee<obj>, $env);
        return SHORT if $obj ~~ ShortCircuit;
        return SHORT if $callee<opt> && (is-null($obj) || is-undef($obj));
        my $f = get-prop($obj, to-str(eval-expr($callee<e>, $env)));
        return SHORT if $n<opt> && (is-null($f) || is-undef($f));
        return call-value($f, @args, $obj);
    }
    if $callee<t> eq 'supermethod' {
        my $this = $env.get('this');
        my $cls = $env.get('%class%');
        my $r = $cls.parent.defined ?? $cls.parent.lookup($callee<name>) !! Nil;
        die-js "TypeError: super." ~ $callee<name> ~ " is not a function" unless $r.defined;
        return call-value(with-class($r[0], $r[1]), @args, $this);
    }
    my $fn = eval-expr($callee, $env);
    return SHORT if $fn ~~ ShortCircuit;
    return SHORT if $n<opt> && (is-null($fn) || is-undef($fn));   # f?.()
    call-value($fn, @args, UNDEF)
}

sub eval-expr($n, Env $env) {
    given $n<t> {
        when 'ident' { $env.get($n<name>) }
        when 'num'   { $n<v> }
        when 'bin'   { js-binop($n<op>, eval-expr($n<l>, $env), eval-expr($n<r>, $env)) }
        when 'call'  { eval-call($n, $env) }
        when 'member' {
            my $obj = eval-expr($n<obj>, $env);
            return SHORT if $obj ~~ ShortCircuit;
            return SHORT if $n<opt> && (is-null($obj) || is-undef($obj));
            get-prop($obj, $n<name>)
        }
        when 'cond' {
            truthy(eval-expr($n<cond>, $env)) ?? eval-expr($n<then>, $env) !! eval-expr($n<else>, $env)
        }
        when 'str'   { $n<v> }
        when 'bool'  { $n<v> }
        when 'null'  { NULL }
        when 'undef' { UNDEF }
        when 'this'  { $env.has-name('this') ?? $env.get('this') !! UNDEF }
        when 'tmpl'  {
            my $out = '';
            for $n<parts>.list -> $p {
                $out ~= $p<kind> eq 'text' ?? $p<s> !! to-str(eval-expr($p<e>, $env));
            }
            $out
        }
        when 'array' {
            my @a;
            for $n<items>.list -> $e { @a.push(eval-expr($e, $env)) }
            @a
        }
        when 'object' {
            my $obj = JSObject.new;
            for $n<pairs>.list -> $p {
                given $p<kind> {
                    when 'pair'      { $obj.set($p<key>, eval-expr($p<value>, $env)) }
                    when 'shorthand' { $obj.set($p<key>, $env.get($p<key>)) }
                    when 'method'    { $obj.set($p<key>, eval-expr($p<fn>, $env)) }
                }
            }
            $obj
        }
        when 'arrowfn' {
            JSFunc.new(name => Nil, params => $n<params>.list, body => $n<body>,
                       expr-body => ?$n<expr-body>, env => $env, kind => 'arrow')
        }
        when 'funcexpr' {
            my $f = JSFunc.new(name => $n<name>, params => $n<params>.list, body => $n<body>,
                               expr-body => False, env => $env, kind => 'fn');
            # a named function expression can call itself
            if $n<name>.defined {
                my $inner = Env.new(parent => $env);
                $inner.declare($n<name>, $f);
                $f = JSFunc.new(name => $n<name>, params => $n<params>.list, body => $n<body>,
                                expr-body => False, env => $inner, kind => 'fn');
                $inner.set($n<name>, $f);
            }
            $f
        }
        when 'logic' {
            my $l = eval-expr($n<l>, $env);
            given $n<op> {
                when '&&' { truthy($l) ?? eval-expr($n<r>, $env) !! $l }
                when '||' { truthy($l) ?? $l !! eval-expr($n<r>, $env) }
                default   { (is-null($l) || is-undef($l)) ?? eval-expr($n<r>, $env) !! $l }   # ??
            }
        }
        when 'unary' {
            given $n<op> {
                when 'typeof' {
                    # typeof x never throws, even for undeclared x
                    if $n<e><t> eq 'ident' && !$env.has-name($n<e><name>) { return 'undefined' }
                    js-typeof(eval-expr($n<e>, $env))
                }
                when '!'    { !truthy(eval-expr($n<e>, $env)) }
                when '-'    { -to-num(eval-expr($n<e>, $env)) }
                when '~'    { to-int32(+^ to-int32(eval-expr($n<e>, $env))).Num }
                when 'void' { eval-expr($n<e>, $env); UNDEF }
                when 'delete' {
                    my $t = $n<e>;
                    if $t<t> eq 'member' {
                        my $obj = eval-expr($t<obj>, $env);
                        $obj.del($t<name>) if $obj ~~ JSObject;
                        True
                    }
                    elsif $t<t> eq 'index' {
                        my $obj = eval-expr($t<obj>, $env);
                        $obj.del(to-str(eval-expr($t<e>, $env))) if $obj ~~ JSObject;
                        True
                    }
                    else { True }
                }
                default  { to-num(eval-expr($n<e>, $env)) }   # unary +
            }
        }
        when 'optchain' {
            my $v = eval-expr($n<e>, $env);
            $v ~~ ShortCircuit ?? UNDEF !! $v
        }
        when 'index' {
            my $obj = eval-expr($n<obj>, $env);
            return SHORT if $obj ~~ ShortCircuit;
            return SHORT if $n<opt> && (is-null($obj) || is-undef($obj));
            my $key = eval-expr($n<e>, $env);
            given $obj {
                when Positional {
                    my $i = to-num($key).Int;
                    ($i < 0 || $i >= $obj.elems) ?? UNDEF !! ($obj[$i] // UNDEF)
                }
                when Str {
                    my $i = to-num($key).Int;
                    ($i < 0 || $i >= $obj.chars) ?? UNDEF !! $obj.substr($i, 1)
                }
                default { get-prop($obj, to-str($key)) }
            }
        }
        when 'supercall' {
            my $this = $env.get('this');
            my $cls = $env.get('%class%');
            my @args;
            for $n<args>.list -> $a { @args.push(eval-expr($a, $env)) }
            my $r = $cls.parent.defined ?? $cls.parent.lookup-ctor !! Nil;
            call-value(with-class($r[0], $r[1]), @args, $this) if $r.defined;
            UNDEF
        }
        when 'supermethod' {
            my $this = $env.get('this');
            my $cls = $env.get('%class%');
            my $r = $cls.parent.defined ?? $cls.parent.lookup($n<name>) !! Nil;
            $r.defined ?? Bound.new(fn => with-class($r[0], $r[1]), receiver => $this) !! UNDEF
        }
        when 'new' {
            my $cls = $env.get($n<cls>);
            my @args;
            for $n<args>.list -> $a { @args.push(eval-expr($a, $env)) }
            given $cls {
                when JSClass  { instantiate($cls, @args) }
                when NativeFn { my $fn = $cls.fn; $fn(@args, UNDEF) }   # e.g. new Error(...)
                default { die-js "TypeError: $n<cls> is not a constructor" }
            }
        }
        when 'assign' {
            my $t = $n<target>;
            my $op = $n<op>;
            # logical assignment short-circuits: `a &&= b` assigns only when a is truthy
            if $op eq '&&=' || $op eq '||=' || $op eq '??=' {
                my $cur = eval-expr($t, $env);
                my $do = $op eq '&&=' ?? truthy($cur)
                      !! $op eq '||=' ?? !truthy($cur)
                      !!                 (is-null($cur) || is-undef($cur));
                return $cur unless $do;
                my $v = eval-expr($n<value>, $env);
                assign-to($t, $v, $env);
                return $v;
            }
            my $v = eval-expr($n<value>, $env);
            if $op ne '=' {
                my $cur = eval-expr($t, $env);
                $v = js-binop($op.chop, $cur, $v);   # `<<=` → `<<`, `+=` → `+`
            }
            assign-to($t, $v, $env);
            $v
        }
        when 'postop' {
            my $old = to-num(eval-expr($n<target>, $env));
            assign-to($n<target>, ($n<op> eq '++' ?? $old + 1e0 !! $old - 1e0), $env);
            $old
        }
        default { die "internal: unknown expression type $n<t>" }
    }
}

sub assign-to($t, $v, Env $env) {
    given $t<t> {
        when 'ident'  { $env.set($t<name>, $v) }
        when 'member' { set-prop(eval-expr($t<obj>, $env), $t<name>, $v) }
        when 'index'  {
            my $obj = eval-expr($t<obj>, $env);
            my $key = eval-expr($t<e>, $env);
            given $obj {
                when Positional {
                    my $i = to-num($key).Int;
                    die-js "RangeError: invalid array index" if $i < 0;
                    $obj.push(UNDEF) while $obj.elems < $i;
                    $obj[$i] = $v;
                }
                default { set-prop($obj, to-str($key), $v) }
            }
        }
        default { die-js "SyntaxError: invalid assignment target" }
    }
}

# ---------- builtins ----------------------------------------------------
sub make-global-env(--> Env) {
    my $g = Env.new;

    my $console = JSObject.new;
    $console.set('log',   native('log',   -> @a, $ { say @a.map(-> $x { display($x) }).join(' '); UNDEF }));
    $console.set('error', native('error', -> @a, $ { note @a.map(-> $x { display($x) }).join(' '); UNDEF }));
    $console.set('warn',  native('warn',  -> @a, $ { note @a.map(-> $x { display($x) }).join(' '); UNDEF }));
    $g.declare('console', $console, :const);

    my $math = JSObject.new;
    $math.set('PI', pi.Num);
    $math.set('E',  e.Num);
    $math.set('floor',  native('floor',  -> @a, $ { to-num(@a[0] // UNDEF).floor.Num }));
    $math.set('ceil',   native('ceil',   -> @a, $ { to-num(@a[0] // UNDEF).ceiling.Num }));
    $math.set('round',  native('round',  -> @a, $ { my $x = to-num(@a[0] // UNDEF); ($x - $x.floor == 0.5e0) ?? ($x.floor + 1e0) !! $x.round.Num }));
    $math.set('trunc',  native('trunc',  -> @a, $ { to-num(@a[0] // UNDEF).truncate.Num }));
    $math.set('abs',    native('abs',    -> @a, $ { to-num(@a[0] // UNDEF).abs }));
    $math.set('sign',   native('sign',   -> @a, $ { my $x = to-num(@a[0] // UNDEF); $x.isNaN ?? NaN !! ($x > 0e0 ?? 1e0 !! ($x < 0e0 ?? -1e0 !! 0e0)) }));
    $math.set('sqrt',   native('sqrt',   -> @a, $ { my $x = to-num(@a[0] // UNDEF); $x < 0e0 ?? NaN !! $x.sqrt }));
    $math.set('pow',    native('pow',    -> @a, $ { to-num(@a[0] // UNDEF) ** to-num(@a[1] // UNDEF) }));
    $math.set('min',    native('min',    -> @a, $ { @a.elems ?? @a.map(-> $x { to-num($x) }).min !! Inf }));
    $math.set('max',    native('max',    -> @a, $ { @a.elems ?? @a.map(-> $x { to-num($x) }).max !! -Inf }));
    $math.set('random', native('random', -> @a, $ { rand.Num }));
    $math.set('log',    native('log',    -> @a, $ { my $x = to-num(@a[0] // UNDEF); $x <= 0e0 ?? ($x == 0e0 ?? -Inf !! NaN) !! $x.log }));
    $math.set('exp',    native('exp',    -> @a, $ { to-num(@a[0] // UNDEF).exp }));
    $math.set('hypot',  native('hypot',  -> @a, $ { @a.map(-> $x { to-num($x) ** 2e0 }).sum.sqrt }));
    $g.declare('Math', $math, :const);

    my $json = JSObject.new;
    $json.set('stringify', native('stringify', -> @a, $ {
        my $indent = 0;
        if @a.elems > 2 && @a[2] ~~ Num { $indent = @a[2].Int }
        my $r = json-stringify(@a[0] // UNDEF, :$indent);
        $r // UNDEF
    }));
    $g.declare('JSON', $json, :const);

    my $object = JSObject.new;
    $object.set('%instanceof%', 'Object');
    $object.set('keys', native('keys', -> @a, $ {
        my $o = @a[0] // UNDEF;
        my @out;
        if $o ~~ JSObject { for $o.names -> $k { @out.push($k) } }
        elsif $o ~~ Positional { for ^$o.elems -> $i { @out.push(~$i) } }
        @out
    }));
    $object.set('values', native('values', -> @a, $ {
        my $o = @a[0] // UNDEF;
        my @out;
        if $o ~~ JSObject { for $o.names -> $k { @out.push($o.get($k)) } }
        elsif $o ~~ Positional { for @$o -> $x { @out.push($x) } }
        @out
    }));
    $object.set('entries', native('entries', -> @a, $ {
        my $o = @a[0] // UNDEF;
        my @out;
        if $o ~~ JSObject {
            for $o.names -> $k { my @pair = $k, $o.get($k); @out.push(@pair) }
        }
        @out
    }));
    $g.declare('Object', $object, :const);

    my $arraycls = JSObject.new;
    $arraycls.set('%instanceof%', 'Array');
    $arraycls.set('isArray', native('isArray', -> @a, $ { (@a[0] // UNDEF) ~~ Positional }));
    $arraycls.set('from', native('from', -> @a, $ {
        my $src = @a[0] // UNDEF;
        my @out;
        given $src {
            when Str        { @out = $src.comb }
            when Positional { @out = @$src }
        }
        @out
    }));
    $g.declare('Array', $arraycls, :const);

    # Number / String / Boolean are namespace objects that are also callable:
    # a JSObject with a '%call%' property is invoked through it.
    my $numcls = JSObject.new;
    $numcls.set('%call%', native('Number', -> @a, $ { to-num(@a[0] // UNDEF) }));
    $numcls.set('isInteger', native('isInteger', -> @a, $ {
        my $v = @a[0] // UNDEF;
        $v ~~ Num && !$v.isNaN && $v != Inf && $v != -Inf && $v == $v.Int
    }));
    $numcls.set('parseFloat', native('parseFloat', -> @a, $ { parse-float(to-str(@a[0] // UNDEF)) }));
    $numcls.set('MAX_SAFE_INTEGER', 9007199254740991e0);
    $g.declare('Number', $numcls, :const);

    my $strcls = JSObject.new;
    $strcls.set('%call%', native('String', -> @a, $ { @a.elems ?? to-str(@a[0]) !! '' }));
    $strcls.set('fromCharCode', native('fromCharCode', -> @a, $ {
        @a.map(-> $x { chr(to-num($x).Int) }).join
    }));
    $g.declare('String', $strcls, :const);

    my $boolcls = JSObject.new;
    $boolcls.set('%call%', native('Boolean', -> @a, $ { truthy(@a[0] // UNDEF) }));
    $g.declare('Boolean', $boolcls, :const);

    $g.declare('parseInt', native('parseInt', -> @a, $ {
        my $s = to-str(@a[0] // UNDEF).trim;
        my $radix = @a.elems > 1 && !is-undef(@a[1]) ?? to-num(@a[1]).Int !! 10;
        my $sign = 1e0;
        if $s.starts-with('-') { $sign = -1e0; $s = $s.substr(1) }
        elsif $s.starts-with('+') { $s = $s.substr(1) }
        if $radix == 16 && ($s.starts-with('0x') || $s.starts-with('0X')) { $s = $s.substr(2) }
        my $digits = '';
        for $s.comb -> $c {
            my $d = '0123456789abcdefghijklmnopqrstuvwxyz'.index($c.lc);
            last if !$d.defined || $d >= $radix;
            $digits ~= $c;
        }
        $digits eq '' ?? NaN !! $sign * $digits.parse-base($radix).Num
    }), :const);
    $g.declare('parseFloat', native('parseFloat', -> @a, $ { parse-float(to-str(@a[0] // UNDEF)) }), :const);
    $g.declare('isNaN', native('isNaN', -> @a, $ { to-num(@a[0] // UNDEF).isNaN }), :const);
    $g.declare('NaN', NaN, :const);
    $g.declare('Infinity', Inf, :const);
    $g.declare('globalThis', UNDEF);

    $g.declare('Error', native('Error', -> @a, $ {
        my $o = JSObject.new;
        $o.set('name', 'Error');
        $o.set('message', @a.elems ?? to-str(@a[0]) !! '');
        $o
    }), :const);

    $g
}

sub parse-float(Str $s0) {
    my $s = $s0.trim;
    my $m = $s ~~ / ^ ('-'|'+')? \d+ ['.' \d+]? [<[eE]> <[+-]>? \d+]? /;
    $m ?? (~$m).Num !! NaN
}

# ---------- AST dump ----------------------------------------------------
sub dump-ast($n, Int $level = 0) {
    my $pad = '  ' x $level;
    given $n {
        when Associative {
            my $head = $n<t> // '';
            say $pad, $head eq '' ?? '·' !! $head;
            for $n.keys.sort -> $k {
                next if $k eq 't';
                my $v = $n{$k};
                if $v ~~ Associative || ($v ~~ Positional && $v.elems && ($v[0] ~~ Associative || $v[0] ~~ Positional)) {
                    say $pad, '  ', $k, ':';
                    dump-ast($v, $level + 2);
                }
                elsif $v ~~ Positional {
                    say $pad, '  ', $k, ': [', $v.map(-> $x { $x // 'nil' }).join(', '), ']';
                }
                else {
                    say $pad, '  ', $k, ': ', ($v // 'nil');
                }
            }
        }
        when Positional {
            for @$n -> $item { dump-ast($item, $level) }
        }
        default { say $pad, $n // 'nil' }
    }
}

# ---------- driver ------------------------------------------------------
sub parse-js(Str $src0) {
    my $src = insert-asi(strip-comments($src0));
    my $m = JSGrammar.parse($src, actions => JSActions.new);
    unless $m {
        my $p = JSGrammar.subparse($src, actions => JSActions.new);
        my $pos = $p ?? $p.to !! 0;
        my $line = $src.substr(0, $pos).comb("\n").elems + 1;
        die "SyntaxError: cannot parse (around line $line)";
    }
    $m.made;
}

sub run-js(Str $src, Env $env) {
    my $ast = parse-js($src);
    eval-stmts($ast<stmts>.list, $env)
}

sub run-program(Str $src) {
    my $env = make-global-env();
    {
        run-js($src, $env);
        CATCH {
            when ThrX {
                my $v = .value;
                my $msg = $v ~~ JSObject && $v.has-key('message')
                    ?? to-str($v.get('name') // 'Error') ~ ': ' ~ to-str($v.get('message'))
                    !! display($v);
                note "Uncaught $msg";
                exit 1;
            }
            when RetX { }                    # stray top-level return: ignore
            default { note .message; exit 1 }
        }
    }
}

sub repl {
    my $env = make-global-env();
    say "js.raku — a JavaScript/TypeScript interpreter in Raku (rakupp). Ctrl-D to exit.";
    while (my $line = prompt('js> ')).defined {
        my $src = $line.trim;
        next if $src eq '';
        $src ~= ';' unless $src.ends-with(';') || $src.ends-with('}');
        {
            my $ast;
            {
                $ast = parse-js($src);
                CATCH {
                    default {
                        # a `}`-terminated statement may still need its `;`
                        $ast = parse-js($src ~ ';');
                    }
                }
            }
            my $v = eval-stmts($ast<stmts>.list, $env);
            say display($v, :nested) unless is-undef($v);
            CATCH {
                when ThrX { note "Uncaught " ~ display(.value) }
                default   { note .message }
            }
        }
    }
    say '';
}

sub MAIN($file?, Str :$ast, Str :$asi) {
    if $asi.defined {
        print insert-asi(strip-comments($asi.IO.slurp));
    }
    elsif $ast.defined {
        dump-ast(parse-js($ast.IO.slurp));
    }
    elsif $file.defined {
        run-program($file.IO.slurp);
    }
    else {
        repl;
    }
}
