#!/usr/bin/env rakupp
# dep-leverage.raku — for every distribution, how many NOT-YET-GREEN dists sit
# transitively downstream of it. That is the upper bound on what fixing it
# could unlock, and it is the order the 1000 chase should be worked in.
#
#   rakupp dep-leverage.raku --rea=rea-meta.json --sweep=sweep.tsv [--top=40]
#
# Dependencies name MODULES; verdicts are per DIST. The `provides` map of each
# dist is what joins them, so a dep on Foo::Bar resolves to whichever dist
# ships that module — without it every multi-module dist looks like a leaf.

my $have-own-json = ?(try { ::('Rakupp::Internals::JSON').from-json('1') === 1 });
sub json-decode(Str $text) {
    $have-own-json
        ?? ::('Rakupp::Internals::JSON').from-json($text)
        !! ::('Rakudo::Internals::JSON').from-json($text)
}

sub field($line, $key) {
    $line ~~ /'"' $key '":"' (<-["]>+) '"'/ ?? ~$0 !! ''
}

# A depends value is an array of strings or hashes, or a phase hash. The phase
# value is itself either a list or `{requires => [...]}` — the shape the repo's
# two rankers both miss, and 94 dists are invisible without it.
sub deps-of($v, %into) {
    return unless $v.defined;
    if $v ~~ Positional { deps-of($_, %into) for @$v }
    elsif $v ~~ Associative {
        if $v<name>.defined { one(~$v<name>, %into) }
        else {
            for <runtime build test> -> $phase {
                next unless $v{$phase}.defined;
                my $p = $v{$phase};
                deps-of($p ~~ Associative && $p<requires>.defined ?? $p<requires> !! $p, %into);
            }
            deps-of($v<requires>, %into) if $v<requires>.defined;
        }
    }
    elsif $v ~~ Str { one($v, %into) }
}

sub one(Str $s, %into) {
    return if $s ~~ /':from<' <-[>]>+ '>'/ && $s !~~ /':from<raku>'/;
    my $name = $s ~~ /^ ( [ <-[:\s]>+ | '::' ]+ ) / ?? ~$0 !! '';
    return unless $name.chars;
    return if $name eq 'Rakudo' || $name eq 'perl6' || $name eq 'nqp';
    %into{$name} = True;
}

sub MAIN(:$rea!, :$sweep!, Int :$top = 40, :$union) {
    my %verdict;
    for $sweep.IO.lines.skip(1) -> $l {
        my @f = $l.split("\t");
        %verdict{@f[0]} = @f[3] if @f[3];
    }

    my %latest;
    for $rea.IO.lines -> $line {
        next unless $line.starts-with('{');
        my $date = field($line, 'release-date');
        my $dist = field($line, 'dist');
        my $name = $dist ~~ /^ (.+?) ':ver<'/ ?? ~$0 !! field($line, 'name');
        next unless $name && $date;
        %latest{$name} = { :$date, :$line }
            if !%latest{$name} || $date gt %latest{$name}<date>;
    }
    note "{%latest.elems} dists in the index";

    my %provides;
    my %needs;
    for %latest.kv -> $name, $rec {
        my $j = try json-decode($rec<line>.chomp(','));
        next unless $j;
        %provides{$name} = $name;
        if $j<provides> ~~ Associative {
            %provides{$_} //= $name for $j<provides>.keys;
        }
        my %d;
        deps-of($j<depends>,       %d);
        deps-of($j<test-depends>,  %d);
        deps-of($j<build-depends>, %d);
        %needs{$name} = %d.keys.Set;
    }

    my %edge;
    for %needs.kv -> $d, $mods {
        my %seen;
        for $mods.keys -> $m {
            my $owner = %provides{$m} // next;
            next if $owner eq $d;
            %seen{$owner} = True;
        }
        %edge{$d} = %seen.keys;
    }

    my %closure;
    sub upstream($d, %active) {
        return %closure{$d} if %closure{$d}:exists;
        return Set.new if %active{$d};
        %active{$d} = True;
        my %all;
        for @(%edge{$d} // []) -> $u {
            %all{$u} = True;
            %all{$_} = True for upstream($u, %active).keys;
        }
        %active{$d}:delete;
        %closure{$d} = %all.keys.Set;
    }
    for %edge.keys -> $d { my %a; upstream($d, %a) }

    my %unlocks;
    for %closure.kv -> $d, $up {
        next if (%verdict{$d} // 'unknown') eq 'pass';
        %unlocks{$_}++ for $up.keys;
    }

    if $union {
        my $want = $union.split(',').map(*.trim).Set;
        my @hit = %closure.keys.grep({
            (%verdict{$_} // 'unknown') ne 'pass' && %closure{$_}.keys.any (elem) $want
        }).sort;
        say "union of { $want.keys.sort.join(', ') }";
        say "distinct not-green dists downstream: {+@hit}";
        say @hit.join("\n");
        return;
    }
    say "dist\tverdict\tblocked-downstream";
    my @board = %unlocks.pairs.grep({ .key.chars && (%verdict{.key} // 'unknown') ne 'pass' })
                              .sort({ $^b.value <=> $^a.value });
    my $n = min($top, +@board);
    for @board[^$n] -> $p {
        say "{$p.key}\t{%verdict{$p.key} // 'unknown'}\t{$p.value}";
    }
}
