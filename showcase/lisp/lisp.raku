#!/usr/bin/env raku
# A small but real Scheme, written with a Raku grammar for the reader and a
# tree-walking evaluator for everything else. It has lexical closures, tail
# positions, quote/quasiquote, `define`/`lambda`/`let`/`cond`/`and`/`or`, a
# numeric tower that rides on Raku's own (so exact `Rat`s and bignums come for
# free), and enough built-ins to run the demo programs in `examples/`.
#
#   build/rakupp showcase/lisp/lisp.raku showcase/lisp/examples/fact.scm
#   build/rakupp showcase/lisp/lisp.raku            # no file → a REPL
#
# The point of the showcase is the front end: `token`/`rule`/`regex` plus an
# actions class turn source text straight into Raku data structures, which is
# exactly the job a language implementation exists to do.

# ---------- data model --------------------------------------------------
# S-expressions map onto native Raku values:
#   numbers  -> Int / Rat / Num        strings -> Str
#   symbols  -> Sym (wrapper so a symbol is distinct from a string)
#   lists    -> Array                  booleans -> Bool
#   nil/()   -> the empty Array        procedures -> Closure or a Raku Callable
class Sym {
    has Str $.name;
    method Str { $!name }
    method gist { $!name }
}
my %interned;
sub sym(Str $n) { %interned{$n} //= Sym.new(name => $n) }

class Closure {
    has @.params;
    has $.rest;         # Sym for a variadic tail, or Nil
    has @.body;
    has $.env;
}

# An environment is a pair (frame-hash, parent-env-or-Nil).
class Env {
    has %.vars;
    has $.parent;
    method lookup(Str $n) {
        my $e = self;
        while $e.defined {
            if $e.vars{$n}:exists { return $e.vars{$n} }
            $e = $e.parent;
        }
        die "unbound symbol: $n";
    }
    method set(Str $n, $v) {
        my $e = self;
        while $e.defined {
            if $e.vars{$n}:exists { return $e.define($n, $v) }
            $e = $e.parent;
        }
        die "set! on unbound symbol: $n";
    }
    method define(Str $n, $v) { %!vars{$n} = $v; $v }
}

# ---------- reader: a grammar + actions ---------------------------------
grammar Scheme {
    token TOP        { <.ws> [ <form>+ % <.ws> ]? <.ws> }
    token form       { <atom> | <list> | <quoted> }

    token ws         { \s* }

    token list       { '(' <.ws> [ <form>+ % <.ws> ]? <.ws> ')' }
    token quoted     { <sigil> <.ws> <form> }
    token sigil      { '\'' | '`' | ',@' | ',' }

    # An atom is either a string literal or a bareword. Numbers, booleans and
    # symbols are all barewords — the action classifies them, so there is no
    # longest-token ambiguity between "42" (number) and a symbol named "42".
    token atom    { <string> | <word> }
    token word    { <symchar>+ }

    token number  { '-'? \d+ [ '/' \d+ | '.' \d+ ]? }
    token string  { '"' $<chars>=[ <-["\\]> | '\\' . ]* '"' }
    token symchar { <-[\s()"';`,]> }
}

class Actions {
    method TOP($/)  { make $<form>».ast }
    method form($/) {
        make ($<atom> // $<list> // $<quoted>).ast;
    }
    method list($/) { make [ |$<form>».ast ] }
    method quoted($/) {
        my $wrap = $<sigil>.Str eq '\''  ?? 'quote'
                !! $<sigil>.Str eq '`'   ?? 'quasiquote'
                !! $<sigil>.Str eq ',@'  ?? 'unquote-splicing'
                !!                          'unquote';
        make [ sym($wrap), $<form>.ast ];
    }
    method atom($/) { make ($<string> // $<word>).ast }
    method word($/) {
        my $s = $/.Str;
        if    $s eq '#t' { make True }
        elsif $s eq '#f' { make False }
        elsif $s ~~ /^ '-'? \d+ ['/' \d+ | '.' \d+]? $/ {
            make $s.contains('/') ?? $s.Rat
              !! $s.contains('.') ?? $s.Num
              !!                     $s.Int;
        }
        else { make sym($s) }
    }
    method string($/) {
        my $inner = $<chars>.Str;
        $inner ~~ s:g/'\\n'/\n/;
        $inner ~~ s:g/'\\t'/\t/;
        $inner ~~ s:g/'\\"'/"/;
        $inner ~~ s:g/'\\\\'/\\/;
        make $inner;
    }
}

# Strip `;`-to-end-of-line comments, but leave any `;` inside a "..." string
# alone. Doing this here keeps the grammar's whitespace rule trivial.
sub strip-comments(Str $src) {
    my @c = $src.comb;
    my $out = '';
    my $in-str = False;
    my $i = 0;
    while $i < @c.elems {
        my $ch = @c[$i];
        if $in-str {
            $out ~= $ch;
            if $ch eq '\\' { $out ~= (@c[$i + 1] // ''); $i += 2; next }
            $in-str = False if $ch eq '"';
            $i++;
        }
        elsif $ch eq '"' { $in-str = True; $out ~= $ch; $i++ }
        elsif $ch eq ';' { $i++ while $i < @c.elems && @c[$i] ne "\n" }
        else             { $out ~= $ch; $i++ }
    }
    $out
}

sub read-all(Str $src) {
    my $m = Scheme.parse(strip-comments($src), actions => Actions.new);
    die "parse error" unless $m;
    $m.ast;
}

# ---------- printer -----------------------------------------------------
sub write-str($v) {
    given $v {
        when Sym     { $v.name }
        when Bool    { $v ?? '#t' !! '#f' }
        when Str     { '"' ~ $v.subst('\\', '\\\\', :g).subst('"', '\\"', :g) ~ '"' }
        when Closure { '#<procedure>' }
        when Callable { '#<builtin>' }
        when Array   { '(' ~ $v.map(&write-str).join(' ') ~ ')' }
        when !.defined { "#<undef>" }
        default      { ~$v }
    }
}
# display: like write but strings unquoted
sub display-str($v) {
    $v ~~ Str ?? $v !! write-str($v)
}

# ---------- evaluator ---------------------------------------------------
sub truthy($v) { !($v ~~ Bool && $v == False) }

sub eval-seq(@forms, $env) {
    my $r;
    $r = evaluate($_, $env) for @forms;
    $r
}

sub evaluate($x, $env) {
    # self-evaluating
    return $x if $x ~~ Int | Rat | Num | Str | Bool;
    return $env.lookup($x.name) if $x ~~ Sym;

    # everything else is a list form
    my @l = @($x);
    return [] unless @l;                          # empty list evaluates to nil

    my $head = @l[0];
    if $head ~~ Sym {
        given $head.name {
            when 'quote'  { return @l[1] }
            when 'if'     {
                return truthy(evaluate(@l[1], $env))
                    ?? evaluate(@l[2], $env)
                    !! (@l[3]:exists ?? evaluate(@l[3], $env) !! Nil);
            }
            when 'define' {
                if @l[1] ~~ Array {               # (define (f a b) body...)
                    my @sig  = @(@l[1]);
                    my $name = @sig.shift;
                    $env.define($name.name, make-closure(@sig, @l[2..*], $env));
                    return Nil;
                }
                else {
                    $env.define(@l[1].name, evaluate(@l[2], $env));
                    return Nil;
                }
            }
            when 'set!'   { return $env.set(@l[1].name, evaluate(@l[2], $env)) }
            when 'lambda' { return make-closure(@(@l[1]), @l[2..*], $env) }
            when 'begin'  { return eval-seq(@l[1..*], $env) }
            when 'and'    {
                my $r = True;
                for @l[1..*] -> $c { $r = evaluate($c, $env); return $r unless truthy($r) }
                return $r;
            }
            when 'or'     {
                for @l[1..*] -> $c { my $r = evaluate($c, $env); return $r if truthy($r) }
                return False;
            }
            when 'cond'   {
                for @l[1..*] -> $clause {
                    my @c = @($clause);
                    if @c[0] ~~ Sym && @c[0].name eq 'else' {
                        return eval-seq(@c[1..*], $env);
                    }
                    my $t = evaluate(@c[0], $env);
                    return truthy($t) && @c > 1 ?? eval-seq(@c[1..*], $env) !! $t
                        if truthy($t);
                }
                return Nil;
            }
            when 'let'    {
                my $new = Env.new(vars => {}, parent => $env);
                for @(@l[1]) -> $bind {
                    my @b = @($bind);
                    $new.define(@b[0].name, evaluate(@b[1], $env));
                }
                return eval-seq(@l[2..*], $new);
            }
            when 'quasiquote' { return qq-expand(@l[1], $env) }
        }
    }

    # application. Build @args with an explicit push so that an argument which is
    # itself a list (an Array) stays a single element instead of being flattened.
    my $proc = evaluate($head, $env);
    my @args;
    @args.push(evaluate(@l[$_], $env)) for 1 ..^ @l.elems;
    apply($proc, @args)
}

sub make-closure(@sig, @body, $env) {
    # a trailing `. rest` becomes the variadic tail
    my @params;
    my $rest = Nil;
    my $i = 0;
    while $i < @sig.elems {
        if @sig[$i] ~~ Sym && @sig[$i].name eq '.' {
            $rest = @sig[$i + 1];
            last;
        }
        @params.push: @sig[$i];
        $i++;
    }
    Closure.new(params => @params, rest => $rest, body => [@body], env => $env)
}

sub apply($proc, @args) {
    if $proc ~~ Closure {
        my $new = Env.new(vars => {}, parent => $proc.env);
        for $proc.params.kv -> $idx, $p {
            $new.define($p.name, @args[$idx]);
        }
        if $proc.rest.defined {
            my @tail;
            @tail.push(@args[$_]) for $proc.params.elems ..^ @args.elems;
            $new.define($proc.rest.name, @tail);
        }
        return eval-seq($proc.body, $new);
    }
    elsif $proc ~~ Callable {
        # Pass the whole argument list as one itemised value: every builtin takes a
        # single `(@a)` parameter. This dodges rakupp's `|@list` over-flattening and
        # the slurpy single-argument rule, so a list argument stays one element.
        return $proc($(@args));
    }
    die "not a procedure: {write-str($proc)}";
}

# quasiquote expansion (one level, handles unquote / unquote-splicing)
sub qq-expand($x, $env) {
    return $x unless $x ~~ Array;
    return $x unless $x.elems;
    if $x[0] ~~ Sym && $x[0].name eq 'unquote' {
        return evaluate($x[1], $env);
    }
    my @out;
    for @($x) -> $el {
        if $el ~~ Array && $el.elems && $el[0] ~~ Sym && $el[0].name eq 'unquote-splicing' {
            @out.append: @(evaluate($el[1], $env));
        }
        else {
            @out.push: qq-expand($el, $env);
        }
    }
    @out
}

# ---------- built-in procedures ----------------------------------------
sub global-env() {
    my $g = Env.new(vars => {}, parent => Nil);
    my %b;

    # Every builtin takes ONE parameter `@a` — the Scheme argument list — so a list
    # argument is never flattened by the call. Numeric ops ride on Raku's own tower.
    %b{'+'} = sub (@a) { @a ?? [+] @a !! 0 };
    %b{'*'} = sub (@a) { @a ?? [*] @a !! 1 };
    %b{'-'} = sub (@a) { @a.elems == 1 ?? -@a[0] !! @a[0] - [+] @a[1..*] };
    %b{'/'} = sub (@a) { @a.elems == 1 ?? 1 / @a[0] !! @a[0] / [*] @a[1..*] };
    %b{'='} = sub (@a) { [==] @a };
    %b{'<'}  = sub (@a) { [<]  @a };
    %b{'>'}  = sub (@a) { [>]  @a };
    %b{'<='} = sub (@a) { [<=] @a };
    %b{'>='} = sub (@a) { [>=] @a };
    %b<abs>    = sub (@a) { abs @a[0] };
    %b<min>    = sub (@a) { @a.min };
    %b<max>    = sub (@a) { @a.max };
    %b<modulo> = sub (@a) { @a[0] % @a[1] };
    %b<remainder> = sub (@a) { @a[0] - (@a[0] div @a[1]) * @a[1] };
    %b<quotient>  = sub (@a) { (@a[0] / @a[1]).Int };
    %b<gcd>    = sub (@a) { [gcd] @a };
    %b<expt>   = sub (@a) { @a[0] ** @a[1] };
    %b<sqrt>   = sub (@a) { @a[0].sqrt };
    %b<number?>  = sub (@a) { so @a[0] ~~ Int | Rat | Num };
    %b<zero?>    = sub (@a) { @a[0] == 0 };
    %b<even?>    = sub (@a) { @a[0] %% 2 };
    %b<odd?>     = sub (@a) { !(@a[0] %% 2) };

    %b<cons>   = sub (@a) {
        my @r; @r.push(@a[0]);
        if @a[1] ~~ Array { @r.push($_) for @(@a[1]) } else { @r.push(@a[1]) }
        @r
    };
    %b<car>    = sub (@a) { @a[0][0] };
    %b<cdr>    = sub (@a) { my @r; @r.push(@a[0][$_]) for 1 ..^ @a[0].elems; @r };
    %b<list>   = sub (@a) { my @r; @r.push($_) for @a; @r };
    %b<length> = sub (@a) { @a[0].elems };
    %b<append> = sub (@a) { my @r; for @a -> $lst { @r.push($_) for @($lst) }; @r };
    %b<reverse> = sub (@a) { my @r; @r.push($_) for @(@a[0]).reverse; @r };
    %b<null?>  = sub (@a) { @a[0] ~~ Array && @a[0].elems == 0 };
    %b<pair?>  = sub (@a) { @a[0] ~~ Array && @a[0].elems > 0 };
    %b<list?>  = sub (@a) { so @a[0] ~~ Array };
    %b<list-ref> = sub (@a) { @a[0][@a[1]] };

    %b<not>    = sub (@a) { !truthy(@a[0]) };
    %b<eq?>    = sub (@a) {
        my ($x, $y) = @a[0], @a[1];
        so ($x ~~ Sym && $y ~~ Sym && $x.name eq $y.name)
         || (($x ~~ Int|Rat|Num) && ($y ~~ Int|Rat|Num) && $x == $y)
         || $x === $y
    };
    %b<equal?> = sub (@a) { write-str(@a[0]) eq write-str(@a[1]) };

    %b<map>    = sub (@a) {
        my $f = @a[0];
        my @cols; @cols.push(@a[$_]) for 1 ..^ @a.elems;
        my $n = Inf; for @cols -> $c { $n = $c.elems if $c.elems < $n }
        $n = 0 if $n == Inf;
        my @out;
        for ^$n -> $i {
            my @call; @call.push($_[$i]) for @cols;
            @out.push(apply($f, @call));
        }
        @out
    };
    %b<for-each> = sub (@a) {
        my $f = @a[0];
        my @cols; @cols.push(@a[$_]) for 1 ..^ @a.elems;
        my $n = Inf; for @cols -> $c { $n = $c.elems if $c.elems < $n }
        $n = 0 if $n == Inf;
        for ^$n -> $i {
            my @call; @call.push($_[$i]) for @cols;
            apply($f, @call);
        }
        Nil
    };
    %b<filter> = sub (@a) {
        my $f = @a[0]; my @out;
        for @(@a[1]) -> $x { @out.push($x) if truthy(apply($f, [$x])) }
        @out
    };
    %b<apply>  = sub (@a) {
        my $f = @a[0];
        my @call; @call.push(@a[$_]) for 1 ..^ @a.elems - 1;
        @call.push($_) for @(@a[*-1]);
        apply($f, @call)
    };
    %b<fold-left> = sub (@a) {
        my $f = @a[0]; my $acc = @a[1];
        for @(@a[2]) -> $x { $acc = apply($f, [$acc, $x]) }
        $acc
    };

    %b<display> = sub (@a) { print display-str(@a[0]); Nil };
    %b<write>   = sub (@a) { print write-str(@a[0]); Nil };
    %b<newline> = sub (@a) { print "\n"; Nil };
    %b<error>   = sub (@a) { die @a.map(&display-str).join(' ') };

    for %b.kv -> $k, $val { $g.define($k, $val) }
    $g
}

# ---------- driver ------------------------------------------------------
sub run-source(Str $src, $env) {
    my $result;
    $result = evaluate($_, $env) for @(read-all($src));
    $result
}

sub repl($env) {
    say "rakupp-scheme — Ctrl-D to exit";
    while (my $line = prompt("λ ")).defined {
        next unless $line.trim;
        my $r = try { run-source($line, $env) };
        if $! { note "error: $!" }
        else  { say write-str($r) unless $r ~~ Nil }
    }
    say "";
}

sub MAIN($file?) {
    my $env = global-env();
    if $file.defined {
        run-source($file.IO.slurp, $env);
    }
    else {
        repl($env);
    }
}
