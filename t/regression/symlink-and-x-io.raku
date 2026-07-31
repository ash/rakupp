# Regression: the symlink/link/readlink SUB forms were missing entirely, and the
# X::IO exception family could not be CONSTRUCTED — rakupp threw those types from
# its own IO builtins, but `X::IO::Dir.new(...)` said "No such method 'new'".
#
# Two subtleties, both found by running File::Find's suite:
#  * `symlink` absolutizes its TARGET, as Rakudo does. The OS reads a relative
#    target relative to the LINK's directory, not the cwd, so
#    `symlink("t/a/d", "t/b/link")` otherwise makes a dangling link.
#  * `.throw` on an exception object now falls back to its `message` ATTRIBUTE
#    when the class carries no `message` METHOD (it reported the type name).
# Contract: exit 0 + last line PASS.
my @fail;
# .resolve: on macOS $*TMPDIR is itself a symlink (/var -> /private/var), and
# an absolutized target comes back resolved — compare like with like
my $dir = $*TMPDIR.add("rakupp-symlink-{$*PID}").resolve;
$dir.add('sub').mkdir(:p);
my $file = $dir.add('sub/f'); $file.spurt("hi\n");

# symlink with a RELATIVE target: the link resolves, because the target is
# absolutized before it reaches the OS. This runs in a CHILD with a real working
# directory — `chdir` sets $*CWD, which is not the same thing on every engine.
sub inDir($code) {
    run($*EXECUTABLE.absolute, '-e', $code, :cwd($dir.absolute), :out, :err)
        .out.slurp(:close).chomp
}
@fail.push('relative target resolves')
    unless inDir(q{symlink('sub/f', 'rel-link'); say 'rel-link'.IO.e}) eq 'True';
@fail.push('link points at the target')
    unless inDir(q{say 'rel-link'.IO.readlink.Str}) eq $file.absolute;
@fail.push('reading through the link')
    unless inDir(q{say 'rel-link'.IO.slurp.chomp}) eq 'hi';

# an absolute target is left alone
@fail.push('absolute target kept')
    unless inDir("symlink('{$file.absolute}', 'abs-link'); say 'abs-link'.IO.readlink.Str")
             eq $file.absolute;

# .readlink answers an IO::Path, not a Str
@fail.push('readlink is an IO::Path')
    unless inDir(q{say 'rel-link'.IO.readlink.^name}) eq 'IO::Path';

# a hard link
@fail.push('hard link')
    unless inDir(q{link('sub/f', 'hard'); say 'hard'.IO.slurp.chomp}) eq 'hi';

# a failing symlink throws X::IO::Symlink, not a bare error
@fail.push('symlink over an existing name throws')
    unless inDir(q{say (try { symlink('sub/f', 'rel-link'); 'no' } // $!.^name)})
             eq 'X::IO::Symlink';

# every X::IO exception can be constructed, and composes Rakudo's message text
my @cases =
    X::IO::Dir.new(path => 'P', os-error => 'E'),
        "Failed to get the directory contents of 'P': E",
    X::IO::Rmdir.new(path => 'P', os-error => 'E'),
        "Failed to remove the directory 'P': E",
    X::IO::Unlink.new(path => 'P', os-error => 'E'),
        "Failed to remove the file 'P': E",
    X::IO::Chdir.new(path => 'P', os-error => 'E'),
        "Failed to change the working directory to 'P': E",
    X::IO::Cwd.new(os-error => 'E'),
        "Failed to get the working directory: E",
    X::IO::Symlink.new(name => 'N', target => 'T', os-error => 'E'),
        "Failed to create symlink called 'N' on target 'T': E",
    X::IO::Link.new(name => 'N', target => 'T', os-error => 'E'),
        "Failed to create link called 'N' on target 'T': E",
    X::IO::Rename.new(from => 'F', to => 'T', os-error => 'E'),
        "Failed to rename 'F' to 'T': E",
    X::IO::Copy.new(from => 'F', to => 'T', os-error => 'E'),
        "Failed to copy 'F' to 'T': E",
    X::IO::Move.new(from => 'F', to => 'T', os-error => 'E'),
        "Failed to move 'F' to 'T': E",
    X::IO::Mkdir.new(path => 'P', mode => 0o755, os-error => 'E'),
        "Failed to create directory 'P' with mode '0o755': E",
    X::IO::Chmod.new(path => 'P', mode => 0o755, os-error => 'E'),
        "Failed to set the mode of 'P' to '0o755': E",
    X::IO::DoesNotExist.new(path => 'P', trying => 'open'),
        "Failed to find 'P' while trying to do '.open'";
for @cases -> $ex, $want {
    @fail.push("{$ex.^name}: {$ex.message}") unless $ex.message eq $want;
}

# a constructed one throws, keeps its type through CATCH, and reports its message
my $caught;
try {
    X::IO::Dir.new(path => 'd', os-error => 'e').throw;
    CATCH { when X::IO::Dir { $caught = .message } }
}
@fail.push("catch by type: {$caught // 'not caught'}")
    unless $caught eq "Failed to get the directory contents of 'd': e";

# .throw reports a plain `has $.message` attribute, not the type name
class MyEx is Exception { has $.message }
@fail.push('user exception message')
    unless (try { MyEx.new(message => 'custom').throw; '' } // $!.message) eq 'custom';

unlink($dir.add('rel-link'), $dir.add('abs-link'), $dir.add('hard'), $file);
rmdir($dir.add('sub')); rmdir($dir);

if @fail { note "FAILED:\n" ~ @fail.join("\n"); say 'FAIL' }
else     { say 'PASS' }
