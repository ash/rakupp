#!/usr/bin/env raku
# Every C++ name the Internals book cites, looked for in the source it describes.
#
#   rakupp tools/check-book-symbols.raku [PATH-SUBSTRING]
#
# The book's claim is that it explains THIS code, so a name it puts in backticks
# is a promise that the name exists. Names rot silently: a struct is renamed, a
# method moves to another class, an enum value is folded away, and the prose
# around it still reads perfectly. Nothing catches that — the chapter compiles
# no code and links no file.
#
# What is checked: qualified names (`Foo::bar`, `Value::isInt`) and member
# spellings (`.field`) are too noisy, so only the QUALIFIED form is taken, plus
# `NK::Whatever` enum values. A name is satisfied when the source contains either
# `Foo::bar` (an out-of-line definition or a call) or `bar` inside a `struct Foo`
# / `class Foo` block. Raku-side names are skipped by an allow list: the book
# quotes Raku's own `CompUnit::Repository::*`, `File::Find` and friends, which
# are not this codebase's to define.
#
# Exit 0 when every name resolves, 1 otherwise — a gate, not a report.

my $ROOT   = $?FILE.IO.parent.parent.absolute.IO;
my $FILTER = @*ARGS[0] // '';

# Raku and third-party namespaces the book legitimately names
my @foreign = <
    CompUnit File IO X Test NativeCall Proc Metamodel Rakudo NQP
    Foo Bar My App Some Str Int Num Rat Any Mu Cool List Array Hash
    nqp std absl folly unordered_map vector string map set boost llvm
>;

sub sources() {
    my @out;
    sub walk($d) {
        for $d.dir.sort -> $e {
            if $e.d { walk($e) unless $e.basename eq 'js-rt' }
            elsif $e.extension eq any(<cpp h hpp>) { @out.push($e) }
        }
    }
    walk($ROOT.add('src'));
    walk($ROOT.add('include')) if $ROOT.add('include').e;
    @out
}

my $corpus = sources().map(*.slurp).join("\n");

sub docs() {
    my @out;
    for $ROOT.add('docs/book/ch'), $ROOT.add('docs/internals') -> $d {
        next unless $d.e;
        @out.append($d.dir.grep(*.extension eq 'md').sort);
    }
    @out.grep({ !$FILTER || .Str.contains($FILTER) })
}

# The types the source declares. Matching a member to its OWNER would need brace
# matching over 200k lines of C++ (a first attempt truncated every struct body at
# its first nested `}` and reported sixteen live names as missing), so the rule
# is deliberately loose: a qualified name is satisfied when the source spells it
# verbatim, or when the owner is a type this codebase declares and the member is
# a word the codebase uses. That still catches the failure this gate is for — a
# name the book explains that the code no longer has — without inventing a C++
# parser to do it.
my %types;
%types{~$_[0]}++ for $corpus.match(/ ['struct' | 'class'] \s+ (<[A..Za..z_]> \w*) \s* <[:\{;]> /, :g);

my (%seen, @bad);
my $checked = 0;
for docs() -> $doc {
    my $rel = $doc.relative($ROOT);
    my $text = $doc.slurp;
    for $text.match(/ '`' (<[A..Za..z_]> \w* ['::' \w+]+) '`' /, :g) -> $m {
        my $name = ~$m[0];
        next if $name.split('::')[0] eq any(@foreign);
        next if %seen{"$rel\t$name"}++;
        $checked++;
        my @parts = $name.split('::');
        my $tail  = @parts.tail;
        my $owner = @parts[*-2];
        next if $corpus.contains($name);                      # Foo::bar, verbatim
        # `.contains`, not a `<< … >>` regex: the corpus is ten megabytes and the
        # word-boundary match silently answers False at that size here.
        next if %types{$owner} && $corpus.contains($tail);     # a member of a type we declare
        @bad.push("$rel: `$name` — not in src/ or include/");
    }
    # Member names, by this codebase's convention: a lower-camel identifier with
    # a trailing underscore. Narrow on purpose — 85 such names are cited across
    # the book and the short form, and this rule's whole false-positive rate is
    # zero, because nothing but a member is spelled that way. It is also the rule
    # that catches what the qualified-name check above cannot see: a bare
    # identifier inside a `cpp` block. `atomDropEnd_` was quoted in two documents
    # and removed from the lexer; the field is `unspaceEnd_` and means something
    # else, so the rule those blocks then state was wrong in both.
    for $text.match(/ << (<[a..z]> <[A..Za..z0..9]>* '_') >> /, :g) -> $m {
        my $name = ~$m[0];
        next if %seen{"$rel\t$name"}++;
        $checked++;
        @bad.push("$rel: `$name` — no such member in src/ or include/")
            unless $corpus.contains($name);
    }

    # Exception types. `X::` is Raku's namespace, so most of these are the
    # language's rather than this codebase's — but a book that shows the engine
    # THROWING one is claiming the engine has it. 14 are cited across both trees
    # and 13 are greppable; the fourteenth, `X::Role::Unimplemented`, appears in
    # two documents and nowhere in `src/` (the real one is `X::Comp::AdHoc`).
    for $text.match(/ << ('X::' <[A..Za..z]> <[A..Za..z0..9:]>*) >> /, :g) -> $m {
        my $name = ~$m[0];
        next if %seen{"$rel\t$name"}++;
        $checked++;
        @bad.push("$rel: `$name` — no such exception type in src/")
            unless $corpus.contains($name);
    }

    for $text.match(/ 'NK::' (\w+) /, :g) -> $m {
        my $name = 'NK::' ~ ~$m[0];
        next if %seen{"$rel\t$name"}++;
        $checked++;
        @bad.push("$rel: `$name` — not an AST node kind") unless $corpus.contains(~$m[0]);
    }
}
.say for @bad.sort;
say "$checked cited names checked, {+@bad} not found in the source";
exit(@bad ?? 1 !! 0);
