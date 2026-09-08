# The IO::Path path methods, on invocants that are not paths.
#
# They read the invocant as a path STRING, and an undefined one satisfied that
# as "": `Any.basename` was "/", `Any.is-absolute` False, and `Any.contents`
# LISTED THE CURRENT DIRECTORY. Rakudo has none of the six on Any.
#
# `dir`/`contents` are what made it visible: once an unlistable path became an
# honest X::IO::Dir (#62) the empty path started throwing, and four Roast files
# that had been walking past a hole died mid-run (S26-documentation/04-code.t
# and 08-formattingcodes.t, S02-literals/pod.t, integration/advent2011-day10.t).
#
# Separately: an IO::Handle's `.path` is an IO::PATH, and the Str is what
# IO::Path's OWN `.path` answers one level down. We returned the stored string,
# so `$fh.path.unlink` was a missing method — File::Temp's AutoUnlink::DESTROY
# does exactly that and the dist went 3/3 -> 1/3 in the module battery.
my @fail;
sub check($got, $want, $desc) {
    @fail.push("$desc: got «{$got.raku}», wanted «{$want.raku}»") unless $got eqv $want;
}

# --- an undefined invocant has none of the six ---
for <contents dir is-absolute is-relative basename extension> -> $m {
    my $undef = (my @a = 1, 2)[5];           # out of range: an undefined Any
    my $thrown = '';
    try { $undef."$m"(); CATCH { default { $thrown = .^name } } }
    check($thrown, 'X::Method::NotFound', "Any.$m is a missing method");
}

# Nil ABSORBS rather than throwing — the rule `.IO` follows too
my $nil-threw = '';
try { Nil.basename; CATCH { default { $nil-threw = .^name } } }
check($nil-threw, '', 'Nil.basename absorbs instead of throwing');

# --- a real path still answers ---
check('/tmp/x.tar.gz'.IO.basename,  'x.tar.gz', 'a real path still has .basename');
check('/tmp/x.tar.gz'.IO.extension, 'gz',       '…and .extension');
check('/'.IO.is-absolute,           True,       '…and .is-absolute');
check('.'.IO.dir.elems > 0,         True,       '…and .dir lists a real directory');

# an unlistable path is still an honest X::IO::Dir, not a silent empty list (#62).
# `.dir` is the method both engines have: Rakudo's IO::Path has no `.contents`.
my $missing = '';
try { '/nonexistent-rakupp-probe-dir'.IO.dir; CATCH { default { $missing = .^name } } }
check($missing, 'X::IO::Dir', 'a missing directory throws X::IO::Dir');

# --- IO::Handle.path is an IO::Path ---
my $probe = $*TMPDIR.add('rakupp-handle-path-probe').Str;
my $fh = open $probe, :w;
check($fh.path.^name,      'IO::Path', "an IO::Handle's .path is an IO::Path");
check($fh.path.path.^name, 'Str',      "…and IO::Path's own .path is the Str");
check($fh.path.Str,        $probe,     '…naming the file it opened');
check($fh.Str,             $probe,     'the handle still Strs as its path');
check($fh.path.basename.chars > 0, True, '…and answers IO::Path methods');
$fh.close;
check($probe.IO.unlink, True, 'the probe file unlinks');

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' } else { say 'PASS' }
