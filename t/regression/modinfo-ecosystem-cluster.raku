# Regression: the sixteen general interpreter fixes that came out of building
# showcase/modinfo (the 17-distribution ecosystem showcase), v2 campaign
# 2026-08-02. Every one is a language fix, not a module workaround — the modules
# only showed where to look. Each expectation below was checked against Rakudo.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. `#` is a WORD inside a bare < … > list, not the start of a comment
ck(<# name version>, ("#", "name", "version"), 'hash sign inside an angle word list');

# 2. the Latin-1 letters below U+00C0, and the Lm modifier letters, are
#    identifier characters (Font::AFM keys its encoding table on them)
{
    my %h = (:ª("ordfeminine"), :µ("mu"), :º("ordmasculine"), :ˆ("circumflex"), :ˇ("caron"));
    ck(%h.keys.sort.join(','), <ª µ º ˆ ˇ>.sort.join(','), 'non-ASCII letters as colonpair keys');
    ck(%h<µ>, 'mu', 'and they carry their values');
}

# 3. `my ::?CLASS:U $x` — the enclosing type as a declared type
{
    class Maker {
        method blank { my ::?CLASS:U $c = self.WHAT; $c }
    }
    ck(Maker.new.blank.^name, 'Maker', 'my ::?CLASS:U declaration');
}

# 4. a regex may follow a flip-flop operator
{
    my @in = <a START x END b>;
    my @got = @in.grep({ / START / ff / END / });
    ck(@got, ["START", "x", "END"], 'regex on both sides of ff');
}

# 5. `method dispatch:<…>` parses (Rakudo allows the category on a method)
{
    class Disp { method dispatch:<.?>(\name, |c) is raw { Nil }; method plain { 'p' } }
    ck(Disp.new.plain, 'p', 'a class declaring method dispatch:<.?> still works');
}

# 6. %h.Hash
{
    my %h = a => 1;
    ck(%h.Hash, {a => 1}, 'Hash.Hash');
    ck(%h.Map.Hash, {a => 1}, 'Map.Hash is a Hash');
}

# 7. a bare %h / @a parameter constrains the argument's type in multi dispatch
{
    multi sub which(%d) { 'hash' }
    multi sub which(@p) { 'list' }
    multi sub which($s) { 'scalar' }
    ck(which({a => 1}), 'hash', 'multi picks the %h candidate');
    ck(which([1]), 'list', 'multi picks the @a candidate');
    ck(which('x'), 'scalar', 'multi falls back to the scalar candidate');

    # …and it outranks a coercion parameter, which accepts anything convertible
    class Reader {
        multi method read(%data) { 'hash' }
        multi method read(IO() $path) { 'file' }
        multi method read(@paths) { 'list' }
    }
    ck(Reader.read({a => 1}), 'hash', 'sigil beats a coercion parameter (Hash)');
    ck(Reader.read(['/tmp']), 'list', 'sigil beats a coercion parameter (List)');
    ck(Reader.read('/tmp'), 'file', 'the coercion candidate still wins for a Str');
}

# 8. a bare proto/multi in a class body declares a SUB, and `is export` on it
#    reaches the importer (IO::Glob's `proto glob(|) is export {*}`)
{
    class Holder {
        proto pick-one(|) { * }
        multi sub pick-one(Str:D $s) { "s:$s" }
        multi sub pick-one(Int:D $i) { "i:$i" }
        method call-it { pick-one('x') ~ ' ' ~ pick-one(7) }
    }
    ck(Holder.new.call-it, 's:x i:7', 'proto/multi sub in a class body');
}

# 9. with/without take part in the if-chain
{
    sub pick($g, $a) {
        do with $g { "with" } elsif $a { "elsif" } else { "else" }
    }
    ck(pick(1, 0), 'with', 'with branch');
    ck(pick(Any, 1), 'elsif', 'elsif after with');
    ck(pick(Any, 0), 'else', 'else after with-elsif');
}

# 10. named destructuring binds from a Capture, not just from a Hash
{
    my $c = \(:path('P'), :globbers([1, 2]), :origin);
    my (:$path, :@globbers, :$origin) := $c;
    ck($path, 'P', 'capture named bind: scalar');
    ck(@globbers, [1, 2], 'capture named bind: array');
    ck($origin, True, 'capture named bind: flag');
}

# 11. smart-matching an object calls its ACCEPTS
{
    class Longer { method ACCEPTS($s) { $s.chars > 3 } }
    my $m = Longer.new;
    ck(?("abcd" ~~ $m), True, 'object ACCEPTS via ~~');
    ck(?("ab" ~~ $m), False, 'object ACCEPTS rejects');
    ck(<abcd ab xyzzy>.grep($m).List, ("abcd", "xyzzy"), 'object ACCEPTS via grep');
}

# 12. `make` inside a protoregex candidate belongs to the candidate, not <sym>
{
    grammar Base {
        token TOP { <term>+ }
        token term {
            || <match> { make "M[" ~ $<match>.made ~ "]" }
            || <char>  { make "C[" ~ $<char>.made ~ "]" }
        }
        proto token match {*}
        token char { $<char> = . { make $<char>.Str } }
    }
    grammar Globby is Base {
        token match:sym<*> { <sym> { make "STAR" } }
        token match:sym<?> { <sym> { make "ANY" } }
    }
    my $m = Globby.parse("*a?");
    ck($m<term>.map({ .made }).join(','), 'M[STAR],C[a],M[ANY]', 'make in a protoregex candidate');
}

# 13. a nested type named by a partial qualified path
{
    class Outer {
        class Inner { class Leaf { has $.v; method gist { "L$!v" } } }
        method leaf($v) { Inner::Leaf.new(:$v) }
    }
    ck(Outer.leaf(3).gist, 'L3', 'partial path to a nested type');
}

# 14. a regex variable composes as a sub-pattern, and the composition is baked
#     at construction (so folding a regex back into itself terminates correctly)
{
    my $head = rx/\d+/;
    ck(?("123" ~~ rx/^$head$/), True, 'a regex variable splices as a pattern');

    my @parts = rx/.*?/, '.', 'r';
    my $acc = rx/<?>/;
    for @parts -> $p {
        my $b = $acc;
        $acc = rx/$b$p/;
    }
    ck(?("x.r" ~~ rx/^$acc$/), True, 'folded regex matches');
    ck(?("xxr" ~~ rx/^$acc$/), False, 'folded regex rejects');
}

# 15. `.split` / `.comb` interpolate the pattern's variables
{
    my $d = '/';
    ck("*/META6.json".split(/ $d + /).List, ("*", "META6.json"), 'split on an interpolated regex');
    ck("a//b/c".split(/$d+/).List, ("a", "b", "c"), 'split collapses a run');
}

# 16a. .dir(:test) filters in the method form too
{
    my $tmp = $*TMPDIR.add("modinfo-regr-{$*PID}");
    mkdir $tmp;
    $tmp.add("a.raku").spurt('');
    $tmp.add("b.txt").spurt('');
    ck($tmp.dir(test => /'.raku' $/).map(*.basename).sort.List, ("a.raku",), 'IO::Path.dir(:test)');
    .unlink for $tmp.dir;
    rmdir $tmp;
}

# 16b. `sub rule(…)` is a routine named `rule`, not a grammar rule declaration
{
    sub rule(Int $n --> Str) { '-' x $n }
    sub after-rule($s) { "[$s]" }
    ck(rule(3), '---', 'a sub may be named rule');
    ck(after-rule('x'), '[x]', 'the routine after it still exists');
}

# 16c. a `|` slip flattens inside an array literal
{
    my @a = <x y>;
    ck([|@a, @a[0]], ["x", "y", "x"], 'slip flattens in [ ]');
    ck([|@a[1..*], |@a[0..0]], ["y", "x"], 'two slips flatten');
}

# 16d. a hash composer may have a COMPUTED key — but two terms in a row is a
#      listop call, and stays a block
{
    class Named { has $.name }
    my $d = Named.new(name => 'Loopy');
    my $p = { $d.name => True };
    ck($p.WHAT.^name, 'Hash', '{ $d.name => True } is a Hash');
    ck($p.keys.List, ("Loopy",), 'hash composer with a computed key');
    ck(?$p.clone<Loopy>, True, 'and it clones');

    sub dt(*%args) { %args<month> // -1 }
    ck(dt(month => 5), 5, 'a listop call in a block is still a block');
    my $blk = { dt month => 0 };
    ck($blk.WHAT.^name, 'Block', '{ dt month => 0 } is a Block, not a Hash');
}

say $ok ?? "PASS" !! "FAIL";
exit($ok ?? 0 !! 1);
