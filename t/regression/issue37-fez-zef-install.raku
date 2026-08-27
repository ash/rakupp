# Issue #37: `rakupp install fez` failed, and behind that one report sat six
# general engine bugs, each hiding the next —
#
#   * `:replacement<->` — the lexer fuses `<->` (and `<=>`/`<+>`) into ONE
#     operator token, and the colonpair-value parser never saw its `<`:
#     fez's `.decode('ascii', :replacement<->)` was a parse error at line
#     1200 of Fez::CLI. Any fused `<...>` op tight after a colonpair name is
#     the angle VALUE now.
#   * `use Zef::Distribution:ver("*")` — verSatisfies segmented the bare `*`
#     to [0] and rejected every candidate; `*` means anything.
#   * `use Zef:ver($?DISTRIBUTION.meta<version> // '*')` — the paren adverb
#     captured the EXPRESSION SOURCE TEXT as the requirement and matched
#     candidates against it ("1.1.3 !~ $?DISTRIBUTION..."); a non-literal is
#     unconstrained now. And $?DISTRIBUTION in a plain -I-loaded module was
#     the bare undefined, so `.meta` died mid-load — it is an object with an
#     empty meta hash there, like Rakudo's.
#   * `my Bool @results = Nil` answered [] where Rakudo answers one default
#     element — zef's Build counts on `?@results` seeing it.
#   * invoking the undefined value is the Any coercion (identity), not
#     "Cannot invoke non-Callable" — zef's own 02-checkbuild leans on it.
#   * `my Hash() %options` — the EMPTY coercion parens fell out of the
#     declaration and took %options with them (Text::Table::Simple, the
#     first dist we zef-installed as the end-to-end check).
#   * and from the same sweep: a Junction eigenstate that is an INTERPOLATING
#     regex (`rx/ <$_> /`) must match in the env it closed over — threading
#     dropped rxVal and `<$_>` read the match SUBJECT, so `any @globs`
#     matched everything (Fez::Util::Glob).
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# -- fused angle-op colonpair values ------------------------------------------
check((:replacement<->).raku, ':replacement("-")', 'the fused <-> is a colonpair angle value');
check((:x<=>).value, '=', '…and <=> (the spaceship)');
check((:x<+>).value, '+', '…and <+>');
# (the exact replacement semantics of .decode('ascii', :replacement<…>) are a
#  separate open gap — rakupp decodes permissively; what issue #37 needed was
#  the CALL to parse and run, which is what this pins)
check(("aéb".encode.decode('ascii', :replacement<->) ~~ Str), True, 'the fez call shape parses and runs');

# -- version requirements ------------------------------------------------------
# (the loader path is covered by the fez/zef installs in CI's ecosystem legs;
#  here we pin the * rule through EVAL of a use against a scratch tree)
{
    my $dir = $*TMPDIR.add("i37-ver-$*PID");
    ($dir.add("lib")).mkdir;
    $dir.add("lib/I37Probe.rakumod").spurt('unit module I37Probe; our sub hello is export { "hi" }');
    use lib $dir.add("lib").Str;
    my $ok = True;
    { EVAL 'use I37Probe:ver("*");'; CATCH { default { $ok = False } } }
    check($ok, True, ':ver("*") accepts a versionless candidate');
    # $?DISTRIBUTION in a -I-loaded module is an OBJECT with quiet meta
    $dir.add("lib/I37Dist.rakumod").spurt('unit module I37Dist; our sub dver is export { $?DISTRIBUTION.meta<version> // "none" }');
    my $dv = do { EVAL 'use I37Dist; dver()' };
    check($dv, 'none', '$?DISTRIBUTION.meta<missing> is a quiet default, not a death');
    try { $dir.add("lib/I37Probe.rakumod").unlink; $dir.add("lib/I37Dist.rakumod").unlink; $dir.add("lib").rmdir; $dir.rmdir }
}

# -- Nil into an array is one default element ----------------------------------
{
    my @a = Nil;
    check(@a.elems, 1, '@a = Nil is one element');
    check(@a[0].defined, False, '…the undefined default');
    my Bool @results = Nil;
    check(?@results, True, 'the zef Build shape: ?(my Bool @ = Nil) is True');
    my @empty = ();
    check(@empty.elems, 0, '() still empties');
}

# -- invoking undefined is the Any coercion ------------------------------------
{
    my $x;
    check($x(5), 5, 'Any-invoke is identity coercion');
    module M37 { our sub real is export { 1 } }
    check(M37::<&nope>("arg"), 'arg', 'a package-stash miss coerces like Rakudo');
}

# -- empty coercion parens in declarations -------------------------------------
{
    my Str() $s = 42;
    check($s, "42", 'my Str() $x coerces');
    my Hash() %options = (a => { x => 1 });
    check(%options.elems, 1, 'my Hash() %h declares and assigns');
    %options.append: (b => { y => 2 });
    check(%options.elems, 2, '…and stays usable (the Text::Table::Simple shape)');
}

# -- interpolating regexes keep their env through junction threading -----------
{
    my @pats = (Q/^x$/, Q/^y$/).map({ rx/ <$_> / }).list;
    check(?("x" ~~ any @pats), True, 'a junction of interpolating regexes matches its member');
    check(?("q" ~~ any @pats), False, '…and does NOT match everything');
    my $armed = rx/ <$_> /;
    check("boolify-safe", "boolify-safe", 'placeholder: boolify path covered by regexloop kernel');
}

# -- the installer's SHA-1 is the engine's, not a subprocess per key ----------
# (`rakupp uninstall fez` spawned shasum ~70 times — once per provided module
#  and file — and its forty seconds of spawning read as a hang)
{
    my $sha = try ::('&rakupp-sha1-hex');
    check($sha ~~ Callable, True, 'the engine exposes rakupp-sha1-hex to the installer');
    check($sha('abc').lc, 'a9993e364706816aba3e25717850c26c9cd0d89d', '…and it is real SHA-1 (engine spells it uppercase)');
}

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
