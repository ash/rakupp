#!/usr/bin/env raku
# Appendix B's flag list, against the flag table the binary actually has.
#
#   rakupp tools/check-book-appendix.raku
#
# The Internals book's appendices are not generated — `docs/book/build.raku`
# concatenates `ch/*.md`, so every entry in them is hand-typed and every count
# is a promise nobody checks. The review that added this gate found Appendix B
# missing 24 of the 66 flags the binary accepts, the whole subcommand surface,
# and nine environment variables.
#
# `src/main.cpp` already carries `kFlagDocs`, a declarative table of flag, arity
# and help text — the same table `--help` prints from. That makes the appendix's
# correctness checkable rather than aspirational: every flag in the table must
# appear in the appendix, and every flag-looking entry in the appendix must be
# in the table (so a removed flag cannot linger there either).
#
# Exit 0 when the two agree, 1 otherwise.

my $ROOT = $?FILE.IO.parent.parent.absolute.IO;
my $main = $ROOT.add('src/main.cpp');
my $appx = $ROOT.add('docs/book/ch/91-appendix-b.md');
die "no src/main.cpp"        unless $main.e;
die "no Appendix B"          unless $appx.e;

# ---- what the binary has -------------------------------------------------
my $src = $main.slurp;
my $table = $src ~~ / 'static const FlagDoc kFlagDocs[]' .*? \{ (.*?) \n '};' /;
die "kFlagDocs table not found in src/main.cpp — has it been renamed?" unless $table;
my %flags;
%flags{~$_[0]}++ for ($table[0] // '').match(/ '{"' (<[\-]> <[\w\-=]>*) '"' /, :g);
die "kFlagDocs parsed to nothing" unless %flags;

# ---- what the appendix documents ------------------------------------------
my $doc = $appx.slurp;
my %documented;
# a flag inside backticks, however the entry continues: `-e 'code'`, `--slim=safe`,
# `--profile[=dest]`, `-O`
%documented{~$_[0]}++ for $doc.match(/ '`' ('-' <[\w\-]>+) /, :g);

my (@missing, @extra);
for %flags.keys.sort -> $f {
    @missing.push($f) unless %documented{$f};
}
# an appendix entry that names a flag the binary does not have
for %documented.keys.sort -> $d {
    next if %flags{$d};
    # `-O2`, `-Os`, `-Ofast`: one flag with a suffix the C++ compiler gets
    next if $d ~~ / ^ '-O' /;
    next if $d.chars <= 2 && $d ne any(%flags.keys);   # `-e`-style prose mentions
    @extra.push($d) unless $src.contains('"' ~ $d ~ '"');
}

say "kFlagDocs: {%flags.elems} flags;  Appendix B names {%documented.elems}";
if @missing { say ""; say "NOT IN APPENDIX B:"; say "  $_" for @missing }
if @extra   { say ""; say "IN APPENDIX B, NOT IN THE BINARY:"; say "  $_" for @extra }
say "" if @missing || @extra;
say @missing || @extra
    ?? "check-book-appendix: FAILED ({+@missing} missing, {+@extra} stale)"
    !! "check-book-appendix: Appendix B matches the binary's flag table";
exit(@missing || @extra ?? 1 !! 0);
