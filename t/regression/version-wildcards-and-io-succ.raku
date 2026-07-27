# Regression: Version wildcards / the `+` suffix, and `.succ` keeping its type.
#   * `v1.*` is a WILDCARD and `v1.0.1+` means "that version or later". Matching them
#     (`~~`) is a different question from ordering them (`cmp`), and the two were
#     sharing one part-by-part comparison: a `*` was silently skipped, so
#     `v1.0.1 ~~ v1.*` answered False on the length difference.
#   * for ORDERING a `*` is lower than any concrete part (`v1.2.3 cmp v1.*` is More)
#     and a trailing `+` sorts after the bare version.
#   * `<=>` was missing from the Version operator list, so it numified the string and
#     died on `1.0.1+`.
#   * `.succ`/`.pred` keep the invocant's flavour, so an IO::Path stays an IO::Path.
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) { @fail.push("$what: got $got want $want") unless $got eq $want }

# wildcards match
check((v1.0.1 ~~ v1.*).gist,   'True',  'a trailing wildcard takes the rest');
check((v1.2.3 ~~ v1.*).gist,   'True',  'however many parts');
check((v1     ~~ v1.*).gist,   'True',  'or none at all');
check((v2     ~~ v1.*).gist,   'False', 'but the prefix still has to match');
check((v1.0.1 ~~ v1.*.1).gist, 'True',  'an inner wildcard takes exactly one part');
check((v1.0.2 ~~ v1.*.1).gist, 'False', 'and what follows must match');

# the `+` suffix
check((v1.0.1 ~~ v1.0.1+).gist, 'True',  'a version matches its own `+`');
check((v1.0.2 ~~ v1.0.1+).gist, 'True',  'and so does a later one');
check((v1.0.0 ~~ v1.0.1+).gist, 'False', 'an earlier one does not');
check((v1.0.1+ == v1.0.1).gist, 'False', 'but `+` is not EQUAL to the bare version');
check((v1.0.1+ cmp v1.0.1).gist,'More',  'it sorts after it');
check((v1.0.1+ <=> v1.0.1).gist,'More',  'and <=> answers the same');

# ordering
check((v1.2 cmp v2.1).gist,     'Less',  'ordinary version ordering');
check((v1.2.3 cmp v1.*).gist,   'More',  'a wildcard is lower than a concrete part');
check((v1.0.1 cmp v1.0.1).gist, 'Same',  'and equals stay equal');

# .succ keeps the type
my $p = "foo/file_02.txt".IO.succ;
check($p.gist, '"foo/file_03.txt".IO', 'an IO::Path stays an IO::Path');
check($p.^name, 'IO::Path', 'by name too');
check("az".succ, 'ba', 'a plain Str is unaffected');
check(41.succ,   '42',  'and so is an Int');
check("bb".pred, 'ba', 'pred likewise');

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
