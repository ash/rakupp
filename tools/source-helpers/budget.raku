#!/usr/bin/env rakupp
# A size budget for the sources, so the build cannot quietly get slow again.
#
#   rakupp tools/source-helpers/budget.raku            # check; exit 1 when over
#   rakupp tools/source-helpers/budget.raku --record   # write the ceilings file
#
# A full build takes as long as its slowest file, and a file is slow because it
# is big, or because the headers it includes are. Before the 2026-09 split,
# Interpreter.cpp was 59,375 lines and took 61 s alone, so no number of cores
# could build rakupp faster than that. Nothing had said so while it grew. This
# says so. Three measures, each an exact count, so the check never depends on
# the machine or its load:
#
#   file          lines of each hand-written src/ .cpp and .h
#   function      lines of each function defined at column 0 (a member defined
#                 inside a class body is not seen, nor is a lambda's own size —
#                 it counts toward the function it is written in)
#   preprocessed  lines a header expands to under `c++ -E`: what every file
#                 including it pays. A heavy new #include moves this number
#                 and no other. Skipped, and said so, without a C++ compiler.
#
# budget.txt beside this file holds a default ceiling for files and functions,
# and a named ceiling for each one that was already bigger when it was
# recorded. A named ceiling is the size at recording plus a little headroom
# (3% for files and functions, 2% for headers), so ordinary edits pass and
# steady growth does not. --record rewrites the file from the tree as it is:
# run it after a split, to lower the ceilings, or when a file is allowed to
# grow — and say which in the commit. A ceiling well above its file's size
# is reported, so the budget tightens as files shrink.
#
# Generated sources (the Unicode tables, the JS runtime blob) are skipped:
# nobody edits them, and their compile time is not a budget anyone can spend.

my constant $HEADROOM-LINES = 1.03;
my constant $HEADROOM-PP    = 1.02;
my constant @PP-HEADERS = <src/Interpreter.h src/InterpreterParts.h src/BuiltinsParts.h>;

sub generated(IO::Path $f --> Bool) {
    so $f.basename ~~ /^ ['unicode_' .* | 'JsRuntimeSrc.cpp'] $/
}

sub sources(IO::Path $root) {
    my @todo = $root.add('src');
    my @out;
    while @todo {
        my $d = @todo.shift;
        for $d.dir.sort -> $e {
            if $e.d { @todo.push: $e unless $e.basename eq 'js-rt' }
            elsif $e.extension eq 'cpp' | 'h' && !generated($e) { @out.push: $e }
        }
    }
    @out
}

# Functions defined at column 0: a head line that starts in column 0 and has a
# `(` (or is a lambda bound at namespace scope), up to the `{` that ends it,
# then everything to the next line starting with `}` in column 0.
sub functions(IO::Path $f) {
    my @l = $f.lines;
    my @found;
    my $i = 0;
    while $i < @l {
        my $line = @l[$i];
        my $c = $line.substr(0, 1);
        if $c eq '' || $c eq ' ' || $c eq "\t" || $c eq '#' || $c eq '/' || $c eq '}' || $c eq '{'
           || !$line.contains('(') || $line.starts-with('namespace') {
            $i++;
            next;
        }
        # the head: up to a line ending in `{` (a definition) or `;` (a declaration)
        my $j = $i;
        my $head = $line;
        while $j < @l.end && $head !~~ / ['{' | ';'] \s* ['//' .*]? $ / {
            $j++;
            $head ~= ' ' ~ @l[$j];
        }
        if $head ~~ / '{' \s* ['//' .*]? $ / && $head.index('{') < ($head.index(';') // Inf) {
            my $k = $j + 1;
            $k++ while $k < @l.end && !@l[$k].starts-with('}');
            my $name = $head ~~ / (<[\w:~]>+) \s* '=' \s* '[' / ?? ~$0
                    !! $head ~~ / ('operator' \s* <-[\s(]>+ | <[\w:~]>+) \s* '(' / ?? ~$0
                    !! '?';
            @found.push: $name => $k - $i + 1;
            $i = $k + 1;
        }
        else {
            $i = $j + 1;
        }
    }
    @found
}

sub preprocessed(IO::Path $root, Str $header) {
    my $cxx = %*ENV<CXX> // 'c++';
    my $p = run $cxx, '-std=c++17', '-x', 'c++', '-E',
                '-I' ~ $root.add('src'), '-I' ~ $root.add('include'), '-I' ~ $root.add('include/rakupp'),
                $root.add($header).Str, :out, :err;
    my $out = $p.out.slurp(:close);
    $p.err.slurp(:close);
    return Nil unless $p.exitcode == 0;
    $out.lines.elems
}

sub compiler-id() {
    my $p = run(%*ENV<CXX> // 'c++', '--version', :out, :err);
    my $id = $p.out.slurp(:close).lines.head // '';
    $p.err.slurp(:close);
    $p.exitcode == 0 ?? $id.trim !! ''
}

sub read-budget(IO::Path $file) {
    my %b = default => {}, file => {}, function => {}, preprocessed => {};
    for $file.lines -> $l {
        next if $l ~~ /^ \s* ['#' | $]/;
        if $l ~~ /^ compiler \s+ (.+) $/ { %b<compiler> = ~$0.trim; next }
        my @w = $l.words;
        die "budget.txt: cannot read '$l'" unless @w == 3 && @w[2] ~~ /^ \d+ $/;
        %b{@w[0]}{@w[1]} = +@w[2];
    }
    %b
}

sub MAIN(Bool :$record = False, Str :$root = '.', Str :$budget) {
    my $r = $root.IO;
    my $budget-file = $budget ?? $budget.IO !! $*PROGRAM.parent.add('budget.txt');
    my @files = sources($r);
    my %size = @files.map({ .relative($r) => .lines.elems });
    my @funcs = @files.map({ |functions($_).map(-> $p { $p.key => ($p.value, $_.relative($r)) }) });
    my %pp;
    for @PP-HEADERS -> $h { %pp{$h} = preprocessed($r, $h) if $r.add($h).e }

    if $record {
        my %old = $budget-file.e ?? read-budget($budget-file) !! (default => { file => 10000, function => 1500 });
        my $df = %old<default><file> // 10000;
        my $dfn = %old<default><function> // 1500;
        # keep the opening comment; everything after it is rewritten
        my @out;
        if $budget-file.e { for $budget-file.lines { last unless .starts-with('#'); @out.push: $_ } }
        @out.push: '' if @out;
        @out.push: "default file $df", "default function $dfn", '';
        @out.push: "file $_ {(%size{$_} * $HEADROOM-LINES).ceiling}" for %size.keys.grep({ %size{$_} > $df }).sort;
        @out.push: '';
        my %fmax;
        for @funcs -> $p { %fmax{$p.key} = max(%fmax{$p.key} // 0, $p.value[0]) }
        @out.push: "function $_ {(%fmax{$_} * $HEADROOM-LINES).ceiling}" for %fmax.keys.grep({ %fmax{$_} > $dfn }).sort;
        @out.push: '';
        my $cid = compiler-id();
        @out.push: "compiler $cid" if $cid;
        for %pp.keys.sort -> $h {
            if %pp{$h}.defined { @out.push: "preprocessed $h {(%pp{$h} * $HEADROOM-PP).ceiling}" }
            else { note "budget: `c++ -E $h` failed; its ceiling is not recorded" }
        }
        spurt $budget-file, @out.join("\n") ~ "\n";
        say "budget: recorded {+%size.keys.grep({ %size{$_} > $df })} files, "
          ~ "{+%fmax.keys.grep({ %fmax{$_} > $dfn })} functions, {+%pp.values.grep(*.defined)} headers to {$budget-file.relative($r)}";
        return;
    }

    die "budget: no {$budget-file} — run with --record first" unless $budget-file.e;
    my %b = read-budget($budget-file);
    my ($df, $dfn) = %b<default><file>, %b<default><function>;
    my @over;
    my @slack;
    for %size.keys.sort -> $f {
        my $cap = %b<file>{$f} // $df;
        if %size{$f} > $cap {
            @over.push: "file $f is {%size{$f}} lines, over its ceiling of $cap"
                      ~ (%b<file>{$f} ?? '' !! ' (the default)');
        }
        elsif %b<file>{$f} && %size{$f} < 0.9 * $cap {
            @slack.push: "file $f is {%size{$f}} lines against a ceiling of $cap";
        }
    }
    my %seen;
    for @funcs.sort({ -.value[0] }) -> $p {
        my ($name, $n, $where) = $p.key, |$p.value;
        next if %seen{$name}++;
        my $cap = %b<function>{$name} // $dfn;
        if $n > $cap {
            @over.push: "function $name ($where) is $n lines, over its ceiling of $cap"
                      ~ (%b<function>{$name} ?? '' !! ' (the default)');
        }
        elsif %b<function>{$name} && $n < 0.9 * $cap {
            @slack.push: "function $name is $n lines against a ceiling of $cap";
        }
    }
    for %b<file>.keys.grep({ !(%size{$_}:exists) }).sort { @slack.push: "file $_ has a ceiling but no longer exists" }
    for %b<function>.keys.grep({ !%seen{$_} }).sort { @slack.push: "function $_ has a ceiling but was not found" }
    # a header's expansion depends on the compiler and its standard library, so
    # its ceilings are judged only under the compiler that recorded them
    my $cid = compiler-id();
    if %b<compiler> && $cid ne %b<compiler> {
        say "budget: skipped the header sizes: they were recorded with `{%b<compiler>}`, "
          ~ "this is `{$cid || 'no compiler'}` (record again to judge them here)";
        %pp = ();
    }
    for %pp.keys.sort -> $h {
        with %pp{$h} -> $n {
            my $cap = %b<preprocessed>{$h};
            if !$cap { @slack.push: "header $h expands to $n lines and has no ceiling" }
            elsif $n > $cap { @over.push: "header $h expands to $n lines under c++ -E, over its ceiling of $cap" }
        }
        else { say "budget: skipped the preprocessed size of $h (no working `c++ -E`)" }
    }
    say "budget: {+%size} files, {+@funcs} functions, {+%pp.values.grep(*.defined)} headers checked";
    say "  over: $_" for @over;
    say "  note: $_ — record to tighten" for @slack;
    if @over {
        say "budget: {+@over} over. Split what grew (tools/source-helpers/ has the scripts that split "
          ~ "Interpreter.cpp and Builtins.cpp), or raise the ceiling with --record and say why in the commit.";
        exit 1;
    }
    say 'budget: OK';
}
