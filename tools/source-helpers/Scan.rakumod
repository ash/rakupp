unit module Scan;

# Cut a C++ source file into its top-level items: every function definition,
# declaration, variable, type, preprocessor line or #if group that sits directly
# in the global scope or in a namespace block. Items are whole-line ranges that
# tile the file exactly: an item owns every line from the end of the previous one
# (its leading comment included) through the line of its closing `}` or `;`.
# Two items that share a line are one item.
#
# Everything works on lines: Raku++'s substr on a long string costs time in
# proportion to the offset, and this file is 2.5 MB.

# Comments and the insides of string and character literals become spaces (the
# newlines stay), so braces, semicolons and identifiers can be read off the
# result without a C++ lexer.
sub mask(Str $src --> Str) is export {
    $src.subst(
        / '//' \N*
        | '/*' .*? '*/'
        | 'R"' (<-[(\s\\"]>*) '(' .*? ')' $0 '"'
        | '"' [ '\\' . | <-["\\\n]> ]* '"'
        | "'" [ '\\' . | <-['\\\n]> ]+ "'"
        /,
        -> $m {
            my $s = ~$m;
            if $s.starts-with('"') || $s.starts-with('R"') || $s.starts-with("'") {
                my $q = $s.starts-with("'") ?? "'" !! '"';
                my $open = $s.index($q);
                $s.substr(0, $open + 1) ~ $s.substr($open + 1, $s.chars - $open - 2).subst(/\N/, ' ', :g) ~ $q
            }
            else {
                $s.subst(/\N/, ' ', :g)
            }
        }, :g)
}

class Item is export {
    has Int $.l1;              # first line (0-based), leading comments included
    has Int $.l2 is rw;        # last line
    has Int $.code is rw;      # first line with code on it
    has Str $.kind is rw;      # func fdecl var type template alias pp ppgroup ns-open ns-close
    has Str $.name is rw = '';
    has Bool $.static is rw = False;
    has Bool $.inline is rw = False;
    has Bool $.const is rw = False;
    has Str $.head is rw = '';     # masked text from the code to the body's `{` (or the `;`)
    has $.brace is rw;             # [line, col] of the body's `{`, when it has one
    has @.idents;
    has Int $.container is rw = -1;   # -1 = global, else the index of its ns-open item
    method lines() { $!l2 - $!l1 + 1 }
    method line1() { $!code + 1 }     # 1-based, for people
}

sub scan(Str $src, :$masked --> List) is export {
    my $m = $masked // mask($src);
    my @ml = $m.split("\n");
    @ml.pop if @ml && @ml[*-1] eq '';      # the file's final newline

    my @items;
    my @containers = (0,);          # brace depth at which the current container's items sit
    my @cidx = (-1,);               # item index of each open container
    my $depth = 0;
    my $start = 0;                  # first line of the current item
    my $code;                       # its first line with code, once seen
    my $brace;                      # its first `{` at item level, [line, col]
    my $wait-semi = False;
    my $kind;                       # what ended it, when it has ended mid-line
    my $open-kind;                  # a pending end: the item ended but the line goes on

    my &emit = -> Str $k, Int $ln {
        my $it = Item.new(:l1($start), :l2($ln), :code($code // $ln), :kind($k));
        $it.brace = $brace;
        $it.container = @cidx[*-1];
        @items.push: $it;
        $start = $ln + 1; $code = Nil; $brace = Nil; $wait-semi = False; $open-kind = Nil;
    };

    my $ln = 0;
    while $ln < @ml {
        my $line = @ml[$ln];
        my $at-level = $depth == @containers[*-1];
        if $line ~~ /^ \h* '#'/ {
            # a directive, with its continuation lines
            my $last = $ln;
            $last++ while @ml[$last].ends-with('\\') && $last < @ml.end;
            if $at-level && !$code.defined && !$open-kind.defined {
                if $line ~~ /^ \h* '#' \h* 'if'/ {
                    # an #if group is atomic: find its #endif
                    my $nest = 0; my $d = $depth; my $j = $ln;
                    loop {
                        my $u = @ml[$j];
                        if $u ~~ /^ \h* '#' \h* 'if'/ { $nest++ }
                        elsif $u ~~ /^ \h* '#' \h* 'endif'/ { $nest--; last if $nest == 0 }
                        elsif $u !~~ /^ \h* '#'/ { $d += $u.comb('{') - $u.comb('}') }
                        $j++;
                        die "unterminated #if at line {$ln + 1}" if $j > @ml.end;
                    }
                    die "#if group at line {$ln + 1} does not balance its braces" unless $d == $depth;
                    $code = $ln;
                    emit('ppgroup', $j);
                    $ln = $j + 1;
                    next;
                }
                $code = $ln;
                emit('pp', $last);
            }
            $ln = $last + 1;
            next;
        }
        for $line.comb.kv -> $col, $s {
            next if $s eq q[ ] || $s eq "\t";  # (not a global match: Raku++ runs code in a subject after a matched `{`)
            $at-level = $depth == @containers[*-1];
            if $s ne '{' && $s ne '}' && $s ne ';' {
                $code //= $ln;
                next;
            }
            if $s eq '{' {
                if $at-level && !$brace.defined {
                    my $head = head-text(@ml, $code // $ln, $ln, $col);
                    if $head ~~ /^ \s* [inline \s+]? namespace [\s+ <[\w:]>+]? \s* '{' $/
                       || $head ~~ /^ \s* extern \s* '"' \s* '"' \s* '{' $/ {
                        $depth++;
                        emit('ns-open', $ln);   # a namespace line holds nothing else
                        @containers.push: $depth;
                        @cidx.push: @items.end;
                        next;
                    }
                    $code //= $ln;
                    $brace = [$ln, $col];
                }
                $depth++;
            }
            elsif $s eq '}' {
                $depth--;
                if @containers > 1 && $depth + 1 == @containers[*-1] {
                    die "unterminated item before line {$ln + 1}" if $open-kind.defined || $code.defined && $code < $ln;
                    @containers.pop; @cidx.pop;
                    $code = $ln;
                    emit('ns-close', $ln);
                    next;   # the next item starts on a later line
                }
                elsif $depth == @containers[*-1] {
                    my $head = head-text(@ml, $code, $brace[0], $brace[1]);
                    if $head ~~ /<|w> [struct|class|union|enum] <|w>/ && $head !~~ /'('/
                       || has-top-eq($head) {
                        $wait-semi = True;
                    }
                    else {
                        $open-kind //= 'body';
                    }
                }
            }
            elsif $s eq ';' && $at-level {
                $open-kind //= 'semi';
            }
            $code //= $ln;
            if $open-kind.defined && $at-level && $depth == @containers[*-1] {
                # ended; does the line go on? then the next item shares it: merge
                my $rest = $line.substr($col + 1);
                if $rest !~~ /\S/ { emit($open-kind, $ln); last }
                # keep the item open but let the next statement end it
                $brace = Nil unless $open-kind eq 'body';
                $wait-semi = False;
            }
        }
        if $open-kind.defined && $depth == @containers[*-1] && $start <= $ln {
            emit($open-kind, $ln);
        }
        $ln++;
    }
    if $start <= @ml.end {
        die "code left over after the last item at line {$start + 1}" if $code.defined;
        @items[*-1].l2 = @ml.end;
    }
    for @items -> $it { classify($it, @ml) }
    @items.List
}

sub head-text(@ml, Int $from, Int $to, Int $col --> Str) {
    my @parts = @ml[$from .. $to];
    @parts[*-1] = @parts[*-1].substr(0, $col + 1);
    @parts.join(' ')
}

# Is there an initializer's `=` outside every bracket? Not `==`, `<=`, `!=`,
# `operator=`, and `->` is not a closing angle bracket.
sub has-top-eq(Str $s --> Bool) {
    my $t = $s.subst(/'operator' \s* <[=!<>+\-*\/%&|^]>+/, 'operatorX', :g).subst('->', '  ', :g);
    my $d = 0;
    my @c = $t.comb;
    for @c.kv -> $i, $c {
        if $c eq '(' | '[' | '<' { $d++ }
        elsif $c eq ')' | ']' | '>' { $d-- }
        elsif $c eq '=' && $d <= 0 {
            my $prev = $i > 0 ?? @c[$i - 1] !! '';
            my $next = $i < @c.end ?? @c[$i + 1] !! '';
            return True unless $next eq '=' || $prev eq '=' | '!' | '<' | '>';
        }
    }
    False
}

my constant %NOT-NAME = set <__attribute__ alignas decltype noexcept sizeof alignof __declspec if while for switch return>;

sub classify(Item $it, @ml) {
    $it.idents.append: @ml[$it.l1 .. $it.l2].join(' ').comb(/<[A..Za..z_]>\w*/).unique;
    return if $it.kind eq 'pp' | 'ppgroup' | 'ns-open' | 'ns-close';
    my $head = $it.brace
        ?? head-text(@ml, $it.code, $it.brace[0], $it.brace[1] - 1)
        !! @ml[$it.code .. $it.l2].join(' ');
    $head .= subst(/\s+/, ' ', :g);
    $head .= trim;
    $head .= subst(/';' $/, '') unless $it.brace;
    $head .= trim;
    $it.head = $head;

    my $h = $head;
    if $h ~~ /^ template \s* '<'/ {
        $it.kind = 'template';
        my $d = 0; my $k = $h.index('<');
        for $h.comb.kv -> $p, $c {
            next if $p < $k;
            $d++ if $c eq '<'; $d-- if $c eq '>';
            if $d == 0 { $k = $p + 1; last }
        }
        $h = $h.substr($k).trim;
    }
    my $lead = $h.substr(0, $h.index('(') // $h.chars);
    $it.static = so $lead ~~ /<|w> static <|w>/;
    $it.inline = so $lead ~~ /<|w> [inline|constexpr] <|w>/;
    $it.const  = so $lead ~~ /<|w> [const|constexpr] <|w>/ && $lead !~~ /'*'|'&'/;

    if $h ~~ /^ [using|typedef] <|w>/ {
        $it.kind = 'alias' unless $it.kind eq 'template';
        if $h ~~ /^ using \s+ (\w+) \s* '='/ { $it.name = ~$0 }
        elsif $h ~~ /^ typedef .* <|w> (\w+) \s* $/ { $it.name = ~$0 }
        return;
    }
    if $h !~~ /'('/ && $h ~~ /^ [ ['[[' .*? ']]' | static | inline | constexpr | const | thread_local | extern] \s+ ]* [struct|class|union|enum [\s+ class]?] \s+ ['[[' .*? ']]' \s*]? (\w+)/ {
        $it.name = ~$0;
        $it.kind = 'type' unless $it.kind eq 'template';
        return;
    }
    # a function: the identifier before the first ( that is not an attribute
    my $paren;
    for $h.indices('(') -> $p {
        next if angle-depth($h, $p) > 0;
        if $h.substr(0, $p) ~~ / ( [ 'operator' \s* <-[\s(]>+ | <[\w:~]>+ ] ) \s* $/ {
            my $w = ~$0;
            next if $w (elem) %NOT-NAME;
            $paren = $p; $it.name = $w;
            last;
        }
    }
    my $body = $it.kind eq 'body';
    if $paren.defined && !has-top-eq($h.substr(0, $paren)) {
        $it.kind = $it.kind eq 'template' ?? 'template' !! $body ?? 'func' !! 'fdecl';
        return;
    }
    # a variable: the last identifier before its initializer
    my $decl = $h;
    my $d = 0;
    for $h.comb.kv -> $p, $c {
        if $c eq '(' | '[' | '<' { $d++ } elsif $c eq ')' | ']' | '>' { $d-- }
        elsif ($c eq '=' || $c eq '{') && $d <= 0 { $decl = $h.substr(0, $p); last }
    }
    $decl .= subst(/ '[' <-[\]]>* ']' \s* $/, '') for ^2;
    $decl ~~ / (\w+) \s* $/;
    $it.name = $0 ?? ~$0 !! '';
    $it.kind = 'var' unless $it.kind eq 'template';
}

# How deep in template angle brackets is position $p? (`->`, `<<`, `>>=` and
# `operator<` are not brackets.)
sub angle-depth(Str $s, Int $p --> Int) is export {
    my $t = $s.substr(0, $p).subst(/'operator' \s* <[<>=]>+/, '', :g).subst('->', '', :g).subst('<<', '', :g);
    $t.comb('<').elems - $t.comb('>').elems
}
