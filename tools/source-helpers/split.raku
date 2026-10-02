#!/usr/bin/env rakupp
# Split one big C++ translation unit into several, mechanically.
#
#   rakupp tools/source-helpers/split.raku [--dry] [--note-src=NAME] SRC PLAN OUTDIR
#
# Run it from the repository root: a function a header in src/ already declares
# needs no prototype from it. OUTDIR gets the new header and one .cpp per part;
# copy them over src/ when they build. --dry prints the plan's outcome and writes
# nothing. interpreter.plan and builtins.plan beside this file are the plans that
# split Interpreter.cpp and Builtins.cpp (2026-09); INTERNALS.md in docs/internals
# says why the parts are the parts.
#
# PLAN lines:
#   header NAME.h "one-line description"
#   part FILE.cpp FIRST-LINE "one-line description"
#                     the part takes the items from FIRST-LINE (1-based) to the
#                     next part's; the first part starts at 1. The same FILE
#                     given again adds another range; FIRST-LINE 0 = no range
#   hot FILE.cpp PROFILE SELF% INCL%
#                     every function PROFILE (see hot-profile.raku) lists with
#                     at least SELF% self or INCL% inclusive time goes to FILE,
#                     wherever it sits: the hot paths stay in ONE file, so they
#                     keep inlining into each other (PROFILE is relative to PLAN)
#   pin FILE.cpp TEXT the item whose first line of code contains TEXT goes to FILE
#   follow            a static helper, variable or type that exactly one OTHER
#                     part uses moves into that part
#   inline-max N      a shared static function of at most N lines is defined in
#                     the header (static inline) rather than exported
#
# Everything above the last top-level `namespace rakupp {` (the includes) goes
# into the header. A top-level name defined in one part and used in another is
# SHARED:
#   types, aliases, templates, #defines, small functions, trivial constants
#       -> moved into the header whole
#   other functions   -> a prototype in the header; the definition loses `static`
#   other variables   -> `extern` in the header; the definition loses `static`.
#                        A scalar or pointer thread_local with a constant
#                        initializer is declared RAKUPP_CONSTINIT, so another
#                        file reads it directly instead of through a wrapper call
# The header keeps the original file order, so it declares nothing before what
# it depends on — the original file compiled in that order. An anonymous
# namespace moves whole, or not at all when it holds a variable.
use lib $*PROGRAM.parent.Str;
use Scan;

sub MAIN(Str $src-file, Str $plan-file, Str $out-dir, Bool :$dry = False, Str :$note-src = '') {
    my $src = $src-file.IO.slurp;
    my $m = mask($src);
    my @items = scan($src, :masked($m));
    my @sl = $src.split("\n"); @sl.pop if @sl[*-1] eq '';
    my @ml = $m.split("\n");   @ml.pop if @ml[*-1] eq '';

    # ---- the plan
    my ($hname, $hdesc, $inline-max) = '', '', 12;
    my @parts;
    my @hot;            # functions a profile says are hot go to one part
    my @extra-starts;  # line => part index, for a file given more than one range
    my @pins;           # an item whose first code line contains TEXT goes to a part
    my $follow = False; # a static helper that only one other part uses moves there
    for $plan-file.IO.lines -> $l {
        next if $l ~~ /^ \s* ['#' | $]/;
        if $l ~~ /^ header \s+ (\S+) \s+ '"' (<-["]>*) '"'/ { ($hname, $hdesc) = ~$0, ~$1 }
        elsif $l ~~ /^ part \s+ (\S+) \s+ (\d+) \s* ['"' (<-["]>*) '"']?/ {
            my ($f, $first, $desc) = ~$0, +$1, ($2 ?? ~$2 !! '');
            with @parts.first(*.<file> eq $f, :k) -> $k { @extra-starts.push: $first => $k }   # another range of the same file
            else { @parts.push: %(file => $f, :$first, :$desc) }
        }
        elsif $l ~~ /^ 'inline-max' \s+ (\d+)/ { $inline-max = +$0 }
        elsif $l ~~ /^ hot \s+ (\S+) \s+ (\S+) \s+ (<[\d.]>+) \s+ (<[\d.]>+)/ { @hot.push: %(file => ~$0, prof => ~$1, self => +$2, incl => +$3) }
        elsif $l ~~ /^ pin \s+ (\S+) \s+ (.+?) \s* $/ { @pins.push: %(file => ~$0, text => ~$1) }
        elsif $l ~~ /^ follow \s* $/ { $follow = True }
        else { die "bad plan line: $l" }
    }
    die "the first part must start at line 1" unless @parts && @parts[0]<first> == 1;

    my $ns-open = @items.first({ .kind eq 'ns-open' && .container == -1 }, :k, :end);   # the last one: the main block
    my $ns-close = @items.first({ .kind eq 'ns-close' && .container == -1 }, :k, :end);
    die "no namespace rakupp block" unless $ns-open.defined && $ns-close.defined;
    die "items after the namespace: {$ns-close + 1}..{@items.end}" if $ns-close < @items.end;

    # ---- which part each item lives in
    my %starts = @parts.kv.grep(-> $i, %p { %p<first> > 0 }).map(-> ($i, %p) { %p<first> => $i });
    %starts{.key} = .value for @extra-starts;
    for @extra-starts -> $p { die "a range of @parts[$p.value]<file> starts at line {$p.key}, which is not where an item starts" unless @items.first({ .l1 + 1 == $p.key }) }
    my %part-index = @parts.kv.map(-> $i, %p { %p<file> => $i });
    my @part-of;
    my $cur = 0;
    for @items.kv -> $i, $it {
        if $i > $ns-open && (%starts{$it.l1 + 1}:exists) {
            die "part {@parts[%starts{$it.l1 + 1}]<file>} starts inside an anonymous namespace (line {$it.l1 + 1})"
                unless $it.container == $ns-open;
            $cur = %starts{$it.l1 + 1};
        }
        @part-of[$i] = $cur;
    }
    for @parts.kv -> $i, %p {
        next if $i == 0 || %p<first> == 0;
        next if @items.grep({ .l1 + 1 == %p<first> });
        my $near = @items.grep({ .container == $ns-open }).min({ abs(.l1 + 1 - %p<first>) });
        die "part %p<file> starts at line %p<first>, which is not where an item starts; nearest is line {$near.l1 + 1} ({$near.kind} {$near.name})";
    }

    # ---- names: who defines them, who uses them
    my %defs;       # name -> item indices
    for $ns-open ^..^ $ns-close -> $i {
        my $it = @items[$i];
        next unless $it.kind eq any <func fdecl var type template alias> and $it.name and !$it.name.contains('::');
        %defs{$it.name}.push: $i;
    }
    # a #define shares like anything else
    for $ns-open ^..^ $ns-close -> $i {
        my $it = @items[$i];
        next unless $it.kind eq 'pp' && @sl[$it.code] ~~ /^ \h* '#' \h* define \h+ (\w+)/;
        $it.name = ~$0;
        %defs{$it.name}.push: $i;
    }
    # names an #if group defines (a rough read: definitions start in column 0)
    my %group-defs;
    for $ns-open ^..^ $ns-close -> $i {
        my $it = @items[$i];
        next unless $it.kind eq 'ppgroup';
        # only what has internal linkage or is a type: a non-static function
        # defined in a group links from anywhere, through a declaration outside it
        for @ml[$it.l1 .. $it.l2] -> $l {
            next if $l ~~ /^ ['#' | \s | '}' | '{' | $ | namespace | 'static_assert']/;
            my $name;
            if $l ~~ /^ [struct|class|union|enum] \s+ (\w+)/ { $name = ~$0 }
            elsif $l ~~ /^ static <|w>/ && $l ~~ / (<[\w:]>+) \s* ['(' | '=' | ';' | '{' | '[']/ { $name = ~$0 }
            next unless $name && !$name.contains('::');
            %group-defs{$name}.push: $i unless %defs{$name}:exists;
        }
    }

    # ---- hot functions and pinned items override the line ranges
    my &block-of = -> $i {       # an anonymous namespace moves whole: its open..close
        my $it = @items[$i];
        if $it.container != $ns-open {
            my $open = $it.container;
            my $close = ($open ^.. @items.end).first({ @items[$_].kind eq 'ns-close' && @items[$_].container == $ns-open });
            ($open .. $close).List
        }
        else { ($i,) }
    };
    my &move = -> $i, $p { @part-of[$_] = $p for block-of($i) };
    for @hot -> %h {
        my $p = %part-index{%h<file>} // die "hot: no part %h<file>";
        my %want;
        my $prof = %h<prof>.IO.is-absolute ?? %h<prof>.IO !! $plan-file.IO.parent.add(%h<prof>);   # relative to the plan
        for $prof.lines -> $l {
            my @w = $l.words;
            %want{@w[2]} = True if @w[0].chop >= %h<self> || @w[1].chop >= %h<incl>;
        }
        my $n = 0;
        for $ns-open ^..^ $ns-close -> $i {
            my $it = @items[$i];
            next unless $it.kind eq 'func' && %want{$it.name};
            move($i, $p); $n++;
        }
        say "hot: $n functions to %h<file>";
    }
    for @pins -> %pn {
        my $p = %part-index{%pn<file>} // die "pin: no part %pn<file>";
        my @hit = ($ns-open ^..^ $ns-close).grep({ @sl[@items[$_].code].contains(%pn<text>) });
        die "pin: no item's first line contains %pn<text>" unless @hit;
        move($_, $p) for @hit;
    }
    # ---- a static helper, variable or type that exactly one OTHER part uses moves there
    if $follow {
        my %users;      # ident -> items that mention it
        for $ns-open ^..^ $ns-close -> $i { %users{$_}.push: $i for @items[$i].idents }
        my $moved = 0;
        for ^20 {
            my $changed = False;
            for %defs.kv -> $name, @is {
                my @its = @is.map({ @items[$_] });
                next if @its.grep({ .kind eq 'func' && !.static && .container == $ns-open });   # external: stays
                next if @its.grep({ .kind eq 'var' && !.static && .container == $ns-open });
                my %home = @is.map({ @part-of[$_] => True });
                next if %home > 1;
                next if @is.map({ |@items[$_].idents }).grep({ %group-defs{$_}:exists });   # tied to an #if group: stays by it
                my %own = @is.flatmap({ block-of($_) }).map({ $_ => True });
                my %by = (%users{$name} // ()).grep({ !%own{$_} }).map({ @part-of[$_] => True });
                next unless %by == 1;
                my $p = %by.keys[0].Int;
                next if %home{$p};
                move($_, $p) for @is;
                $moved++; $changed = True;
            }
            last unless $changed;
        }
        say "follow: $moved names moved to their only user";
    }

    my @uses = @parts.map({ SetHash.new });
    for $ns-open ^..^ $ns-close -> $i { @uses[@part-of[$i]]{$_} = True for @items[$i].idents }

    my %shared;     # name -> True
    for %defs.kv -> $name, @is {
        my %home = @is.map({ @part-of[$_] => True });
        %shared{$name} = True if %home > 1 || @parts.keys.grep({ !%home{$_} && @uses[$_]{$name} });
    }
    # names a public header already declares need no prototype from us
    my %declared;
    # (not in comments, and never in the header this tool writes)
    for dir('src').grep({ .extension eq 'h' && .basename ne $hname }) -> $h {
        %declared{$_} = True for mask($h.slurp).comb(/<[A..Za..z_]>\w*/);
    }
    my @problems;
    for %group-defs.kv -> $name, @is {
        my %home = @is.map({ @part-of[$_] => True });
        push @problems, "#if group at line {@items[@is[0]].line1} defines $name, used in another part"
            if !%declared{$name} && @parts.keys.grep({ !%home{$_} && @uses[$_]{$name} });
    }


    # ---- decide how each shared item travels
    my %how;        # item index -> full | inline | proto | extern | fdecl
    my @todo = %shared.keys;
    my %seen;
    my &share = -> $name { unless %seen{$name}++ { %shared{$name} = True; @todo.push: $name } };
    %seen{$_} = True for @todo;
    my &body-has-static = -> $it { so @ml[$it.code ^.. $it.l2].join("\n") ~~ /<|w> static <|w>/ };
    my &trivial-type = -> $it { $it.head.substr(0, $it.head.index($it.name) // 0) !~~ /'std::' | Value | '<' | '[' /
                                   && $it.head !~~ / '[' / };
    while @todo {
        my $name = @todo.shift;
        for %defs{$name}.list -> $i {
            my $it = @items[$i];
            if $it.container != $ns-open {
                # an anonymous namespace travels whole, as long as moving it cannot
                # give each file its own copy of a variable
                my $open = $it.container;
                next if %how{$open};
                my $close = ($open ^.. @items.end).first({ @items[$_].kind eq 'ns-close' && @items[$_].container == $ns-open });
                my @in = $open ^..^ $close;
                if @in.grep({ @items[$_].kind eq 'var' }) -> @v {
                    die "$name (line {$it.line1}) is used from another part, but its anonymous namespace also holds "
                      ~ "the variable {@items[@v[0]].name}";
                }
                %how{$_} = 'full' for $open, |@in, $close;
                %how{$_} = 'inline' for @in.grep({ @items[$_].kind eq 'func' && !@items[$_].inline });
                for @in -> $j { for @items[$j].idents -> $n { share($n) if %defs{$n}:exists && @items[%defs{$n}[0]].container != $open } }
                next;
            }
            my $how = do given $it.kind {
                when 'type' | 'alias' | 'template' | 'pp' { 'full' }
                when 'fdecl' { $it.static ?? 'fdecl' !! (%declared{$name} ?? 'keep' !! 'fdecl') }
                when 'func' {
                    if $it.inline { 'full' }
                    elsif !$it.static { %declared{$name} ?? 'keep' !! 'proto' }
                    elsif $it.l2 - $it.code + 1 <= $inline-max && !body-has-static($it) { 'inline' }
                    else { 'proto' }
                }
                when 'var' {
                    if !$it.static && %declared{$name} { 'keep' }
                    elsif $it.head ~~ /<|w> constexpr <|w>/ || $it.const && trivial-type($it) && $it.lines <= 20 { 'full' }
                    else { 'extern' }
                }
            };
            %how{$i} = $how;
            # what the header text now needs, declared before it
            my @need = $how eq 'full' | 'inline'
                ?? $it.idents
                !! $it.head.comb(/<[A..Za..z_]>\w*/);
            for @need -> $n { share($n) if %defs{$n}:exists && $n ne $name }
            for @need -> $n {
                push @problems, "$name (line {$it.line1}) needs $n from an #if group" if %group-defs{$n}:exists && !%declared{$n} && $how ne q[keep];
            }
        }
    }

    # a forward declaration keeps its `static` when the definition itself moved
    # into the header (still internal, now in every file); it loses it only
    # when the definition is exported
    for %how.kv -> $i, $how {
        next unless $how eq 'fdecl';
        my $it = @items[$i];
        my @d = %defs{$it.name}.grep({ @items[$_].kind eq 'func' });
        %how{$i} = 'full' if @d && @d.grep({ (%how{$_} // q[]) eq q[inline] || (%how{$_} // q[]) eq q[full] }) == @d;
    }

    # ---- report
    my %count;
    %count{$_}++ for %how.values;
    say "items: {+@items}; shared names: {+%shared}; " ~ %count.sort.map({ "{.key} {.value}" }).join(', ');
    for @parts.kv -> $p, %pt {
        my @mine = ($ns-open ^..^ $ns-close).grep({ @part-of[$_] == $p });
        my $lines = [+] @mine.map({ @items[$_].lines });
        say sprintf "  %-28s from line %6d  %6d lines  %4d items", %pt<file>, %pt<first>, $lines, +@mine;
    }
    my @tls = %how.grep({ .value eq 'extern' && @items[.key].head ~~ /thread_local/ }).map({ @items[.key].name });
    say "exported thread_locals: {@tls.sort.join(' ')}" if @tls;
    say "inline in the header: {%how.grep(*.value eq 'inline').map({ @items[.key].name }).sort.join(' ')}";
    if @problems { note "PROBLEM: $_" for @problems; exit 1 }
    return if $dry;

    # ---- emit
    my $out = $out-dir.IO; $out.mkdir;
    my &text = -> $it { @sl[$it.l1 .. $it.l2].join("\n") };
    my $origin = $note-src || $src-file.IO.basename;

    # the header
    my @h;
    @h.push: "// $hname — $hdesc";
    @h.push: "//";
    @h.push: "// What the parts of the interpreter share: every type, helper and variable";
    @h.push: "// that one {$origin}-family file defines and another one uses. The parts:";
    @h.push: "//   {.<file>.fmt('%-26s')} {.<desc>}" for @parts;
    @h.push: "#pragma once";
    for ^$ns-open -> $i { @h.push: inline-statics(text(@items[$i])) }
    @h.push: text(@items[$ns-open]);
    for $ns-open ^..^ $ns-close -> $i {
        my $it = @items[$i];
        @h.push: "// ITEM $i {$it.kind} {$it.name} {%how{$i}}" if %*ENV<SPLIT_DEBUG> && %how{$i};
        given %how{$i} // '' {
            when 'full'   { @h.push: text($it) }
            when 'inline' { @h.push: make-inline($it, @sl, @ml) }
            when 'fdecl'  { @h.push: drop-static($it, @sl, @ml, :decl) }
            when 'proto'  { @h.push: leading(@sl, $it) ~ prototype($it, @sl, @ml) }
            when 'extern' { @h.push: leading(@sl, $it) ~ extern-decl($it, @sl, @ml) }
        }
    }
    @h.push: text(@items[$ns-close]);
    spurt $out.add($hname), @h.join("\n") ~ "\n";

    for @parts.kv -> $p, %pt {
        my @o;
        @o.push: "// %pt<file> — %pt<desc>";
        @o.push: "//";
        @o.push: "// One of the parts $hname lists; what they share is declared there.";
        @o.push: "#include \"$hname\"";
        @o.push: '';
        @o.push: text(@items[$ns-open]).trim-leading;
        for $ns-open ^..^ $ns-close -> $i {
            next unless @part-of[$i] == $p;
            my $it = @items[$i];
            given %how{$i} // '' {
                when 'full' | 'inline' | 'fdecl' { }
                when 'proto'  { @o.push: definition($it, @sl, @ml) }
                when 'extern' { @o.push: var-definition($it, @sl, @ml) }
                default       { @o.push: text($it) }
            }
        }
        @o.push: text(@items[$ns-close]);
        spurt $out.add(%pt<file>), @o.join("\n") ~ "\n";
    }
    say "wrote $hname and {+@parts} parts to $out";
}

# ---- text surgery. Positions come from the masked lines, which line up with
# the real ones character for character.

sub leading(@sl, $it) { $it.code > $it.l1 ?? @sl[$it.l1 ..^ $it.code].join("\n") ~ "\n" !! '' }

# the head (code line .. the body's `{`, exclusive) as [real, masked] joined text
sub head-span($it, @sl, @ml) {
    my $last = $it.brace ?? $it.brace[0] !! $it.l2;
    my @r = @sl[$it.code .. $last];
    my @k = @ml[$it.code .. $last];
    my $cut = $it.brace ?? $it.brace[1] !! @k[*-1].chars;
    my $tail = @r[*-1].substr($cut);
    @r[*-1] = @r[*-1].substr(0, $cut);
    @k[*-1] = @k[*-1].substr(0, $cut);
    (@r.join("\n"), @k.join("\n"), $tail, $last)
}

# where the declarator's parameter list opens and closes, in masked text
sub param-span(Str $k) {
    my constant %NOT = set <__attribute__ alignas decltype noexcept sizeof alignof __declspec>;
    for $k.indices('(') -> $p {
        next if angle-depth($k, $p) > 0;
        next unless $k.substr(0, $p) ~~ / ( [ 'operator' \s* <-[\s(]>+ | <[\w:~]>+ ] ) \s* $/;
        next if ~$0 (elem) %NOT;
        my $d = 0;
        for $k.substr($p).comb.kv -> $j, $c {
            $d++ if $c eq '(' | '[' | '{';
            $d-- if $c eq ')' | ']' | '}';
            return ($p, $p + $j) if $d == 0;
        }
    }
    Nil
}

# drop every `= default` from a parameter list
sub strip-defaults(Str $r, Str $k) {
    my $span = param-span($k) or return $r;
    my ($open, $close) = @$span;
    my @cut;
    my $d = 0; my $eq;
    my @c = $k.comb;
    for ($open + 1) .. $close -> $j {
        my $c = @c[$j];
        if $j == $close || ($c eq ',' && $d == 0) {
            if $eq.defined { my $from = $eq; $from-- while $from > 0 && @c[$from - 1] eq ' '; @cut.push: ($from, $j) }
            $eq = Nil;
            next;
        }
        if $c eq '(' | '[' | '{' | '<' { $d++ }
        elsif $c eq ')' | ']' | '}' | '>' { $d-- unless $c eq '>' && @c[$j - 1] eq '-' }
        elsif $c eq '=' && $d == 0 && !$eq.defined && @c[$j + 1] ne '=' && @c[$j - 1] ne '=' | '!' | '<' | '>' { $eq = $j }
    }
    my $out = $r;
    for @cut.reverse -> ($a, $b) { $out = $out.substr(0, $a) ~ $out.substr($b) }
    $out
}

# drop the first `static` before the name
sub unstatic(Str $r, Str $k) {
    my $stop = (param-span($k) // [$k.chars])[0];
    my $lead = $k.substr(0, $stop);
    if $lead ~~ /<|w> static \s+/ {
        return $r.substr(0, $/.from) ~ $r.substr($/.to);
    }
    $r
}

sub prototype($it, @sl, @ml) {
    my ($r, $k) = head-span($it, @sl, @ml);
    unstatic($r, $k).trim ~ ';'
}

sub definition($it, @sl, @ml) {
    my ($r, $k, $tail, $last) = head-span($it, @sl, @ml);
    my $new = strip-defaults($r, $k);
    my $knew = strip-defaults($k, $k);
    $new = unstatic($new, $knew);
    (flat leading(@sl, $it).chomp.lines, $new ~ $tail, @sl[$last ^.. $it.l2]).grep(*.defined).join("\n")
        .subst(/^ \n/, '')
}

sub drop-static($it, @sl, @ml, :$decl) {
    my ($r, $k, $tail, $last) = head-span($it, @sl, @ml);
    leading(@sl, $it) ~ unstatic($r, $k) ~ $tail ~ (@sl[$last ^.. $it.l2].map("\n" ~ *).join)
}

sub make-inline($it, @sl, @ml) {
    my ($r, $k, $tail, $last) = head-span($it, @sl, @ml);
    my $stop = (param-span($k) // [$k.chars])[0];
    my $new = $k.substr(0, $stop) ~~ /<|w> static \s+/
        ?? $r.substr(0, $/.to) ~ 'inline ' ~ $r.substr($/.to)
        !! 'static inline ' ~ $r;
    leading(@sl, $it) ~ $new ~ $tail ~ (@sl[$last ^.. $it.l2].map("\n" ~ *).join)
}

# `extern T name[...];` from a definition
sub extern-decl($it, @sl, @ml) {
    my $r = @sl[$it.code .. $it.l2].join("\n");
    my $k = @ml[$it.code .. $it.l2].join("\n");
    my $d = 0; my $cut = $k.chars;
    for $k.comb.kv -> $p, $c {
        if $c eq '(' | '[' | '<' { $d++ } elsif $c eq ')' | ']' | '>' { $d-- }
        elsif ($c eq '=' || $c eq '{' || $c eq ';') && $d <= 0 { $cut = $p; last }
    }
    my $decl = $r.substr(0, $cut).trim;
    $decl ~~ s/<|w> static \s+//;
    $decl ~~ s/<|w> inline \s+//;
    $decl ~~ s/^ extern \s+//;
    # a thread_local of scalar or pointer type with a constant initializer can be
    # read directly from another file, once the declaration says so
    my $init = $k.substr($cut).subst(/';' \s* $/, '').trim;
    my $type = $k.substr(0, $cut).subst(/<|w> $($it.name) \s* ['[' .* ']']? \s* $/, '');
    my $trivial = $type.trim.ends-with('*') || $type !~~ /'std::' | Value | '<' | '::'/;
    my $const-init = $init eq '' || $init ~~ /^ ['=' \s*]? ['{' \s* ]? [nullptr | true | false | '-'? \d+ <[uUlL]>* | '0x' <xdigit>+ | ''] \s* ['}']? $/;
    my $ci = $k.substr(0, $cut) ~~ /<|w> thread_local <|w>/ && $trivial && $const-init ?? 'RAKUPP_CONSTINIT ' !! '';
    "{$ci}extern $decl;"
}

sub var-definition($it, @sl, @ml) {
    my $r = @sl[$it.code .. $it.l2].join("\n");
    my $k = @ml[$it.code .. $it.l2].join("\n");
    my $stop = $k.index($it.name) // $k.chars;
    my $out = $r;
    if $k.substr(0, $stop) ~~ /<|w> static \s+/ {
        $out = $r.substr(0, $/.from) ~ ($it.const ?? 'extern ' !! '') ~ $r.substr($/.to);
    }
    elsif $it.const {
        $out = 'extern ' ~ $r;
    }
    leading(@sl, $it) ~ $out
}

# a static function in a header would warn in every file that does not call it
sub inline-statics(Str $t) {
    $t.lines.map({ $_ ~~ /^ static \s+ <!before inline>/ && .contains('(') && (.contains('{') || !.contains(';')) ?? .subst(/^ static \s+/, 'static inline ') !! $_ }).join("\n")
}
