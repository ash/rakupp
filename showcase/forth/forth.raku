#!/usr/bin/env raku
# A Forth interpreter — a stack machine with a word dictionary. Where the Lisp
# showcase walks a parse tree, this is the other classic model: source is a flat
# stream of whitespace-separated *words*, each of which pushes a number or runs
# a dictionary entry against a shared data stack. `: name … ;` defines new words
# in terms of old ones, so the language grows itself.
#
#   build/rakupp showcase/forth/forth.raku showcase/forth/examples/demo.fth
#   build/rakupp showcase/forth/forth.raku            # no file → a REPL
#
# The reader here is deliberately trivial (split on whitespace); the interest is
# the execution model — a data stack, a dictionary, and structured control words
# (if/else/then, begin/until, begin/while/repeat, do/loop) compiled to a little
# node tree and run recursively.

my @stack;          # the data stack (integers)
my @loops;          # active do-loop indices, innermost last (for `i`)
my %words;          # user-defined words: name => list of body nodes
my %builtins;       # primitive words: name => a Raku block acting on the stack

# ---------- reader: source text -> a flat token list -------------------
sub tokenize(Str $src --> List) {
    my @toks;
    for $src.lines -> $raw {
        my $line = $raw;
        $line ~~ s/ '\\' \s .* $ //;             # `\ …` line comment
        $line ~~ s:g/ '(' <-[)]>* ')' //;        # `( … )` inline comment
        @toks.append: $line.words;
    }
    @toks.List;
}

# ---------- parser: tokens -> nested nodes -----------------------------
# Each node is a hash with an `op`. Control words consume their block up to a
# terminator; `: … ;` produces a `define` node holding its body.
sub parse-block(@toks, $ip is rw, @stop --> List) {
    my @nodes;
    while $ip < @toks.elems {
        my $t = @toks[$ip];
        last if $t (elem) @stop;
        $ip++;
        if    $t eq 'if' {
            my $true = parse-block(@toks, $ip, <else then>);
            my @false;
            if $ip < @toks.elems && @toks[$ip] eq 'else' {
                $ip++;
                @false = parse-block(@toks, $ip, <then>);
            }
            $ip++;                                # consume `then`
            @nodes.push: { op => 'if', :$true, false => @false };
        }
        elsif $t eq 'begin' {
            my $body = parse-block(@toks, $ip, <until while>);
            if $ip < @toks.elems && @toks[$ip] eq 'until' {
                $ip++;
                @nodes.push: { op => 'until', :$body };
            }
            else {                                # begin <cond> while <body> repeat
                $ip++;                            # consume `while`
                my $rest = parse-block(@toks, $ip, <repeat>);
                $ip++;                            # consume `repeat`
                @nodes.push: { op => 'while', cond => $body, body => $rest };
            }
        }
        elsif $t eq 'do' {
            my $body = parse-block(@toks, $ip, <loop>);
            $ip++;                                # consume `loop`
            @nodes.push: { op => 'do', :$body };
        }
        elsif $t eq ':' {
            my $name = @toks[$ip++];
            my $body = parse-block(@toks, $ip, <;>);
            $ip++;                                # consume `;`
            @nodes.push: { op => 'define', :$name, :$body };
        }
        elsif $t eq '."' {                        # print a string up to a `"`
            my $s = '';
            while $ip < @toks.elems {
                my $w = @toks[$ip++];
                if $w.ends-with('"') { $s ~= $w.chop; last }
                $s ~= $w ~ ' ';
            }
            @nodes.push: { op => 'print', text => $s };
        }
        elsif $t ~~ /^ '-'? \d+ $/ {
            @nodes.push: { op => 'lit', val => $t.Int };
        }
        else {
            @nodes.push: { op => 'call', name => $t };
        }
    }
    @nodes.List;
}

# ---------- executor ---------------------------------------------------
sub pop(--> Int) { @stack ?? @stack.pop !! die "stack underflow" }

sub exec(@nodes) {
    for @nodes -> $n {
        given $n<op> {
            when 'lit'    { @stack.push($n<val>) }
            when 'print'  { print $n<text> }
            when 'define' { %words{$n<name>} = $n<body> }
            when 'call'   { call-word($n<name>) }
            when 'if'     { if pop() != 0 { exec($n<true>) } else { exec($n<false>) } }
            when 'until'  { repeat { exec($n<body>) } until pop() != 0 }
            when 'while'  {
                loop {
                    exec($n<cond>);
                    last if pop() == 0;
                    exec($n<body>);
                }
            }
            when 'do'     {                       # ( limit start -- )
                my $start = pop();
                my $limit = pop();
                for $start ..^ $limit -> $i {
                    @loops.push($i);
                    exec($n<body>);
                    @loops.pop;
                }
            }
        }
    }
}

sub call-word(Str $name) {
    if %words{$name}:exists {
        exec(%words{$name});
    }
    elsif %builtins{$name}:exists {
        %builtins{$name}();
    }
    elsif $name ~~ /^ '-'? \d+ $/ {
        @stack.push($name.Int);
    }
    else {
        die "unknown word: $name";
    }
}

# ---------- built-in words ---------------------------------------------
# Forth truth: 0 is false, -1 (all bits set) is true.
sub bool($b) { $b ?? -1 !! 0 }
sub bin(&op)  { my $b = pop(); my $a = pop(); @stack.push(op($a, $b).Int) }
sub cmp(&op)  { my $b = pop(); my $a = pop(); @stack.push(bool(op($a, $b))) }

%builtins{'+'}    = { bin(&[+]) };
%builtins{'-'}    = { bin(&[-]) };
%builtins{'*'}    = { bin(&[*]) };
%builtins{'/'}    = { bin({ ($^a / $^b).Int }) };
%builtins{'mod'}  = { bin(&[%]) };
%builtins{'/mod'} = { my $b = pop(); my $a = pop(); @stack.push($a % $b); @stack.push(($a / $b).Int) };
%builtins{'negate'} = { @stack.push(-pop()) };
%builtins{'abs'}  = { @stack.push(abs pop()) };
%builtins{'min'}  = { bin(&[min]) };
%builtins{'max'}  = { bin(&[max]) };
%builtins{'1+'}   = { @stack.push(pop() + 1) };
%builtins{'1-'}   = { @stack.push(pop() - 1) };
%builtins{'2*'}   = { @stack.push(pop() * 2) };
%builtins{'2/'}   = { @stack.push((pop() / 2).Int) };

%builtins{'='}    = { cmp(&[==]) };
%builtins{'<'}    = { cmp(&[<]) };
%builtins{'>'}    = { cmp(&[>]) };
%builtins{'<='}   = { cmp(&[<=]) };
%builtins{'>='}   = { cmp(&[>=]) };
%builtins{'<>'}   = { cmp({ $^a != $^b }) };
%builtins{'0='}   = { @stack.push(bool(pop() == 0)) };
%builtins{'0<'}   = { @stack.push(bool(pop() < 0)) };
%builtins{'0>'}   = { @stack.push(bool(pop() > 0)) };
%builtins{'and'}  = { bin(&[+&]) };
%builtins{'or'}   = { bin(&[+|]) };
%builtins{'xor'}  = { bin(&[+^]) };
%builtins{'invert'} = { @stack.push(+^pop()) };

%builtins{'dup'}  = { my $a = pop(); @stack.push($a); @stack.push($a) };
%builtins{'?dup'} = { my $a = @stack.tail; @stack.push($a) if $a != 0 };
%builtins{'drop'} = { pop() };
%builtins{'swap'} = { my $b = pop(); my $a = pop(); @stack.push($b); @stack.push($a) };
%builtins{'over'} = { my $b = pop(); my $a = pop(); @stack.push($a); @stack.push($b); @stack.push($a) };
%builtins{'rot'}  = { my $c = pop(); my $b = pop(); my $a = pop(); @stack.push($b); @stack.push($c); @stack.push($a) };
%builtins{'nip'}  = { my $b = pop(); pop(); @stack.push($b) };
%builtins{'tuck'} = { my $b = pop(); my $a = pop(); @stack.push($b); @stack.push($a); @stack.push($b) };
%builtins{'depth'} = { @stack.push(@stack.elems) };

%builtins{'i'}    = { @stack.push(@loops.tail // 0) };
%builtins{'j'}    = { @stack.push(@loops[*-2] // 0) };

%builtins{'.'}    = { print pop() ~ ' ' };
%builtins{'.s'}   = { print '<' ~ @stack.elems ~ '> ' ~ @stack.join(' ') ~ ' ' };
%builtins{'cr'}   = { print "\n" };
%builtins{'space'} = { print ' ' };
%builtins{'spaces'} = { print ' ' x pop() };
%builtins{'emit'} = { print chr(pop()) };
%builtins{'bl'}   = { @stack.push(32) };

# ---------- driver -----------------------------------------------------
sub run-source(Str $src) {
    my @toks = tokenize($src);
    my $ip = 0;
    exec(parse-block(@toks, $ip, ()));
}

sub repl() {
    say "rakupp-forth — type words, or `bye` to quit. Try:  1 2 + .";
    while (my $line = prompt("")).defined {
        last if $line.trim eq 'bye';
        my $r = try run-source($line);
        if $! { note "  ? {$!.message}"; @stack = () }
        else  { say " ok" }
    }
    say "";
}

sub MAIN($file?) {
    if $file.defined {
        run-source($file.IO.slurp);
    }
    else {
        repl();
    }
}
