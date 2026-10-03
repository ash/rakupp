#!/usr/bin/env rakupp
# Packs a Linux install layout (bin/ + lib/ + include/ + share/, what
# `cmake --install` writes) into a Debian package, for
# `sudo apt install ./rakupp_<version>_<arch>.deb`.
#
# It packs, it does not build: the binaries are the release archive's own,
# so the .deb is the archive under /usr, byte for byte, and inherits the
# floor the archive has (glibc 2.28, GCC 11's libstdc++ -- tools/floor-gate.raku
# holds both). That is also why Depends is written here rather than computed
# by dpkg-shlibdeps: the floor gate is what knows it.
#
# Under /usr, findRuntime() reaches lib/ and include/rakupp/ by its usual
# `../lib` rule, so `--exe` works from the package unchanged; and
# `rakupp upgrade` refuses a /usr prefix, sending the user to apt.
#
# Only the name `rakupp` is installed: Debian's own rakudo package owns
# /usr/bin/raku, and a .deb cannot ask the one-liner's second-name question.
#
# Usage:  rakupp tools/make-deb.raku dist/rakupp [OUT]
#         OUT is a directory (default .), which gets Debian's own name,
#         rakupp_<version>_<arch>.deb, or a file name ending in .deb
#         (release.yml: rakupp-linux-x86_64.deb, a stable download link)
#         --arch=amd64|arm64|…  (default: dpkg --print-architecture)
# Needs dpkg-deb, md5sum and du (Debian/Ubuntu; the ubuntu-* runners have them).
# Prints the path of the .deb it wrote.

sub sh(*@cmd, :$cwd = $*CWD) {
    my $p = run |@cmd, :out, :err, :$cwd;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    die "{@cmd.head} failed (exit {$p.exitcode}):\n$err" unless $p.exitcode == 0;
    $out
}

sub MAIN(Str $layout, Str $out = '.', Str :$arch) {
    my $src  = $layout.IO;
    my $repo = $*PROGRAM.IO.parent.parent;
    die "$layout/bin/rakupp: no such file -- pass the install layout (cmake --install --prefix)"
        unless $src.add('bin/rakupp').f;

    # `Raku++ 5.2.1 (2026-10-03) x86_64-linux` on a tag; between tags git's
    # describe, `5.2.1-4-g954f1af7`, which becomes 5.2.1+4.g954f1af7 -- a
    # hyphen in a Debian version starts the package revision, and `+` sorts
    # the snapshot after the release it follows.
    my $banner  = sh($src.add('bin/rakupp').absolute, '--version').trim;
    my $version = $banner.words[1]
        // die "cannot read a version from: $banner";
    $version .= subst('-', '+');
    $version .= trans('-' => '.');
    die "not a Debian version: $version" unless $version ~~ /^ <[0..9]> <[0..9A..Za..z.+~]>* $/;

    my $deb-arch = $arch // sh('dpkg', '--print-architecture').trim;

    my $stage = $*TMPDIR.add("rakupp-deb-$*PID");
    sh('rm', '-rf', $stage.absolute);
    my $usr = $stage.add('usr');
    $usr.mkdir;
    for <bin lib include share> -> $d {
        sh('cp', '-R', $src.add($d).absolute, $usr.absolute) if $src.add($d).d;
    }
    # The one-liner's `raku` link, when the layout came from an install that
    # said yes to it: /usr/bin/raku is the rakudo package's.
    for $usr.add('bin').dir -> $f {
        $f.unlink unless $f.basename eq 'rakupp';
    }
    my $doc = $usr.add('share/doc/rakupp');
    $doc.mkdir;
    $repo.add('LICENSE').copy($doc.add('copyright'));
    $repo.add('README.md').copy($doc.add('README.md'));

    # dpkg records what it finds: directories 755, files 644, executables 755.
    sh('chmod', '-R', 'u=rwX,go=rX', $usr.absolute);
    sh('chmod', '755', $usr.add('bin/rakupp').absolute);

    my $control = $stage.add('DEBIAN');
    $control.mkdir;
    my $md5 = sh('find', 'usr', '-type', 'f', '-exec', 'md5sum', '{}', '+', :cwd($stage.absolute));
    $control.add('md5sums').spurt($md5.lines.sort.join("\n") ~ "\n");

    my $kb = sh('du', '-sk', $usr.absolute).words.head;
    $control.add('control').spurt: qq:to/END/;
        Package: rakupp
        Version: $version
        Architecture: $deb-arch
        Maintainer: Andrew Shitov <mail\@andreyshitov.com>
        Installed-Size: $kb
        Depends: libc6 (>= 2.28), libstdc++6 (>= 11)
        Suggests: g++ | clang
        Section: interpreters
        Priority: optional
        Homepage: https://raku.online
        Description: Raku++, an implementation of the Raku programming language
         A self-contained Raku engine in one binary, with a module installer
         (rakupp install), a REPL, and --exe, which compiles a Raku program to a
         native executable. --exe needs a C++ compiler, hence the suggestion.
         .
         The command is rakupp. The name raku is left to the rakudo package.
        END

    my $deb = $out.ends-with('.deb') ?? $out.IO
            !! $out.IO.add("rakupp_{$version}_{$deb-arch}.deb");
    sh('dpkg-deb', '--build', '--root-owner-group', '-Zxz', $stage.absolute, $deb.absolute);
    sh('rm', '-rf', $stage.absolute);
    put $deb;
}
