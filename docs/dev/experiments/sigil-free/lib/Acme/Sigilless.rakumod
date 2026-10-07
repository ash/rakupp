# Acme::Sigilless — write Raku without sigils.
#
# A source-to-source translator: sigil-free Raku in, ordinary Raku out. It only
# INSERTS characters (a sigil before a name, a twigil for an attribute), so the
# output has the same lines as the input and every error message the compiler
# prints points at the line the programmer wrote.
#
# Two ways to say what a variable is, freely mixed:
#
#   * declare it once with a sigil and drop it afterwards
#         my @words = <a b c>;   words.push('d');   say words.elems;
#   * declare it bare and let its uses decide
#         my words = <a b c>;    words.push('d');   # -> @words, from .push
#
# One name means one variable kind in a lexical view: `my @a` and `my $a` side
# by side, or shadowing each other, is an error, and so is a variable whose uses
# disagree (`a[0]` here, `a<k>` there).

module Acme::Sigilless {

class X::Sigilless is Exception {
    has Str $.reason;
    method message { "Acme::Sigilless: $!reason" }
}

my class Tok {
    has Str $.kind;          # ws comment pod str ident var ctx num op open close words regex heredoc
    has Str $.text;
    has Int $.from;
    has Str $.prefix is rw = '';
    has Bool $.call is rw = False;   # a bare `name(…)`: calls a &-variable as written
}

my constant %CLOSER = '(' => ')', '[' => ']', '{' => '}', '<' => '>',
                      '«' => '»', '｢' => '｣', '“' => '”', '‘' => '’';

# Words that are operators when an operator is expected: `x x 3` is a variable,
# the repetition operator and a number.
my constant %INFIX-WORD = set <x xx eq ne lt gt le ge leg cmp and or xor not
    div mod gcd lcm min max before after andthen orelse notandthen but does
    eqv ff fff o Z X R minmax unicmp coll>;

# Words after which a TERM comes: keywords and the common list operators. Any
# other bare word is a value (a variable, a constant, a type), after which an
# operator comes — that is what decides whether `/` divides or starts a regex.
my constant %TERM-WORD = set <if unless elsif else while until for loop given
    when with without orwith repeat return say put print note die warn fail
    take my our has state constant so not and or xor andthen orelse do try
    gather lazy eager start await react whenever supply emit is does but where
    of returns grep map sort first push append unshift prepend join any all
    one none sum min max list item flat slip set bag mix hash done last next
    redo proceed succeed exit sleep sink use need require unit EVAL>;

# Quote words: followed directly by a delimiter they start a quote, not a call.
my constant %QUOTE-WORD = set <q qq Q qw qqw qww qqww m rx s S tr TR ms ss qx qqx>;

# Lower-case type names: the natives, and NativeCall's C types.
my constant %NATIVE-TYPE = set <int int8 int16 int32 int64 uint uint8 uint16
    uint32 uint64 num num32 num64 str byte atomicint array buf8 buf16 buf32
    buf64 blob8 blob16 blob32 blob64 utf8 utf16 utf32 size_t ssize_t long
    longlong ulong ulonglong bool>;

# Names a variable may not have, and which are never read as one: keywords,
# the control routines, the quote words above (a variable `m` would turn
# `m[0]` into a match) and the native types.
my constant %FORBIDDEN = set |<if unless elsif else while until for loop given
    when default with without orwith repeat my our has state constant sub
    method submethod multi proto only class role grammar module package token
    rule regex unit use need require import return and or not so xor do try
    gather take last next redo start react whenever supply emit is does but
    where self augment temp let anon enum subset say put print note die warn
    fail done exit proceed succeed return-rw nextsame callsame nextwith
    callwith samewith lastcall q qq Q qw qqw qww qqww m rx s S tr TR ms ss qx
    qqx>, |%NATIVE-TYPE.keys;

# After a bare variable these words continue the statement; any other term
# after it means the word was a list operator call: `fail 'boom'`, `exit $c`.
my constant %CONTINUES = set <if unless for while until with without given
    when and or not xor andthen orelse notandthen but does is where so>;

my constant %LISTY-METHOD = set <map grep sort split comb lines words list
    Array reverse unique squish flat pairs keys values kv rotor batch Seq
    pick roll permutations combinations antipairs head tail skip zip cross
    produce classify-list>;
my constant %HASHY-METHOD = set <Hash hash classify categorize Map Bag Set
    Mix BagHash SetHash MixHash>;
my constant %ARRAY-MUTATOR = set <push pop shift unshift append prepend splice>;
my constant %SCALAR-OP = set <++ -- += -= *= /= ~= %= **= x= ||= &&= //=>;

# Multi-character operators, longest first where one is a prefix of another.
my constant @OPS = q｢...^ ^..^ **= ||= &&= //= ==> <== --> <-> <=> =:= === !==
    ^fff^ ^ff^ ^fff ^ff
    ... ..^ ^.. .. => -> ++ -- ** == != <= >= ~~ !~~ && || // ^^ ?? !!
    += -= *= /= ~= %= x= .= .? .^ .+ .* :: := ~< ~> +< +> << >> !~ =~｣.words;

#----------------------------------------------------------------------------
# Lexer
#----------------------------------------------------------------------------

my class Lexer {
    has Str $.src;
    has Int $.pos = 0;
    has Tok @.toks;
    has Bool $!term = True;      # is a term expected next?
    has @!heredocs;              # terminators waiting for the next newline
    has Bool $!regex-body = False; # the next `{` opens a token/rule/regex body

    method lex() { self!code(Str); @!toks }

    method !emit(Str $kind, Int $from, Int $to) {
        @!toks.push: Tok.new(:$kind, :text($!src.substr($from, $to - $from)), :$from);
    }

    method !peek(Int $n = 0) { $!src.substr($!pos + $n, 1) }
    method !at(Str $s) { $!src.substr($!pos, $s.chars) eq $s }

    # Code until `$stop` closes at our own depth (a string's `{ … }`), or EOF.
    method !code($stop) {
        my $depth = 0;
        while $!pos < $!src.chars {
            my $c = self!peek;
            my $start = $!pos;
            if $c ~~ /\s/ {
                self!whitespace;
                next;
            }
            if $c eq '#' {
                self!comment;
                next;
            }
            if $stop.defined && $c eq $stop && $depth == 0 {
                $!pos++;
                self!emit('close', $start, $!pos);
                $!term = False;
                return;
            }
            if $c eq '{' && $!regex-body {
                $!regex-body = False;
                self!bracketed('regex', '{', '}');
                $!term = True;    # the next `token` starts a declaration
                next;
            }
            # a reduction `[*]`, `[\+]`, `[max]` where a term belongs is an operator
            if $!term && $c eq '['
               && $!src.substr($!pos, 256) ~~ /^ '[' '\\'? [ <[-+*\/~%^&|<>=!?.,]>+ | max | min | gcd | lcm | and | or | xor | eq | lt | gt | le | ge | cmp | leg | Z | X ] ']' / {
                $!pos += $/.to;
                self!emit('op', $start, $!pos);
                $!term = True;
                next;
            }
            # a set operator `(^)`, `(elem)`, `(<=)` where an operator belongs
            if !$!term && $c eq '(' && $start > 0 && $!src.substr($start - 1, 1) ~~ /\s/
               && $!src.substr($!pos, 16) ~~ /^ '(' [ <[-+^|&.<>=!≼≽]>+ | '!'? [ elem | cont ] ] ')' / {
                $!pos += $/.to;
                self!emit('op', $start, $!pos);
                $!term = True;
                next;
            }
            # a colonpair with a number in front: `:26day`, `:0c`
            if $c eq ':' && $!src.substr($!pos, 64) ~~ /^ ':' \d+ <alpha> [ \w | <[-']> <alpha> ]* / {
                $!pos += $/.to;
                self!emit('num', $start, $!pos);
                $!term = False;
                next;
            }
            if $c eq '(' | '[' | '{' {
                $depth++ if $c eq '{';
                $!pos++;
                self!emit('open', $start, $!pos);
                $!term = True;
                next;
            }
            if $c eq ')' | ']' | '}' {
                $depth-- if $c eq '}';
                $!pos++;
                self!emit('close', $start, $!pos);
                # a block's `}` at the end of a line ends a statement
                $!term = $c eq '}' && $!src.substr($!pos, 256) ~~ /^ \h* [\n | $]/ ?? True !! False;
                next;
            }
            if $c eq ';' | ',' {
                $!pos++;
                self!emit('op', $start, $!pos);
                $!term = True;
                next;
            }
            if $c eq "'" { self!single('\'', "'"); next }
            if $c eq '｢' { self!bracketed('str', '｢', '｣'); next }
            if $c eq '"' { self!double('"'); next }
            if $c eq '“' { self!double('”'); next }
            if $c ~~ /\d/ { self!number; next }
            # `&[+]`, the infix as a noun
            if $c eq '&' && ($!term || $start > 0 && $!src.substr($start - 1, 1) ~~ /\s/)
               && $!src.substr($!pos, 32) ~~ /^ '&[' <-[\]\s]>+ ']' / {
                $!pos += $/.to;
                self!emit('var', $start, $!pos);
                $!term = False;
                next;
            }
            # `%` and `&` are sigils where a term belongs — and also after a
            # space and before a name, which is how a list operator's argument
            # looks (`check %h<a>`, `make %h`), and never how `%` divides
            if $c eq '$' | '@' || $c eq '%' | '&' && !self!at('&&')
               && ($!term || $start > 0 && $!src.substr($start - 1, 1) ~~ /\s/
                   && $!src.substr($!pos + 1, 2) ~~ /^ [ <alpha> | <[$@%&.!*^?:=~]> <alpha> ]/) {
                self!variable;
                next;
            }
            if $c ~~ /<alpha>/ {
                self!word;
                next;
            }
            if $!term && $c eq '<' && !self!at('<->') && !self!at('<==') {
                self!at('<<') ?? self!words('<<', '>>') !! self!words('<', '>');
                next;
            }
            if $!term && $c eq '«' {
                self!words('«', '»');
                next;
            }
            # glued `<` or `«` after a value is a subscript: `h<key>`, `h«$k»`;
            # after a colonpair's name it is the value, whatever it holds: `:x<=>`
            my $pair-name = @!toks >= 2 && @!toks[*-1].kind eq 'ident' && @!toks[*-2].kind eq 'op'
                            && @!toks[*-2].text eq ':';
            if !$!term && $c eq '<' | '«' && $start > 0 && $!src.substr($start - 1, 1) !~~ /\s/
               && ($pair-name || !self!at('<=') && !self!at('<->')) {
                $c eq '«' ?? self!words('«', '»') !! self!at('<<') ?? self!words('<<', '>>') !! self!words('<', '>');
                next;
            }
            if $!term && $c eq '/' && !self!at('//') { self!pattern('/', '/'); next }
            self!operator;
        }
    }

    method !whitespace() {
        my $start = $!pos;
        while $!pos < $!src.chars && self!peek ~~ /\s/ {
            my $nl = self!peek eq "\n";
            $!pos++;
            if $nl {
                self!emit('ws', $start, $!pos);
                $start = $!pos;
                self!heredoc-bodies if @!heredocs;
                self!pod;
                $start = $!pos;
            }
        }
        self!emit('ws', $start, $!pos) if $!pos > $start;
    }

    # pod starts with `=word` at the start of a line
    method !pod() {
        return unless $!src.substr($!pos, 64) ~~ /^ \h* '=' <alpha>/;
        my $rest = $!src.substr($!pos);
        return unless $rest ~~ /^ \h* '=' (<alpha>\w*)/;
        my $directive = ~$0;
        my $start = $!pos;
        if $directive eq 'finish' | 'END' {
            $!pos = $!src.chars;
        }
        elsif $directive eq 'begin' && $rest ~~ /^ \h* '=begin' \h+ (\S+)/ {
            my $name = ~$0;
            if $rest ~~ /\n \h* '=end' \h+ $name \N* / {
                $!pos += $/.to;
            }
            else {
                $!pos = $!src.chars;
            }
        }
        else {
            # an abbreviated block runs to the first blank line
            if $rest ~~ /\n \h* \n/ { $!pos += $/.from + 1 }
            else { $!pos = $!src.chars }
        }
        self!emit('pod', $start, $!pos);
    }

    method !heredoc-bodies() {
        while @!heredocs {
            my $term = @!heredocs.shift;
            my $start = $!pos;
            my $rest = $!src.substr($!pos);
            if $rest ~~ /^^ \h* $term \h* $$ \n?/ {
                $!pos += $/.to;
            }
            else {
                $!pos = $!src.chars;
            }
            self!emit('heredoc', $start, $!pos);
        }
    }

    method !comment() {
        my $start = $!pos;
        my $rest = $!src.substr($!pos, 3);
        my $open = $rest ~~ /^ '#' <[`|=]> (<[([{<«]>)/ ?? ~$0 !! Str;
        if $open {
            $!pos += 2;
            self!skip-bracketed($open, %CLOSER{$open});
        }
        else {
            $!pos++ while $!pos < $!src.chars && self!peek ne "\n";
        }
        self!emit('comment', $start, $!pos);
    }

    # From an opening bracket (at $!pos) to just past its partner, nesting.
    method !skip-bracketed(Str $open, Str $close) {
        my $depth = 0;
        my $olen = $open.chars;
        my $clen = $close.chars;
        while $!pos < $!src.chars {
            if self!peek eq '\\' && $open ne $close { $!pos += 2; next }
            if self!at($open) && $open ne $close { $depth++; $!pos += $olen; next }
            if self!at($close) {
                $depth--;
                $!pos += $clen;
                last if $depth <= 0;
                next;
            }
            $!pos++;
        }
    }

    method !single(Str $open, Str $close) {
        my $start = $!pos;
        $!pos++;
        while $!pos < $!src.chars && self!peek ne $close {
            $!pos += self!peek eq '\\' ?? 2 !! 1;
        }
        $!pos++;
        self!emit('str', $start, $!pos);
        $!term = False;
    }

    method !bracketed(Str $kind, Str $open, Str $close) {
        my $start = $!pos;
        self!skip-bracketed($open, $close);
        self!emit($kind, $start, $!pos);
        $!term = False;
    }

    # A double-quoted string: its `{ … }` blocks are code and are lexed as such.
    method !double(Str $close) {
        my $start = $!pos;
        $!pos++;
        while $!pos < $!src.chars {
            my $c = self!peek;
            if $c eq '\\' { $!pos += 2; next }
            if $c eq $close { $!pos++; last }
            if $c eq '{' {
                self!emit('str', $start, $!pos);
                my $b = $!pos++;
                self!emit('open', $b, $!pos);
                $!term = True;
                self!code('}');
                $start = $!pos;
                next;
            }
            $!pos++;
        }
        self!emit('str', $start, $!pos);
        $!term = False;
    }

    method !words(Str $open, Str $close) {
        self!bracketed('words', $open, $close);
    }

    method !pattern(Str $open, Str $close) {
        my $start = $!pos;
        self!delimited($open, $close);
        self!emit('regex', $start, $!pos);
        $!term = False;
    }

    # Past a quote or regex body from its opening delimiter (at $!pos).
    method !delimited(Str $open, Str $close) {
        if $open eq $close {
            $!pos += $open.chars;
            while $!pos < $!src.chars && !self!at($close) {
                my $c = self!peek;
                if $c eq '\\' { $!pos += 2; next }
                # a character class: its quotes and slashes are just characters
                if self!at('<[') || self!at('<-[') || self!at('<+[') {
                    $!pos++ while $!pos < $!src.chars && self!peek ne '[';
                    $!pos++;
                    while $!pos < $!src.chars && self!peek ne ']' {
                        $!pos += self!peek eq '\\' ?? 2 !! 1;
                    }
                    next;
                }
                if $c eq "'" | '"' {
                    my $q = $c;
                    $!pos++;
                    $!pos++ while $!pos < $!src.chars && self!peek ne $q;
                }
                $!pos++;
            }
            $!pos += $close.chars;
        }
        else {
            self!skip-bracketed($open, $close);
        }
    }

    method !number() {
        my $rest = $!src.substr($!pos, 256);
        $rest ~~ /^ [ '0' <[xob]> <[0..9a..fA..F_]>+
                    | \d <[\d_]>* [ '.' <?before \d> \d <[\d_]>* ]? [ <[eE]> <[+-]>? \d+ ]? ] [ 'i' <!before \w> ]? /;   # 2i is imaginary
        my $start = $!pos;
        $!pos += $/.to;
        self!emit('num', $start, $!pos);
        $!term = False;
    }

    method !variable() {
        my $start = $!pos;
        my $rest = $!src.substr($!pos, 256);
        # contextualizers $( @( %( and @$x
        if $rest ~~ /^ <[$@%&]> <?before <[(\[{]>> / {
            $!pos++;
            self!emit('ctx', $start, $!pos);
            $!term = True;
            return;
        }
        if $rest ~~ /^ <[$@%&]> <[$@%&]> <alpha> [ \w | <[-']> <alpha> ]* /        # @$arr, $%h
           || $rest ~~ /^ <[$@%&]> <[.!*^?:=~]>? <alpha> [ \w | <[-']> <alpha> ]* [ '::' <alpha> [ \w | <[-']> <alpha> ]* ]* /
           || $rest ~~ /^ '$' <[/_!¢]> / || $rest ~~ /^ <[$@]> '_' / || $rest ~~ /^ '$' \d+ /
           || $rest ~~ /^ <[$@%]> <[$@%]> / || $rest ~~ /^ <[$@%&]> / {
            $!pos += $/.to;
            # an operator's name: `&infix:<->`
            $!pos += $/.to if $!src.substr($!pos, 128) ~~ /^ ':' [ '<' <-[>]>+ '>' | '«' <-[»]>+ '»' ] /;
        }
        self!emit('var', $start, $!pos);
        $!term = False;
    }

    method !word() {
        my $start = $!pos;
        my $rest = $!src.substr($!pos, 256);
        $rest ~~ /^ <alpha> [ \w | <[-']> <alpha> ]* [ '::' <alpha> [ \w | <[-']> <alpha> ]* ]* /;
        my $word = ~$/;
        $!pos += $word.chars;
        # an operator's name is one word: `infix:<->`, `prefix:«-»`
        if $!src.substr($!pos, 128) ~~ /^ ':' [ '<' <-[>]>+ '>' | '«' <-[»]>+ '»' ] / {
            $!pos += $/.to;
            $word = $!src.substr($start, $!pos - $start);
        }
        # quote constructs: q{…} rx/…/ m:g/…/ s/…/…/ qq:to/END/
        # not after a `.`: `$x.s` is a method
        my $prior = @!toks ?? self!prev-sig(@!toks.end) !! Tok;
        if %QUOTE-WORD{$word} && !($prior.defined && $prior.kind eq 'op' && $prior.text.starts-with('.')) {
            my $after = $!src.substr($!pos, 256);
            if $after ~~ /^ [ ':' <alpha>\w* [ '(' <-[)]>* ')' ]? ]* <?before <[/{\[<|!'"«｢]>> / {
                my $adverbs = ~$/;
                $!pos += $adverbs.chars;
                my $d = self!peek;
                my $close = %CLOSER{$d} // $d;
                if $adverbs ~~ / ':' [ to | heredoc ] » / {
                    my $q = $!pos;
                    self!delimited($d, $close);
                    my $terminator = $!src.substr($q + 1, $!pos - $q - 2);
                    @!heredocs.push: $terminator;
                    self!emit('str', $start, $!pos);
                    $!term = False;
                    return;
                }
                if $word eq 's' | 'S' | 'tr' | 'TR' && !%CLOSER{$d} {
                    # s/pattern/replacement/: three delimiters
                    $!pos++;
                    for ^2 {
                        while $!pos < $!src.chars && self!peek ne $d {
                            $!pos += self!peek eq '\\' ?? 2 !! 1;
                        }
                        $!pos++;
                    }
                    self!emit('regex', $start, $!pos);
                    $!term = False;
                    return;
                }
                # the body is opaque, qq's `{ … }` included: a known limit
                self!delimited($d, $close);
                self!emit($word eq any(<q qq Q qw qqw qww qqww>) ?? 'str' !! 'regex', $start, $!pos);
                $!term = False;
                return;
            }
        }
        if !$!term && %INFIX-WORD{$word} {
            self!emit('op', $start, $!pos);
            $!term = True;
            return;
        }
        self!emit('ident', $start, $!pos);
        $!regex-body = True if $!term && $word eq any(<token rule regex>);
        my $prev = @!toks.elems > 1 ?? self!prev-sig(@!toks.end - 1) !! Tok;
        # a method name is glued to its `.` or `!` (`self!foo`); `$x! where` is not one
        my $method = $prev.defined && $prev.kind eq 'op' && $prev.text eq any(<. .? .^ .= .+ .* !>)
                      && @!toks[@!toks.end - 1] === $prev;
        $!term = !$method && %TERM-WORD{$word} ?? True !! False;
    }

    method !prev-sig(Int $i is copy) {
        $i-- while $i >= 0 && @!toks[$i].kind eq 'ws' | 'comment' | 'pod';
        $i >= 0 ?? @!toks[$i] !! Tok;
    }

    method !operator() {
        my $start = $!pos;
        my $rest = $!src.substr($!pos, 4);
        my $op = @OPS.first({ $rest.starts-with($_) }) // $rest.substr(0, 1);
        # where a term belongs `+<a b>` is a prefix `+` and a word list
        $op = $op.substr(0, 1) if $!term && $op eq any('+<', '+>', '~<', '~>');
        $!pos += $op.chars;
        self!emit('op', $start, $!pos);
        my $postfix = !$!term && $op eq '++' | '--';
        # a `*` where a term belongs is Whatever, a term itself: `* < 0`, `*.sum`
        my $whatever = $!term && $op eq '*' && $!src.substr($!pos, 256) !~~ /^ <[\w$@%&]>/;
        $!term = !$postfix && !$whatever;
    }
}

#----------------------------------------------------------------------------
# Scopes and variables
#----------------------------------------------------------------------------

my class Var {
    has Str $.name;
    has Int $.line;
    has Int $.from;              # where the declaration is, for "declared earlier"
    has $.declared;           # the sigil written at the declaration, if any
    has Str $.twigil = '';        # '.' or '!' for an attribute
    has Bool $.attr = False;
    has Bool $.slurpy = False;
    has $.decl-tok;           # the bare declaration to put a sigil on
    has Tok @.refs;               # bare references to put a sigil on
    has @.evidence;               # [kind, strength, line, why]
    has $.scope;
    has Str $.sigil is rw;
}

my class Scope {
    has %.vars;
    has $.outer;
    has Bool $.class = False;
    has Int $.nest = 0;
    method lookup(Str $name) {
        my $s = self;
        while $s {
            return $s.vars{$name} if $s.vars{$name}:exists;
            $s = $s.outer;
        }
        Nil
    }
}

my constant STRONG = 3;
my constant MEDIUM = 2;
my constant WEAK   = 1;

my class Analyzer {
    has Str $.src;
    has Tok @.toks;
    has Int @!sig;               # indexes of significant tokens
    has Int %!at;                # token index -> position in @!sig
    has Scope $!scope;
    has Var @.vars;
    has %!consumed;              # sig positions already handled
    has @!pending;               # scopes waiting for their `{`
    has Bool $!class-next = False;
    has Int $!nest = 0;
    has @!brace;                 # per `{`: did it push a scope?
    has Str @.errors;
    has Str @.warnings;
    has @!dups;                  # same name declared twice in one scope
    has %!types;                 # names the program declares as types or constants

    has Int @!nl;                # offsets of the newlines, for line numbers
    method line(Tok $t) {
        @!nl = $!src.indices("\n") unless @!nl || !$!src.contains("\n");
        my ($lo, $hi) = 0, +@!nl;
        while $lo < $hi {
            my $mid = ($lo + $hi) div 2;
            @!nl[$mid] < $t.from ?? ($lo = $mid + 1) !! ($hi = $mid);
        }
        $lo + 1
    }

    method run() {
        for @!toks.kv -> $i, $t {
            next if $t.kind eq 'ws' | 'comment' | 'pod' | 'heredoc';
            %!at{$i} = @!sig.elems;
            @!sig.push: $i;
        }
        # types and constants, wherever they are declared
        for ^@!sig -> $k {
            my $t = self!t($k);
            %!types{self!t($k + 1).text} = True
                if $t.kind eq 'ident' && $t.text eq any(<constant class role grammar subset enum module package>)
                && self!is($k + 1, 'ident');
        }
        $!scope = Scope.new;
        for ^@!sig -> $k {
            self!step($k);
        }
        self!decide;
        self
    }

    method !t(Int $k) { 0 <= $k < @!sig ?? @!toks[@!sig[$k]] !! Tok }
    method !is(Int $k, Str $kind, $text?) {
        my $t = self!t($k);
        $t.defined && $t.kind eq $kind && (!$text.defined || $t.text eq $text)
    }
    # no whitespace between sig token k and the next one
    method !glued(Int $k) {
        0 <= $k < @!sig - 1 && @!sig[$k + 1] == @!sig[$k] + 1
    }

    method !is-type(Int $k) {
        my $t = self!t($k);
        return False unless $t.defined && $t.kind eq 'ident';
        $t.text ~~ /^ <:Lu> / || %NATIVE-TYPE{$t.text} || $t.text.contains('::')
            || %!types{$t.text}
    }

    # A type in front of a declared name: a known type, or any word followed by
    # the name (`my buf32 $w`, `size_t $n`) or a smiley (`array:D \arr`).
    method !type-at(Int $k) {
        return False unless self!is($k, 'ident');
        return False if self!t($k).text eq any(<is where of returns handles does but>);
        return True if self!is-type($k);
        my $n = self!t($k + 1);
        return False unless $n.defined;
        $n.kind eq 'var'
            || $n.kind eq 'ident' && $n.text ne any(<is where of returns handles>) && !%INFIX-WORD{$n.text}
            || $n.kind eq 'op' && $n.text eq '\\'
            || $n.kind eq 'op' && $n.text eq ':' && self!glued($k) && self!is($k + 2, 'ident')
    }

    method !step(Int $k) {
        my $t = self!t($k);
        if $t.kind eq 'open' {
            $!nest++;
            if $t.text eq '{' {
                if @!pending && @!pending.tail.nest == $!nest - 1 {
                    $!scope = @!pending.pop;
                    @!brace.push: True;
                }
                else {
                    $!scope = Scope.new(:outer($!scope), :class($!class-next), :nest($!nest));
                    @!brace.push: True;
                }
                $!class-next = False;
            }
            return;
        }
        if $t.kind eq 'close' {
            $!nest--;
            if $t.text eq '}' && @!brace {
                @!brace.pop;
                $!scope = $!scope.outer // $!scope;
            }
            return;
        }
        if $t.kind eq 'op' && $t.text eq ';' {
            # a signature that never got a body (a stub, a `proto … {*}` aside)
            while @!pending && @!pending.tail.nest == $!nest {
                @!pending.pop;
                $!scope = $!scope.outer // $!scope;
            }
            return;
        }
        if $t.kind eq 'op' && $t.text eq '->' | '<->' {
            self!pointy($k);
            return;
        }
        return unless $t.kind eq 'ident';
        return if %!consumed{$k};
        my $w = $t.text;
        my $p = self!t($k - 1);
        my $after-dot = $p.defined && $p.kind eq 'op' && $p.text eq any(<. .? .^ .= .+ .* .& !>);
        return if $after-dot;
        given $w {
            when 'my' | 'our' | 'state' | 'has' { self!declare($k); return }
            when 'class' | 'role' | 'grammar' | 'module' | 'package' {
                %!consumed{$k + 1} = True;
                $!class-next = $w ne 'module' | 'package';
                $!scope = Scope.new(:outer($!scope), :class(True), :nest($!nest)) if $p.defined && $p.text eq 'unit';
                return;
            }
            when 'sub' | 'method' | 'submethod' {
                self!routine($k);
                return;
            }
            when 'token' | 'rule' | 'regex'
               | 'constant' | 'enum' | 'subset' | 'is' | 'does' | 'of' | 'returns'
               | 'use' | 'need' | 'require' | 'import' | 'trusts' | 'handles' | 'also' {
                %!consumed{$k + 1} = True if self!is($k + 1, 'ident');
                return;
            }
        }
        self!reference($k);
    }

    # my x / my @x / my Int x / my (a, b) / has x / has !x / my \x
    method !declare(Int $k) {
        # `my sub`, `my constant`, `our class`: a declarator, not a variable
        return if self!is($k + 1, 'ident') && self!t($k + 1).text eq any(<sub method submethod multi proto
            only constant class role grammar enum subset module package token rule regex>);
        my $attr = self!t($k).text eq 'has';
        my $j = $k + 1;
        # a type-like word right before `=`, `;` or `,` is the name itself (`my num = 1`)
        while self!type-at($j) && !(self!is($j + 1, 'op') && self!t($j + 1).text eq any('=', ';', ',', ':=', '.='))
              && !self!is($j + 1, 'close') {
            $j++;
            $j += 2 if self!is($j, 'op', ':') && self!glued($j - 1);    # Int:D
            $j++ if self!is($j, 'ident', 'of');                         # Array of Int
        }
        my @names;
        if self!is($j, 'open', '(') {
            my $depth = 0;
            for $j + 1 .. @!sig - 1 -> $m {
                my $t = self!t($m);
                last if $t.kind eq 'close' && $depth == 0;
                $depth++ if $t.kind eq 'open';
                $depth-- if $t.kind eq 'close';
                # `@pc is copy`: a trait and its name declare nothing
                next if self!is($m, 'ident') && self!t($m).text eq any(<is where of>)
                     || self!is($m - 1, 'ident') && self!t($m - 1).text eq any(<is where of>);
                next unless $depth == 0 && $t.kind eq 'ident' | 'var' && !self!type-at($m);
                # my (\a, \b): the `\` carries the name
                @names.push: self!is($m - 1, 'op', '\\') ?? $m - 1 !! $m;
            }
        }
        else {
            @names.push: $j;
        }
        for @names -> $m {
            my $t = self!t($m);
            %!consumed{$m} = True;
            if $t.kind eq 'var' {
                next unless $t.text ~~ /^ (<[$@%&]>) (<[.!]>?) (<alpha> .*) $/;
                self!add-var(~$2, :declared(~$0), :twigil(~$1), :$attr, :tok($t));
            }
            elsif $t.kind eq 'op' && $t.text eq '!' && $attr && self!is($m + 1, 'ident') {
                %!consumed{$m + 1} = True;
                self!add-var(self!t($m + 1).text, :twigil<!>, :attr, :decl-tok($t), :tok($t));
            }
            elsif $t.kind eq 'op' && $t.text eq '\\' && self!is($m + 1, 'ident') {
                %!consumed{$m + 1} = True;
                self!add-var(self!t($m + 1).text, :declared('\\'), :tok($t));
            }
            elsif $t.kind eq 'ident' {
                self!add-var($t.text, :twigil($attr ?? '.' !! ''), :$attr, :decl-tok($t), :tok($t));
            }
        }
        # the initializer of a single declaration says what it holds
        if @names == 1 {
            my $n = @names[0];
            $n++ if self!is($n, 'op', '!') || self!is($n, 'op', '\\');
            my $var = self!var-at($n);
            self!assignment($var, $n + 1) if $var && self!is($n + 1, 'op', '=');
        }
    }

    method !var-at(Int $m) {
        my $t = self!t($m);
        my $name = $t.kind eq 'var' ?? $t.text.subst(/^ <[$@%&]> <[.!]>? /, '') !! $t.text;
        $!scope.vars{$name}
    }

    method !add-var(Str $name, :$declared, :$twigil = '', :$attr = False, :$decl-tok, :$tok, :$slurpy = False) {
        my $line = self.line($tok);
        if %FORBIDDEN{$name} && !$declared {
            @!errors.push: "line $line: '$name' cannot be a sigil-free variable (it is a keyword, "
                ~ "a control routine, a quote word or a native type)";
        }
        if $name ~~ /^ <:Lu> / && !$declared {
            @!errors.push: "line $line: '$name' cannot be a sigil-free variable: a capitalized "
                ~ "name is a type or a constant (`constant $name = …` declares one)";
        }
        my $old = $!scope.vars{$name};
        my $var = Var.new(:$name, :$line, :from($tok.from), :$declared, :$twigil, :$attr, :$decl-tok, :scope($!scope), :$slurpy);
        $!scope.vars{$name} = $var;
        @!vars.push: $var;
        @!dups.push: ($old, $var) if $old;
        $var
    }

    # Soft evidence is a use that an item supports too: `x[0]`, `x<k>`, `x.push`,
    # `x(…)` all work on a `$` holding the right thing, so they pick the kind
    # only when nothing harder does.
    method !evidence(Var $v, Str $kind, Int $strength, Int $k, Str $why, :$soft = False) {
        $v.evidence.push: [$kind, $strength, self.line(self!t($k)), $why, $soft];
    }

    # What does the expression starting at sig position $k (after `=`) hold?
    method !assignment(Var $v, Int $eq, :$param) {
        my $k = $eq + 1;
        my $first = self!t($k);
        return unless $first;
        my $depth = 0;
        my $end = $k;
        my (@top, $last-method);
        for $k .. @!sig - 1 -> $m {
            my $t = self!t($m);
            if $t.kind eq 'open' { $depth++; $end = $m; next }
            if $t.kind eq 'close' {
                last if $depth == 0;
                $depth--;
                $end = $m;
                next;
            }
            last if $depth == 0 && $t.kind eq 'op' && ($t.text eq ';' || $param && $t.text eq ',');
            last if $depth == 0 && $t.kind eq 'ident' && $t.text eq any(<if unless for while until with without>) && $m > $k;
            $end = $m;
            if $depth == 0 {
                @top.push: $m;
                $last-method = $t.text if $t.kind eq 'ident' && self!is($m - 1, 'op', '.');
            }
        }
        my $why = "line {self.line($first)}: initialized from `{self!span($k, $end)}`";
        my @texts = @top.map({ self!t($_).text });
        my $single = $end == $k || ($first.kind eq 'open' && self!closes($k) == $end);
        if @texts.grep({ $_ eq ',' }) && self!t(@top.first({ self!t($_).text eq ',' })).kind eq 'op' {
            self!evidence($v, '@', MEDIUM, $k, "$why (a list)");
        }
        elsif $first.kind eq 'open' && $first.text eq '[' && $single {
            self!evidence($v, '@', MEDIUM, $k, "$why (an Array)");
        }
        elsif $first.kind eq 'open' && $first.text eq '{' && $single {
            my $hashy = $end == $k + 1 || (($k + 1) .. $end).first({ self!is($_, 'op', '=>') });
            self!evidence($v, $hashy ?? '%' !! '&', MEDIUM, $k, "$why ({$hashy ?? 'a Hash' !! 'a block'})");
        }
        elsif $first.kind eq 'ctx' && $first.text eq '%' | '@' {
            self!evidence($v, $first.text, MEDIUM, $k, $why);
        }
        elsif $first.kind eq 'words' && $single {
            self!evidence($v, '@', MEDIUM, $k, "$why (a word list)");
        }
        elsif $first.kind eq 'op' && $first.text eq '->' | '<->'
           || $first.kind eq 'ident' && $first.text eq 'sub' {
            self!evidence($v, '&', MEDIUM, $k, "$why (a routine)");
        }
        elsif @texts.first({ $_ eq any(<.. ... ..^ ^.. ^..^ xx>) }) || ($first.kind eq 'op' && $first.text eq '^') {
            self!evidence($v, '@', MEDIUM, $k, "$why (a range or sequence)");
        }
        elsif $last-method && %LISTY-METHOD{$last-method} {
            self!evidence($v, '@', MEDIUM, $k, "$why (.$last-method returns a list)");
        }
        elsif $last-method && %HASHY-METHOD{$last-method} {
            self!evidence($v, '%', MEDIUM, $k, "$why (.$last-method returns a hash)");
        }
        elsif $single && $first.kind eq 'num' | 'str' {
            self!evidence($v, '$', MEDIUM, $k, "$why (a single value)");
        }
        else {
            self!evidence($v, 'item', MEDIUM, $k, "$why (unknown kind: kept as an item)");
        }
    }

    method !closes(Int $k) {
        my $depth = 0;
        for $k .. @!sig - 1 -> $m {
            my $t = self!t($m);
            $depth++ if $t.kind eq 'open';
            if $t.kind eq 'close' {
                $depth--;
                return $m if $depth == 0;
            }
        }
        @!sig - 1
    }

    method !span(Int $a, Int $b) {
        my $s = $!src.substr(self!t($a).from, self!t($b).from + self!t($b).text.chars - self!t($a).from);
        $s = $s.substr(0, 37) ~ '...' if $s.chars > 40;
        $s.subst(/\s+/, ' ', :g)
    }

    method !reference(Int $k) {
        my $t = self!t($k);
        # `name => value` quotes the name; `:name(…)` is a pair
        return if self!is($k + 1, 'op', '=>');
        return if self!is($k - 1, 'op', ':') && self!glued($k - 1);
        # a keyword stays a keyword even beside a `my $loop` or `my @next`,
        # and a type stays a type beside a `my $Int`
        return if %FORBIDDEN{$t.text} || self!is-type($k);
        # a variable followed by a term is two terms in a row; the word is a
        # list operator call: `fail 'boom'`, `set <a b>`, `ok $ok, …`
        my $after = self!t($k + 1);
        return if $after.defined && !self!glued($k)
            && ($after.kind eq any(<str num var ctx words regex>)
                || $after.kind eq 'ident' && !%CONTINUES{$after.text} && !%INFIX-WORD{$after.text});
        my $v = $!scope.lookup($t.text) or return;
        return if $v.declared && $v.declared eq '\\';
        # `fib(5)` beside a `my @fib` calls the routine, as it does in Raku
        return if $v.declared && self!is($k + 1, 'open', '(') && self!glued($k);
        $v.refs.push: $t;
        my $line = self.line($t);
        my $n = self!t($k + 1);
        if $n.defined && self!glued($k) {
            given $n.kind ~ ' ' ~ $n.text {
                when 'open [' { self!evidence($v, '@', STRONG, $k, "line $line: `{$t.text}[…]` indexes it", :soft) }
                when 'open {' { self!evidence($v, '%', STRONG, $k, "line $line: `{$t.text}\{…}` looks up a key", :soft) }
                when 'open (' { $t.call = True; self!evidence($v, '&', STRONG, $k, "line $line: `{$t.text}(…)` calls it", :soft) }
            }
            if $n.kind eq 'words' {
                self!evidence($v, '%', STRONG, $k, "line $line: `{$t.text}{$n.text}` looks up a key", :soft);
            }
        }
        if $n.defined && $n.kind eq 'op' && $n.text eq '.' && self!is($k + 2, 'ident')
           && %ARRAY-MUTATOR{self!t($k + 2).text} {
            self!evidence($v, '@', STRONG, $k, "line $line: `.{self!t($k + 2).text}` mutates an array", :soft);
        }
        if $n.defined && $n.kind eq 'op' && %SCALAR-OP{$n.text} {
            self!evidence($v, '$', STRONG, $k, "line $line: `{$t.text} {$n.text}` updates a single value");
        }
        my $p = self!t($k - 1);
        if $p.defined && $p.kind eq 'op' && $p.text eq '++' | '--' {
            self!evidence($v, '$', STRONG, $k, "line $line: `{$p.text}{$t.text}` updates a single value");
        }
        if $p.defined && $p.kind eq 'ident' && $p.text eq 'for'
           && (!$n.defined || $n.kind eq 'open' && $n.text eq '{' || $n.kind eq 'op' && $n.text eq '->' | '<->' | ';'
               || $n.kind eq 'close') {
            self!evidence($v, '@', WEAK, $k, "line $line: `for {$t.text}` iterates it");
        }
        if $n.defined && $n.kind eq 'op' && $n.text eq '=' && !$v.attr {
            self!assignment($v, $k + 1);
        }
    }

    # sub name(params) / method name(params) / method !name(params)
    method !routine(Int $k) {
        my $j = $k + 1;
        $j++ if self!is($j, 'op', '!');
        if self!is($j, 'ident') {
            %!consumed{$j} = True;
            $j++;
        }
        if self!is($j, 'open', '(') {
            self!signature($j + 1, ')');
        }
    }

    method !pointy(Int $k) {
        self!signature($k + 1, '{');
    }

    # Declare the parameters from sig position $k up to the closing `)` (or the
    # body's `{`) into a fresh scope that the body's `{` will adopt.
    method !signature(Int $k, Str $end) {
        my $scope = Scope.new(:outer($!scope), :nest($!nest));
        $!scope = $scope;
        @!pending.push: $scope;
        my $depth = 0;
        my $start = True;
        my ($named, $slurpy) = False, False;
        my $m = $k;
        while $m < @!sig {
            my $t = self!t($m);
            if $depth == 0 && ($t.kind eq 'close' && $end eq ')' || $t.kind eq 'open' && $t.text eq '{' && $end eq '{') {
                last;
            }
            if $t.kind eq 'open' { $depth++; $m++; next }
            if $t.kind eq 'close' { $depth--; $m++; next }
            if $depth == 0 && $t.kind eq 'op' && $t.text eq ',' | ';;' {
                ($start, $named, $slurpy) = True, False, False;
                $m++;
                next;
            }
            if $depth == 0 && $t.kind eq 'op' && $t.text eq '-->' {
                $start = False;
            }
            if $start && $depth == 0 {
                if self!type-at($m) {
                    $m++;
                    $m += 2 if self!is($m, 'op', ':') && self!glued($m - 1) && self!is($m + 1, 'ident');
                    next;
                }
                if $t.kind eq 'op' && $t.text eq ':' { $named = True; $m++; next }
                if $t.kind eq 'op' && $t.text eq '*' | '**' { $slurpy = True; $m++; next }
                if $t.kind eq 'op' && $t.text eq '+' | '|' | '\\' {
                    # +x, |c and \x are already sigil-free in Raku
                    if self!is($m + 1, 'ident') {
                        %!consumed{$m + 1} = True;
                        self!add-var(self!t($m + 1).text, :declared('\\'), :tok(self!t($m + 1)));
                        $start = False;
                        $m++;
                    }
                    $m++;
                    next;
                }
                if $t.kind eq 'var' {
                    # an anonymous `$` declares nothing
                    self!add-var(~$1, :declared(~$0), :tok($t))
                        if $t.text ~~ /^ (<[$@%&]>) <[.!]>? (<alpha> .*) $/;
                    $start = False;
                    $m++;
                    next;
                }
                # `Pointer is rw` — a trait on an anonymous parameter;
                # `:q(:$query)` — an alias, the name is inside the parens
                if $t.kind eq 'ident' && ($t.text eq any(<is where of returns handles>)
                                          || self!is($m + 1, 'open', '(') && self!glued($m)) {
                    $start = False;
                    $m++;
                    next;
                }
                if $t.kind eq 'ident' {
                    %!consumed{$m} = True;
                    my $v = self!add-var($t.text, :decl-tok($t), :tok($t), :$slurpy);
                    self!evidence($v, '@', MEDIUM, $m, "line {self.line($t)}: `*{$t.text}` is slurpy") if $slurpy;
                    $start = False;
                    if self!is($m + 1, 'op', '=') && $depth == 0 {
                        self!assignment($v, $m + 1, :param);
                    }
                    $m++;
                    next;
                }
            }
            $m++;
        }
    }

    # Pick each variable's sigil from what was declared and what its uses say.
    method !decide() {
        for @!vars -> $v {
            my @ev = $v.evidence;
            # a soft use fits a `$` and its own kind; anything else contradicts
            my sub fits(@e, $sigil) { $sigil eq '$' || @e[0] eq $sigil }
            if $v.declared {
                # what was written wins; only a use it cannot serve contradicts
                # it (`a<k>` on an @a, `a++` on an @a), never an initializer
                $v.sigil = $v.declared;
                my @against = @ev.grep({ .[4] ?? !fits($_, $v.declared)
                                              !! .[1] == STRONG && .[0] ne $v.declared });
                @!errors.push: "'{$v.name}' is declared {$v.declared}{$v.name} at line {$v.line}, but "
                    ~ @against.map(*[3]).join('; ') if @against && $v.declared ne '\\';
                next;
            }
            my @hard = @ev.grep({ !.[4] && .[0] ne 'item' });
            my @soft = @ev.grep({ .[4] });
            my @pool = @hard || (@ev.first({ .[0] eq 'item' }) ?? () !! @soft);
            my $top = @pool ?? @pool.map(*[1]).max !! 0;
            my @strongest = @pool.grep(*[1] == $top);
            my @kinds = @strongest.map(*[0]).unique;
            if @kinds > 1 {
                @!errors.push: "'{$v.name}' (declared at line {$v.line}) is used as more than one kind of variable: "
                    ~ @strongest.map({ "{.[0]} from {.[3]}" }).join('; ');
                $v.sigil = @kinds[0];
                next;
            }
            $v.sigil = @kinds[0] // ($v.slurpy ?? '@' !! '$');
            my @misfits = @soft.grep({ !fits($_, $v.sigil) });
            if @hard && @misfits {
                @!errors.push: "'{$v.name}' is {$v.sigil}{$v.name} from "
                    ~ @strongest.map(*[3]).join('; ') ~ ', but ' ~ @misfits.map(*[3]).join('; ');
                next;
            }
            my @weaker = @hard.grep({ .[0] ne $v.sigil });
            @!warnings.push: "'{$v.name}' became {$v.sigil}{$v.name}, although " ~ @weaker.map(*[3]).join('; ')
                if @weaker;
        }
        # One name, one kind, wherever a bare name has to be resolved. Sigiled
        # code that never says the bare name (`my @e; my $e`) is ordinary Raku
        # and stays legal.
        my sub bare(Var $v) { !$v.declared || so $v.refs }
        for @!dups -> ($old, $new) {
            next unless bare($old) || bare($new);
            @!errors.push: "line {$new.line}: '{$new.name}' is declared again in the same scope "
                ~ "(first at line {$old.line})";
        }
        for @!vars -> $v {
            my $outer = $v.scope.outer;
            my $o = $outer ?? $outer.lookup($v.name) !! Nil;
            $o = Nil if $o && $o.from > $v.from;    # declared after this one: not visible here
            if $o && $o.sigil ne $v.sigil && $o.sigil ne '\\' && $v.sigil ne '\\' && (bare($o) || bare($v)) {
                @!errors.push: "'{$v.name}' is {$v.sigil}{$v.name} at line {$v.line} but "
                    ~ "{$o.sigil}{$o.name} in an enclosing scope (line {$o.line}); one name, one kind";
            }
        }
        for @!vars -> $v {
            next if $v.sigil eq '\\';
            my $twigil = $v.attr ?? ($v.twigil || '.') !! '';
            if $v.decl-tok {
                $v.decl-tok.prefix = $v.attr && $v.twigil eq '!' ?? $v.sigil !! $v.sigil ~ $twigil;
            }
            for $v.refs -> $r {
                $r.prefix = $v.attr ?? $v.sigil ~ '!' !! $r.call && $v.sigil eq '&' ?? '' !! $v.sigil;
            }
        }
    }

    method explain() {
        @!vars.map({
            my $how = .declared ?? 'declared' !! (.evidence ?? 'inferred' !! 'default');
            sprintf "%-14s line %-4d %-9s %s", (.sigil eq '\\' ?? '\\' !! .sigil) ~ (.attr ?? '!' !! '') ~ .name,
                .line, $how, .evidence.map(*[3]).join('; ')
        }).join("\n")
    }
}

#----------------------------------------------------------------------------
# Interface
#----------------------------------------------------------------------------

sub analyze(Str $src) {
    my @toks = Lexer.new(:$src).lex;
    # SIGILLESS_TOKENS=1 shows what the lexer made of the source
    note @toks.grep(*.kind ne 'ws').map({ "{.kind}:{.text.substr(0, 30).raku}" }).join(' ') if %*ENV<SIGILLESS_TOKENS>;
    Analyzer.new(:$src, :@toks).run
}

our sub translate(Str $src, :$warn = True --> Str) is export {
    my $a = analyze($src);
    die X::Sigilless.new(:reason($a.errors.join("\n  "))) if $a.errors;
    note "Acme::Sigilless: $_" for $warn ?? $a.warnings !! ();
    $a.toks.map({ .prefix ~ .text }).join
}

our sub explain(Str $src --> Str) is export {
    my $a = analyze($src);
    ($a.explain, |$a.errors.map({ "ERROR: $_" }), |$a.warnings.map({ "note: $_" })).join("\n")
}

}

# The engine running us takes over the rest of the program at `use` time:
# rewrite the file after the `use` line and run that instead. The lines before
# it become blank lines, so line numbers stay the same. This works where the
# compiler runs `use` while parsing (Rakudo); an engine that parses the whole
# file first rejects the sigil-free source before we get to run.
sub EXPORT() {
    my $file = $*PROGRAM;
    if $file.defined && $file.IO.f && $file.IO.basename ne '-e' {
        my $src = $file.IO.slurp;
        if $src ~~ / ^^ \h* 'use' \h+ 'Acme::Sigilless' \h* ';' / {
            my $cut = $/.to;
            my $head = $src.substr(0, $cut);
            my $code = $head.subst(/\N/, ' ', :g) ~ $src.substr($cut);
            my $out = $*TMPDIR.add("sigilless-{$*PID}-{$file.IO.basename}");
            $out.spurt: Acme::Sigilless::translate($code);
            my $p = run $*EXECUTABLE, $out, |@*ARGS;
            $out.unlink;
            exit $p.exitcode;
        }
    }
    Map.new
}
