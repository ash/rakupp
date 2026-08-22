# Regression: the four blockers Sparrow6 hit under rakupp (2026-08-23). None is
# about Sparrow — each is a general divergence its runner happened to reach
# first. Every check below is Rakudo-verified.

my $ok = True;
sub ck($got, $want, $l) { unless $got eqv $want { say "FAIL: $l — {$got.raku} vs {$want.raku}"; $ok = False } }

# 1. IO::Path.absolute answers a Str, like .relative — NOT an IO::Path.
#    Sparrow6's set-sparrow-root assigns it to a `has Str $.sparrow-root`, so an
#    IO::Path failed the type check at the very first task-run.
ck("/tmp/x".IO.absolute.^name, 'Str', '.absolute is a Str');
ck("/tmp/x".IO.relative.^name, 'Str', '.relative is a Str');
ck("/abs/x".IO.absolute('/base'), '/abs/x', '.absolute($base) leaves an absolute path alone');
ck("x".IO.absolute('/base'),      '/base/x', '.absolute($base) joins a relative one');
ck("x".IO.absolute('/base/'),     '/base/x', '.absolute($base) does not double the slash');
#    …while .cleanup stays an IO::Path (only .absolute changed)
ck("a/./b".IO.cleanup.^name, 'IO::Path', '.cleanup is still an IO::Path');

# 2+3. A Proc::Async react: the stdout tap must fire, and `.ready` must exist.
#    The taps used to register AFTER the react body, but the sibling
#    `whenever $proc.start` runs the process INSIDE that body — so the output
#    was fed to an empty tap list and the block never ran. Sparrow6's runner is
#    written exactly this way, so every task printed nothing.
{
    my @out;
    my $ready-fired = False;
    my $proc = Proc::Async.new(:enc<utf8-c8>, 'echo', 'one two');
    react {
        whenever $proc.stdout.lines { @out.push($_) }
        whenever $proc.ready        { $ready-fired = True }
        whenever $proc.start        { done }
    }
    ck(@out, ['one two'], 'whenever $proc.stdout.lines fires inside a react');
    ck($ready-fired, True, 'whenever $proc.ready fires');
}
{   # the plain (unsplit) stdout supply too
    my $seen = '';
    my $proc = Proc::Async.new('echo', 'plain');
    react {
        whenever $proc.stdout { $seen ~= $_ }
        whenever $proc.start  { done }
    }
    ck($seen.chomp, 'plain', 'whenever $proc.stdout fires inside a react');
}

# 4. A TIGHT word list after a name subscripts the CALL's result; a SPACED one
#    is an argument list. Rakudo applies this whatever the signature says, and
#    `config<key>` is the API every Sparrow task and plugin is written against.
sub cfg () { %( name => 'X', deep => %( k => 'v' ) ) }
ck(cfg<name>, 'X', 'tight name<key> is cfg()<key>');
ck(cfg()<deep><k>, 'v', 'the explicit spelling still works');
ck(cfg<name deep>[0], 'X', 'a multi-word tight list slices the result');
sub words (*@a) { @a.join('|') }
#    (bound first: a listop swallows the rest of the argument list, under both
#    engines — `ck(words <p q>, …)` passes everything to `words`)
my $spaced = words <p q>;
ck($spaced, 'p|q', 'a SPACED word list is still an argument list');
ck(words('p', 'q'), 'p|q', '…same as passing them');

say 'PASS' if $ok;
