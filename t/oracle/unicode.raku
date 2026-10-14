# ORACLE-PLAN P3: the Unicode tables against Unicode's own conformance files,
# at the version they are generated from (18.0.0, as in tools/ucd).
#
# src/unicode_*_gen.cpp are generated from the UCD files in tools/ucd. Roast
# tests them too, but at Rakudo's Unicode version, and in samples: its
# collation test is 8,271 of CollationTest's 212,066 lines, and nothing in it
# asks NormalizationTest's Part 1 rule, that every code point the file does
# not list is unchanged by all four forms. This gate asks all of it:
#
#   1. NormalizationTest.txt: every line, all four forms, through Uni and
#      through Str; and every code point outside Part 1 (but the surrogates,
#      which no Str can hold) unchanged by NFC, NFD, NFKC and NFKD;
#   2. GraphemeBreakTest.txt: every line, as `.comb` and `.chars`;
#   3. CollationTest_NON_IGNORABLE.txt: every line `unicmp` the next is Less
#      (as roast asks, lines whose strings are equal once normalized are
#      skipped, since a Str is normalized);
#   4. every code point, for every property the UCD files in tools/ucd
#      define and Raku++ exposes (about ninety: General_Category through
#      Indic_Conjunct_Break, every binary property, the case mappings and
#      foldings, Numeric_Value as an exact Rat, the name `.uniname` gives and
#      `uniparse` back, every NameAliases alias), against what
#      unicode-oracle.py reads from that text with code of its own. It
#      encodes Raku's spellings where Raku has chosen one (roast's
#      "Not_Reordered" and "Yes"/"Maybe"/"No"; "SYRIAC WAW" for Joining_Group;
#      the bare number for the canonical combining classes that have no
#      name) and leaves out the deprecated Expands_On_* properties.
#      Vertical_Orientation and Script_Extensions are not exposed; their
#      files are not vendored either.
#
#   rakupp t/oracle/unicode.raku [--fetch] [--cache=DIR] [--allow-missing]
#                                [--only=norm,gb,coll,props]
#
# About two minutes in all here: the properties take ninety seconds, the
# normalization ten, collation two. Section 4 needs only python3 and the
# repository; the --fetch files are for 1 to 3.
#
# The files are not in the repository. They live in a cache, by default
# $XDG_CACHE_HOME/rakupp/unicode-18.0.0 (~/.cache when unset), pinned by
# SHA-256 in unicode-oracle.py, and only --fetch downloads them (about 8 MB
# from unicode.org). Without them, or without python3 to check them, this
# FAILS — a check that skips looks the same as one that passed — unless
# --allow-missing makes that a skip on purpose.
#
# Contract: exit 0 + last line PASS, as t/regression has it.

my $fetch = False;
my $allow-missing = False;
my $cache = Str;
my %only;
for @*ARGS -> $a {
    if    $a eq '--fetch'               { $fetch = True }
    elsif $a eq '--allow-missing'       { $allow-missing = True }
    elsif $a.starts-with('--cache=')    { $cache = $a.substr(8) }
    elsif $a.starts-with('--only=')     { %only{$_} = True for $a.substr(7).split(',') }
    else { note "unknown option $a"; exit 2 }
}
sub wanted(Str $part) { !%only || %only{$part} }

my $HELPER = $*PROGRAM.parent.add('unicode-oracle.py').absolute;
$cache //= (%*ENV<XDG_CACHE_HOME> || $*HOME.add('.cache').Str).IO.add('rakupp').add('unicode-18.0.0').Str;
my $DIR = $cache.IO;

sub missing(Str $why) {
    if $allow-missing { say "# SKIP: $why"; say 'PASS'; exit 0 }
    note $why;
    say 'FAIL';
    exit 1;
}
if wanted('norm') || wanted('gb') || wanted('coll') || $fetch {
    my $p = run 'python3', '-I', $HELPER, ($fetch ?? 'fetch' !! 'check'), $DIR.Str, :out, :err;
    my $out = $p.out.slurp(:close) ~ $p.err.slurp(:close);
    if $p.exitcode != 0 {
        missing "python3 is needed to check the files, and was not found" unless $out;
        missing "the Unicode 18.0.0 test files are not in $DIR:\n{$out.lines.map('  ' ~ *).join("\n")}\n"
              ~ "  fetch them once (about 8 MB from unicode.org) with\n"
              ~ "    build/rakupp t/oracle/unicode.raku --fetch";
    }
}

my %fails;                                       # family => [messages]
my %count is default(0);
sub fail(Str $family, Str $msg) { %fails{$family}.push: $msg }
sub hexes(@cps) { @cps.map({ .fmt('%04X') }).join(' ') }
sub cps(Str $field) { $field.words.map({ :16($_) }).List }

# ---- 1. normalization ------------------------------------------------------
sub n-uni(@cps, Str $form) {
    given $form {
        when 'NFC'  { Uni.new(|@cps).NFC.list }
        when 'NFD'  { Uni.new(|@cps).NFD.list }
        when 'NFKC' { Uni.new(|@cps).NFKC.list }
        when 'NFKD' { Uni.new(|@cps).NFKD.list }
    }
}
sub n-str(@cps, Str $form) {
    my $s = @cps.chrs;
    given $form {
        when 'NFC'  { $s.NFC.list }
        when 'NFD'  { $s.NFD.list }
        when 'NFKC' { $s.NFKC.list }
        when 'NFKD' { $s.NFKD.list }
    }
}

if wanted('norm') {
    my %part1;
    my $part = '';
    my $line-no = 0;
    # NormalizationTest's own statement of the invariants, by column:
    #   NFC:  c2 == toNFC(c1) == toNFC(c2) == toNFC(c3);  c4 == toNFC(c4) == toNFC(c5)
    #   NFD:  c3 == toNFD(c1) == toNFD(c2) == toNFD(c3);  c5 == toNFD(c4) == toNFD(c5)
    #   NFKC: c4 == toNFKC(c1..c5)
    #   NFKD: c5 == toNFKD(c1..c5)
    my @rules = ('NFC', 2, (1, 2, 3)), ('NFC', 4, (4, 5)), ('NFD', 3, (1, 2, 3)), ('NFD', 5, (4, 5)),
                ('NFKC', 4, (1, 2, 3, 4, 5)), ('NFKD', 5, (1, 2, 3, 4, 5));
    for $DIR.add('NormalizationTest.txt').lines -> $line {
        $line-no++;
        next if $line eq '' || $line.starts-with('#');
        if $line.starts-with('@') { $part = $line.words[0]; next }
        my @c = $line.split(';')[^5].map(&cps);
        %part1{@c[0][0]} = True if $part eq '@Part1';
        for @rules -> ($form, $want, @from) {
            my $w = hexes(@c[$want - 1]);
            for @from -> $i {
                %count<normalization lines>++;
                my $u = hexes(n-uni(@c[$i - 1], $form));
                fail 'normalization lines', "line $line-no: {$form}(c$i) via Uni is $u, want c$want $w" if $u ne $w;
                my $s = hexes(n-str(@c[$i - 1], $form));
                fail 'normalization lines', "line $line-no: {$form}(c$i) via Str is $s, want c$want $w" if $s ne $w;
            }
        }
    }

    # Part 1's rule for everything else: X is its own NFC, NFD, NFKC and NFKD.
    # Asked a block at a time, with "!" between: it composes with nothing,
    # reorders nothing and decomposes to nothing, so a block normalizes to
    # the separators and the code points themselves exactly when each one
    # does. A block that misses is asked again one code point at a time.
    my @rest = (0 .. 0x10FFFF).grep({ !(0xD800 <= $_ <= 0xDFFF) && !%part1{$_} });
    for @rest.batch(4096) -> @block {
        my @with = @block.map({ |($_, 0x21) });
        my $want = hexes(@with);
        for <NFC NFD NFKC NFKD> -> $form {
            %count<code points outside Part 1 unchanged> += @block;
            next if hexes(n-uni(@with, $form)) eq $want;
            for @block -> $cp {
                my $got = hexes(n-uni(($cp,), $form));
                fail 'code points outside Part 1 unchanged', "{$form}(U+{$cp.fmt('%04X')}) is $got"
                    if $got ne $cp.fmt('%04X');
            }
        }
    }
}

# ---- 2. grapheme clusters ---------------------------------------------------
if wanted('gb') {
    my $line-no = 0;
    for $DIR.add('GraphemeBreakTest.txt').lines -> $line {
        $line-no++;
        next if $line eq '' || $line.starts-with('#');
        my $spec = $line.split('#')[0].trim;
        # "÷ 0061 × 0308 ÷ 0062 ÷": ÷ is a break, × none
        my @groups = $spec.split('÷', :skip-empty).map({ .trim }).grep(*.chars)
                          .map({ .split('×').map({ :16(.trim) }).List });
        my @all = @groups.map(|*);
        my $s = @all.chrs;
        my @got = $s.comb;
        %count<grapheme break lines>++;
        my $want-n = +@groups;
        my $ok = @got == $want-n && $s.chars == $want-n
              && (^$want-n).map({ hexes(@got[$_].NFC.list) eq hexes(n-uni(@groups[$_], 'NFC')) }).all;
        fail 'grapheme break lines', "line $line-no: {hexes(@all)}: {@got.map({ '[' ~ hexes(.NFC.list) ~ ']' }).join} "
            ~ "({$s.chars} chars), want {@groups.map({ '[' ~ hexes($_) ~ ']' }).join}" unless $ok;
    }
}

# ---- 3. collation ------------------------------------------------------------
if wanted('coll') {
    my $line-no = 0;
    my ($last, $last-line, $last-cps);
    for $DIR.add('CollationTest_NON_IGNORABLE.txt').lines -> $line {
        $line-no++;
        next if $line eq '' || $line.starts-with('#');
        my @c = cps($line.split(';')[0]);
        next if @c.first({ 0xD800 <= $_ <= 0xDFFF });
        my $s = @c.chrs;
        if $last.defined && $last ne $s {
            %count<collation order>++;
            my $o = $last unicmp $s;
            fail 'collation order', "line $last-line [{hexes($last-cps)}] unicmp line $line-no [{hexes(@c)}] is $o, want Less"
                if $o !== Less;
        }
        ($last, $last-line, $last-cps) = $s, $line-no, @c;
    }
}

# ---- 4. every code point, every property, against the UCD text ---------------
# unicode-oracle.py reads tools/ucd itself — no code shared with the
# generators — and says what each property is for each code point; this asks
# the engine the same. A miss is reported as a run of code points that got
# the same wrong answer.
if wanted('props') {
    my $tsv = $*TMPDIR.add("rakupp-oracle-unicode-props-$*PID.tsv");
    my $ucd = $*PROGRAM.parent.parent.parent.add('tools').add('ucd');
    my $g = run 'python3', '-I', $HELPER, 'props', $ucd.Str, $tsv.Str, :out, :err;
    my $gerr = $g.err.slurp(:close); $g.out.slurp(:close);
    if $g.exitcode != 0 {
        fail 'properties', "unicode-oracle.py props failed: $gerr";
        %count<properties>++;
    }
    else {
        my ($prop, $kind) = '', '';
        my %runs;                                    # property => [[lo, hi, got, want]]
        sub miss(Int $cp, Str $got, Str $want) {
            my $r = %runs{$prop} //= [];
            if $r && $r[*-1][1] == $cp - 1 && $r[*-1][2] eq $got && $r[*-1][3] eq $want { $r[*-1][1] = $cp }
            else { $r.push: [$cp, $cp, $got, $want] }
        }
        sub rat-of(Str $v) { $v.contains('/') ?? $v.split('/')[0].Int / $v.split('/')[1].Int !! $v.Int }
        for $tsv.lines -> $line {
            if $line.starts-with('P ') { ($prop, $kind) = $line.words[1, 2]; next }
            if $line.starts-with('A ') {
                my ($, $code, $alias) = $line.split(' ', 3);
                %count<names and aliases through uniparse>++;
                my $got = (try uniparse($alias)) // '';
                fail 'names and aliases through uniparse', "uniparse('$alias') is [{hexes($got.ords)}], want $code"
                    if $got ne :16($code).chr;
                next;
            }
            # The loops are by kind, with nothing per code point but the
            # question and the comparison: there are 1,114,112 of them for
            # each of about ninety properties.
            my ($l, $h, $want) = $line.split(' ', 3);
            my ($lo, $hi) = :16($l), :16($h);
            %count{"property $prop"} += $hi - $lo + 1;
            if $kind eq 'enum' | 'bool' {
                for $lo .. $hi -> $cp {
                    my $got = ~$cp.uniprop($prop);
                    miss($cp, $got, $want) if $got ne $want;
                }
            }
            elsif $kind eq 'cps' {
                # both sides as Strs, which are NFC alike: `=` is the code
                # point itself, `-` nothing, anything else those code points
                my $w = $want eq '-' ?? '' !! $want eq '=' ?? Str !! cps($want).chrs;
                for $lo .. $hi -> $cp {
                    next if 0xD800 <= $cp <= 0xDFFF;   # no Str holds one
                    my $got = $cp.uniprop($prop);
                    my $ws = $w // $cp.chr;
                    miss($cp, hexes($got.NFC.list), hexes($ws.NFC.list)) if $got ne $ws;
                }
            }
            elsif $kind eq 'rat' {
                my $r = $want eq 'NaN' ?? Rat !! rat-of($want);
                for $lo .. $hi -> $cp {
                    my $got = $cp.unival;
                    my $ok = $r.defined ?? ($got !~~ Num && $got == $r) !! $got.isNaN;
                    miss($cp, $got.raku, $want) unless $ok;
                }
            }
            elsif $kind eq 'name' {
                my $templated = $want.contains('%');
                my $fixed = $want eq '-' ?? '' !! $want;
                for $lo .. $hi -> $cp {
                    my $w = $templated ?? $want.subst('%', $cp.fmt('%04X')) !! $fixed;
                    my $got = $prop eq 'uniname' ?? $cp.uniname !! ~$cp.uniprop($prop);
                    miss($cp, $got, $w) if $got ne $w;
                    if $prop eq 'uniname' && !$w.starts-with('<') {
                        %count<names and aliases through uniparse>++;
                        my $back = (try uniparse($w)) // '';
                        fail 'names and aliases through uniparse', "uniparse('$w') is [{hexes($back.ords)}], want {$cp.fmt('%04X')}"
                            if $back ne $cp.chr;
                    }
                }
            }
        }
        $tsv.unlink;
        for %runs.kv -> $p, @r {
            for @r -> ($lo, $hi, $got, $want) {
                fail "property $p", "{$lo.fmt('%04X')}{$hi > $lo ?? '..' ~ $hi.fmt('%04X') ~ " ({$hi - $lo + 1})" !! ''}: "
                    ~ "got {$got.raku}, want {$want.raku}";
            }
        }
    }
}

# ---- summary ---------------------------------------------------------------
my $bad = 0;
for %count.keys.sort -> $family {
    my @f = |(%fails{$family} // ());
    if @f {
        $bad++;
        say "not ok - $family: {+@f} of {%count{$family}} missed";
        say "#   $_" for @f.head(15);
        say "#   ... and {@f - 15} more" if @f > 15;
    }
    else {
        say "ok - $family ({%count{$family}} checks)";
    }
}
say $bad ?? 'FAIL' !! 'PASS';
exit($bad ?? 1 !! 0);
