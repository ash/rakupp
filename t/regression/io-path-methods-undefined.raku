# The IO::Path path-string methods do not answer for an undefined invocant.
#
# They read the invocant as a path string, and an undefined one satisfied that
# as "" — so `Any.basename` was "/", `Any.is-absolute` False, and
# `Any.contents` LISTED THE CURRENT DIRECTORY. Rakudo has none of them on Any.
#
# `dir`/`contents` are what made it visible: once an unlistable path became an
# honest X::IO::Dir (#62) the empty path started throwing, and four Roast files
# that had been walking past a hole died mid-run (S26-documentation/04-code.t
# and 08-formattingcodes.t, S02-literals/pod.t, integration/advent2011-day10.t)
# — each on `.contents` of something undefined, reported as
# "Failed to get the directory contents of '<cwd>': No such file or directory".
my $fails = 0;
for <contents dir is-absolute is-relative basename extension> -> $m {
    my $undef = (my @a = 1, 2)[5];      # out of range: an undefined Any
    my $r = try { $undef."$m"() };
    unless $! && $!.^name eq 'X::Method::NotFound' {
        say "NOT OK: Any.$m answered { $! ?? $!.^name !! $r.gist } instead of X::Method::NotFound";
        $fails++;
    }
}
say $fails == 0 ?? 'ok undefined invocant has no path methods' !! "NOT OK: $fails";

# Nil ABSORBS rather than throwing, the rule `.IO` follows too
my $n = try { Nil.basename };
say $! ?? "NOT OK: Nil.basename threw {$!.^name}" !! 'ok Nil absorbs';

# a real path still answers, and .dir still lists a real directory. `.dir` is
# the method both engines have: Rakudo's IO::Path has no `.contents` at all, so
# only the undefined-invocant loop above can name it on both.
say '/tmp/x.tar.gz'.IO.basename eq 'x.tar.gz' ?? 'ok real path basename' !! 'NOT OK basename';
say '/'.IO.is-absolute ?? 'ok real path is-absolute' !! 'NOT OK is-absolute';
say '.'.IO.dir.elems > 0 ?? 'ok real dir listing' !! 'NOT OK dir';

# an unlistable path is still an honest X::IO::Dir, not a silent empty list (#62)
my $missing = try { '/nonexistent-rakupp-probe-dir'.IO.dir };
say $! && $!.^name eq 'X::IO::Dir'
    ?? 'ok missing dir throws X::IO::Dir' !! "NOT OK: { $! ?? $!.^name !! $missing.gist }";

say 'PASS';
