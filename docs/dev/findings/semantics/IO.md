# IO::Path, IO::Handle, IO::Spec, IO::Special, IO::Pipe, IO::CatHandle — semantics sheet

Provenance: Rakudo tag `2026.08`, files `src/core.c/IO/Path.rakumod`
(1,149 lines), `IO/Handle.rakumod` (932), `io_operators.rakumod` (390),
`IO/Spec/Unix.rakumod` (278), `IO/Special.rakumod` (45),
`IO/Pipe.rakumod` (84), `IO/Path/Parts.rakumod` (55), read in full;
`IO/CatHandle.rakumod` (427) read for `path`, `next-handle` and the
switching rules. Out of scope: sockets, `IO::Notification`, signals, the
Win32, Cygwin and QNX specs, handles shared between threads. Oracle:
Homebrew Rakudo v2026.08 on macOS. Compared against Raku++
4.0.1-23-g80b16d18 (build-arm64, 2026-09-19). Format and legend:
[README.md](README.md).

Where this sits against the declared spec: Rakudo passes 46 of the
`S32-io` files and 23 of `S16-io` on this machine; Raku++ passes 19 and 8.
The rules below are the ones behind those files: which operations return
a Failure and which throw, what a missing path answers to each test, how
paths are split, joined and compared, what `open` accepts, how newlines
are translated on the way in and out, and what the standard handles look
like. 7 of the 26 items are neither fully documented nor
fully asserted by Roast.

Every probe ran in a fresh sandbox directory per engine with the string
`line1\n42\nlast` on standard input. The sandbox's absolute path is
elided as `…` in two outputs.

## A. IO::Path: construction, parts, navigation

### IO-01  Construction and printing                                D:partial R:yes V:spec
`.IO` on a Str or Cool gives an IO::Path that keeps the string as given in
`.path` and `.Str`; `.gist` is the path in quotes plus `.IO`; `.raku`
carries `:SPEC` and `:CWD`. An empty path throws `X::AdHoc` ("Must specify
a non-empty string as a path"), a NUL byte `X::IO::Null`. `Any.IO` does
not exist. `.CWD` is an absolute Str, `.SPEC` the platform spec. The
`:basename`/`:dirname` constructor joins the two.
```
say "a/b.txt".IO.^name, " ", "a/b.txt".IO.path.raku, " ", "a/b.txt".IO.Str.raku, " ", "a/b.txt".IO.gist, " ", "/a/b.txt".IO.gist, " ", "a/b.txt".IO.raku.subst(/':CWD(' <-[)]>* ')'/, ':CWD(…)'), " ", (try IO::Path.new("")) // $!.^name ~ ":" ~ $!.message, " | ", (try IO::Path.new("a\0b")) // $!.^name, " ", IO::Path.new(:basename<b.txt>, :dirname</foo>).Str, " ", IO::Path.new(:basename<b.txt>).Str, " ", 42.IO.Str.raku, " ", (try Any.IO) // $!.^name, " ", (try "".IO) // $!.^name, " ", do { sub f(IO() $p) { $p.^name }; f("a") }, " ", "a".IO.IO.^name, " ", "a".IO.SPEC.^name, " ", "a".IO.CWD.^name, " ", "a".IO.CWD.IO.is-absolute
# rakudo 2026.08: IO::Path "a/b.txt" "a/b.txt" "a/b.txt".IO "/a/b.txt".IO IO::Path.new("a/b.txt", :SPEC(IO::Spec::Unix), :CWD(…)) X::AdHoc:Must specify a non-empty string as a path | X::IO::Null /foo/b.txt b.txt "42" X::Method::NotFound X::AdHoc IO::Path IO::Path IO::Spec::Unix Str True
```
rakupp 4.0.1-84: matches — `"".IO` and `IO::Path.new("")` throw, and the `:basename`/`:dirname` constructor joins.

### IO-02  volume, dirname, basename, parts                         D:yes R:yes V:spec
A trailing slash is dropped before splitting; the root splits as `/` and
`/`; a bare name has dirname `.`; `.` and `..` are their own basenames.
`.parts` is an `IO::Path::Parts` that also answers as a Map and a List.
```
say do { for </foo/bar.txt bar.txt / /foo/ foo/ . .. /foo/bar/ a/b/c.d.e> -> $p { my $io = $p.IO; print "$p=[{$io.volume.raku},{$io.dirname},{$io.basename}] " }; "" }, "| ", "/a/b.txt".IO.parts.raku, " ", "/a/b.txt".IO.parts<dirname>, " ", "/a/b.txt".IO.parts[2].raku
# rakudo 2026.08: /foo/bar.txt=["",/foo,bar.txt] bar.txt=["",.,bar.txt] /=["",/,/] /foo/=["",/,foo] foo/=["",.,foo] .=["",.,.] ..=["",.,..] /foo/bar/=["",/foo,bar] a/b/c.d.e=["",a/b,c.d.e] | IO::Path::Parts.new("","/a","b.txt") /a :basename("b.txt")
```
rakupp 4.0.1-84: matches — `.volume` answers `""` and `.parts` an IO::Path::Parts.

### IO-03  absolute, relative, cleanup                              D:yes R:yes V:spec
`.absolute` resolves against `.CWD`; `.relative($base)` walks up with
`..`; `.cleanup` collapses `//`, `/./`, a leading `./` and a trailing `/`,
removes `..` only directly under the root, and leaves `a/../b` alone (no
filesystem access).
```
say "/a/b".IO.is-absolute, " ", "a/b".IO.is-absolute, " ", "a".IO.is-relative, " ", ("a".IO.absolute eq $*CWD.Str ~ "/a"), " ", "/a/b".IO.relative("/a"), " ", "/a/b".IO.relative("/x"), " ", "/a/b".IO.relative("/a/b"), " ", "a//b/./c/".IO.cleanup.Str, " ", "./a".IO.cleanup.Str, " ", "/../a".IO.cleanup.Str, " ", "a/../b".IO.cleanup.Str, " ", "/".IO.cleanup.Str, " ", "a/.".IO.cleanup.Str, " ", "/a/b/".IO.cleanup.Str
# rakudo 2026.08: True False True True b ../a/b . a/b/c a /a a/../b / a /a/b
```
rakupp 4.0.1-84: matches — `cleanup` drops a `..` directly under the root.

### IO-04  parent, child, add, sibling                              D:partial R:partial V:spec
`parent` is textual: `/` stays `/`, `foo` becomes `.`, `.` becomes `..`,
`..` becomes `../..`. `parent(n)` repeats, `parent(0)` is the path, a
negative depth throws `X::OutOfRange`. `child`, `add` and `sibling` join
with a single separator; a child starting with `/` is still joined.
```
say "/foo/bar".IO.parent.Str, " ", "/foo/bar".IO.parent.parent.Str, " ", "/".IO.parent.Str, " ", "foo".IO.parent.Str, " ", ".".IO.parent.Str, " ", "..".IO.parent.Str, " ", "../..".IO.parent.Str, " ", "foo/bar".IO.parent.Str, " ", "/foo/bar/baz".IO.parent(2).Str, " ", "/foo".IO.parent(0).Str, " ", (try "/foo".IO.parent(-1)) // $!.^name, " ", "a".IO.child("x").Str, " ", "a/".IO.child("x").Str, " ", "a".IO.add("x", "y").Str, " ", "a".IO.add("/x").Str, " ", "a/b".IO.sibling("z").Str, " ", "/".IO.child("x").Str, " ", ".".IO.child("x").Str
# rakudo 2026.08: /foo / / . .. ../.. ../../.. foo /foo /foo X::OutOfRange a/x a/x a/x/y a/x a/z /x x
```
rakupp 4.0.1-84: matches.

### IO-05  extension, succ, pred, Numeric                           D:yes R:yes V:spec
The extension is the text after the last dot of the basename; `:parts(n)`
takes n dot-parts, a Range the longest available within it, too many parts
gives `""`. A replacement keeps the dirname; `:parts(0)` appends; `""`
removes; `:joiner` replaces the dot. A dotfile's extension is its whole
name after the dot. `succ`/`pred` increment the part before the first dot
as a string. `.Numeric` numifies the basename.
```
say "a.tar.gz".IO.extension, " ", "a.tar.gz".IO.extension(:parts(2)), " ", "a.tar.gz".IO.extension(:parts(0..2)), " ", "a.tar.gz".IO.extension(:parts(3)).raku, " ", "a.tar.gz".IO.extension("txt").Str, " ", "a.tar.gz".IO.extension("").Str, " ", "a.tar.gz".IO.extension("txt", :parts(2)).Str, " ", "a.tar.gz".IO.extension("txt", :parts(0)).Str, " ", "noext".IO.extension.raku, " ", "noext".IO.extension("x").Str, " ", ".hidden".IO.extension, " ", "a.".IO.extension.raku, " ", "a.".IO.extension("x").Str, " ", "d/a.b".IO.extension("c").Str, " ", "a.tar.gz".IO.extension("x", :joiner("-")).Str, " ", "file1.txt".IO.succ.Str, " ", "file1.txt".IO.pred.Str, " ", "file9".IO.succ.Str, " ", "42".IO.Numeric, " ", ("x".IO.Numeric.^name)
# rakudo 2026.08: gz tar.gz tar.gz "" a.tar.txt a.tar a.txt a.tar.gz.txt "" noext.x hidden "" a.x d/a.c a.tar-x file2.txt file0.txt filf0 42 Failure
```
rakupp 4.0.1-84: matches — too few dot-parts takes none off and still appends.

### IO-06  Comparing paths                                          D:yes R:yes V:spec
With an IO::Path on the right, smartmatch compares the two `.absolute`
strings, which are canonical: `./a`, `a/` and `/../foo` match `a`, `a`
and `/foo`, while `x/../a` does not (no `..` resolution away from the
root) and different CWDs do not. With a Str on the right the match is
`Str.ACCEPTS`, plain equality of the path text, so `"a".IO ~~ "./a"` is
False. `eqv` compares path text and CWD (`./a` is not `eqv` to `a`);
`===` is object identity (an ObjAt WHICH); `eq` compares the Str.
```
say ("a".IO ~~ "a".IO), " ", ("a".IO ~~ "./a"), " ", ("a".IO ~~ "a/"), " ", ("a".IO ~~ "b"), " ", ("./a".IO eqv "a".IO), " ", ("a".IO eqv "a".IO), " ", ("a".IO === "a".IO), " ", ("a".IO eq "a"), " ", ("/a".IO ~~ "a".IO), " ", ("a".IO.WHICH.^name), " ", ("a".IO ~~ IO::Path), " ", ("a".IO ~~ IO), " ", ("a".IO ~~ Cool), " ", (IO::Path.new("a", :CWD("/x")) ~~ IO::Path.new("a", :CWD("/y"))), " ", IO::Path.new("a", :CWD("/x")).absolute
# rakudo 2026.08: True False False False False True False True False ObjAt True True True False /x/a
say ("./a".IO ~~ "a".IO), " ", ("a".IO ~~ "./a".IO), " ", ("/foo".IO ~~ "/../foo".IO), " ", ("a/".IO ~~ "a".IO), " ", ("a".IO ~~ "a"), " ", ("./a" ~~ "a".IO), " ", ("./a".IO.absolute eq "a".IO.absolute), " ", ("a".IO.ACCEPTS("./a")), " ", ("x/../a".IO ~~ "a".IO), " ", ("a".IO eqv "a".IO), " ", ("./a".IO eqv "a".IO), " ", (IO::Path.new("a", :CWD("/x")) eqv IO::Path.new("a", :CWD("/y")))
# rakudo 2026.08: True True True True True True True True False True False False
```
rakupp 4.0.1-84: differs in ONE field — `.absolute` is canonical now, so every smartmatch in the second probe holds, and `eqv` compares the :CWD. `"a".IO === "a".IO` is still True: Rakudo's IO::Path WHICH carries the object's address, and a path here is a copied Str value with no identity slot to carry one. Nothing in Roast asks.

## B. File tests and the standard handles

### IO-07  File tests on an existing and a missing path              D:yes R:partial V:spec
`.e` is a plain Bool. Every other test on a missing path returns a Failure
`X::IO::DoesNotExist` whose `.trying` names the test; so do `modified`,
`accessed`, `changed`, `mode`, `user`, `group`, `inode`. `.mode` is an
IntStr whose Str is four octal digits; the timestamps are Instants.
`dir-with-entries` answers whether a directory has any entry, optionally
matching `:test`.
```
say do { "f04".IO.spurt("abc"); "d04".IO.mkdir; my $f = "f04".IO; my $m = "m04".IO; ($f.e, $f.d, $f.f, $f.s, $f.z, $f.l, $f.r, $f.w, $f.x, $f.rw, $f.rwx, $m.e, $m.d.^name, $m.d.exception.^name, $m.d.exception.trying, $m.f.^name, $m.s.^name, $m.z.^name, $m.l.^name, $m.r.^name, $m.modified.^name, $m.mode.^name, "d04".IO.d, "d04".IO.f, "d04".IO.e, $f.mode.^name, $f.mode.Str.chars, $f.mode.Str.substr(0, 2), $f.modified.^name, ($f.modified <= now), $f.accessed.^name, $f.changed.^name, $f.user.^name, $f.group.^name, $f.inode.^name, $f.dev.^name, $f.devtype.^name, "d04".IO.dir-with-entries, "d04".IO.dir-with-entries(:test(/x/))).join(" ") }
# rakudo 2026.08: True False True 3 False False True True False True False False Failure X::IO::DoesNotExist d Failure Failure Failure Failure Failure Failure Failure True False True IntStr 4 06 Instant True Instant Instant Int Int Int Int Int False False
```
rakupp 4.0.1: differs — `.d` on a missing path is a plain False, so the line died at `.exception`; the rest is unmeasured.

### IO-08  IO::Special and $*IN, $*OUT, $*ERR                       D:yes R:yes V:spec
The standard handles' paths are `IO::Special` values named `<STDIN>`,
`<STDOUT>`, `<STDERR>`: they exist, are neither file nor directory, have
size 0, are readable (STDIN) or writable (the others), have no mode and
type-object timestamps, and compare by value. The handles start opened
with utf8, `nl-out` `"\n"`, `nl-in` `["\n", "\r\n"]` and chomp on; `.t` is
False when not a terminal; the native descriptors are 0, 1, 2.
```
say $*IN.path.^name, " ", $*IN.path.Str, " ", $*IN.path.raku, " ", $*IN.path.e, " ", $*IN.path.d, " ", $*IN.path.f, " ", $*IN.path.s, " ", $*IN.path.r, " ", $*IN.path.w, " ", $*OUT.path.r, " ", $*OUT.path.w, " ", $*ERR.path.Str, " ", $*IN.path.modified.raku, " ", $*IN.path.mode.raku, " ", ($*IN.path === IO::Special.new("<STDIN>")), " ", $*IN.path.IO.^name, " ", $*IN.path.what, " ", $*IN.opened, " ", $*OUT.encoding, " ", $*OUT.t, " ", $*IN.t, " ", $*OUT.nl-out.raku, " ", $*IN.nl-in.raku, " ", $*IN.chomp, " ", $*OUT.gist, " ", $*OUT.Str, " ", $*OUT.path.gist, " ", $*IN.native-descriptor, " ", $*ERR.native-descriptor
# rakudo 2026.08: IO::Special <STDIN> IO::Special.new("<STDIN>") True False False 0 True False False True <STDERR> Instant Nil True IO::Special <STDIN> True utf8 False False "\n" $["\n", "\r\n"] True IO::Handle<IO::Special.new("<STDOUT>")>(opened) <STDOUT> IO::Special.new("<STDOUT>") 0 2
```
rakupp 4.0.1-84: matches — the handles report `IO::Handle`, and IO::Special gists and rakus as its constructor.

## C. Filesystem operations

### IO-09  mkdir, rmdir, unlink                                     D:partial R:partial V:spec
`mkdir` returns the IO::Path, creates missing parents, is a silent no-op
on an existing directory (the mode argument is then ignored) and fails
with `X::IO::Mkdir` (`.path`) where a file is in the way. `rmdir` fails
with `X::IO::Rmdir` on a non-empty directory, a missing path or a file.
`unlink` returns True for a file and also for a missing path, and fails
with `X::IO::Unlink` on a directory. The sub forms take a list and return
the names that did not fail (so a missing name is listed by `unlink`, and
`rmdir` of a file gives `[]`); with no argument they throw
`X::NoZeroArgMeaning`.
```
say do { sub F($f) { $f ~~ Failure ?? do { $f.so; $f.^name ~ ":" ~ $f.exception.^name } !! "ok:" ~ $f.raku }; ("d05".IO.mkdir.^name, "d05/x/y".IO.mkdir.Str, "d05/x/y".IO.d, "d05".IO.mkdir.Str, do { "f05".IO.spurt("x"); my $f = "f05".IO.mkdir; F($f) ~ ($f ~~ Failure ?? ":" ~ $f.exception.path.IO.basename !! "") }, F("d05".IO.rmdir), "d05/x/y".IO.rmdir, F("m05".IO.rmdir), F("m05".IO.unlink), "f05".IO.unlink, "f05".IO.e, do { "u1".IO.spurt("a"); "u2".IO.spurt("b"); unlink("u1", "u2", "u3").raku }, (try unlink()) // $!.^name, (try rmdir()) // $!.^name, F("d05".IO.unlink), "d05".IO.e, do { "f05b".IO.spurt("x"); rmdir("f05b").raku ~ " " ~ F("f05b".IO.rmdir) }, mkdir("d05c").^name, "d05c".IO.rmdir, F("d05/x".IO.rmdir), "d05/x".IO.e, F("d05".IO.mkdir(0o700)), "d05".IO.mode.Str).join(" ") }
# rakudo 2026.08: IO::Path d05/x/y True d05 Failure:X::IO::Mkdir:f05 Failure:X::IO::Rmdir True Failure:X::IO::Rmdir ok:Bool::True True False ["u1", "u2", "u3"] X::NoZeroArgMeaning X::NoZeroArgMeaning Failure:X::IO::Unlink True [] Failure:X::IO::Rmdir IO::Path True ok:Bool::True False ok:IO::Path.new("d05", :SPEC(IO::Spec::Unix), :CWD("…")) 0755
```
rakupp 4.0.1: differs — the `X::IO::Mkdir` exception's `.path` is undefined, so the line died there.

### IO-10  copy                                                     D:yes R:yes V:spec
Returns True. Fails with `X::IO::Copy` when source and target are the same
(os-error "source and target are the same"), when `:createonly` and the
target exists, when a directory would overwrite a file, and when the source
is missing; the target is then not created.
```
say do { "s06".IO.spurt("src"); ("s06".IO.copy("c06"), "c06".IO.slurp, "s06".IO.copy("s06").^name, "s06".IO.copy("s06").exception.^name, "s06".IO.copy("s06").exception.os-error, "s06".IO.copy("c06", :createonly).^name, "s06".IO.copy("c06", :createonly).exception.os-error, do { "dd06".IO.mkdir; "dd06".IO.copy("c06").exception.os-error }, "m06".IO.copy("x06").^name, "m06".IO.copy("x06").exception.^name, "x06".IO.e).join(" ") }
# rakudo 2026.08: True src Failure X::IO::Copy source and target are the same Failure :createonly specified and destination exists cannot copy a directory to a file Failure X::IO::Copy False
```
rakupp 4.0.1-88: matches — the Failure carries a real exception instance, so `.os-error` answers.

### IO-11  rename and move                                          D:yes R:yes V:spec
`rename` returns True and fails with `X::IO::Rename` for a missing source
or, with `:createonly`, an existing target (os-error ":createonly specified
and destination exists"). `move` is copy then unlink: True, `X::IO::Move`
for a missing source, and `X::IO::Move` when moving a file onto itself,
which leaves the file intact. The sub forms behave the same.
```
say do { "r07".IO.spurt("r"); ("r07".IO.rename("r07b"), "r07".IO.e, "r07b".IO.e, "m07".IO.rename("z07").^name, "m07".IO.rename("z07").exception.^name, do { "t07".IO.spurt("t"); "r07b".IO.rename("t07", :createonly).^name ~ ":" ~ "r07b".IO.rename("t07", :createonly).exception.os-error }, "r07b".IO.move("v07"), "r07b".IO.e, "v07".IO.slurp, "m07".IO.move("z07").^name, "m07".IO.move("z07").exception.^name, "v07".IO.move("v07").exception.^name, "v07".IO.slurp, rename("v07", "w07"), copy("w07", "w07b"), move("w07b", "w07c"), "w07c".IO.e, "w07b".IO.e).join(" ") }
# rakudo 2026.08: True False True Failure X::IO::Rename Failure::createonly specified and destination exists True False r Failure X::IO::Move X::IO::Move r True True True True False
```
rakupp 4.0.1-88: matches.

### IO-12  symlink, link, readlink, chmod                           D:yes R:yes V:spec
`symlink` and `link` return True and fail (`X::IO::Symlink`, `X::IO::Link`)
when the name exists or the target is missing. A symlink answers `.l`
True and, if the target exists, `.e`/`.f` and reads through; `.readlink`
gives the stored target, `.resolve` follows it. A dangling symlink is
`.l` True, `.e` False and `.f` a Failure. `chmod` returns True and fails
with `X::IO::Chmod` on a missing path; the sub form returns the names it changed.
```
say do { "t08".IO.spurt("target"); ("t08".IO.symlink("l08"), "l08".IO.l, "l08".IO.e, "l08".IO.f, "l08".IO.slurp, "l08".IO.readlink.basename, "l08".IO.resolve.basename, "t08".IO.link("h08"), "h08".IO.slurp, "h08".IO.l, "t08".IO.symlink("l08").^name, "t08".IO.symlink("l08").exception.^name, "m08".IO.symlink("dang08"), "dang08".IO.e, "dang08".IO.l, "dang08".IO.f.^name, "t08".IO.chmod(0o600), "t08".IO.mode.Str, "m08".IO.chmod(0o600).^name, "m08".IO.chmod(0o600).exception.^name, chmod(0o644, "t08").raku, "t08".IO.mode.Str, "x08".IO.link("y08").exception.^name).join(" ") }
# rakudo 2026.08: True True True True target t08 t08 True target False Failure X::IO::Symlink True False True Failure True 0600 Failure X::IO::Chmod ["t08"] 0644 X::IO::Link
```
rakupp 4.0.1: differs — a symlink whose name exists throws instead of failing, so the line died there.

### IO-13  dir                                                      D:yes R:yes V:spec
Entries are IO::Paths whose Str is the directory as given plus the name
(`d09/a`; a trailing slash or a leading `./` is kept; inside the directory
itself the names are bare). `.` and `..` are excluded, dotfiles included,
the order is the filesystem's. `:test` smartmatches the bare name as a Str,
and a test that matches `.` and `..` brings them back. A missing directory
throws `X::IO::Dir`.
```
say do { "d09".IO.mkdir; "d09/b".IO.spurt("b"); "d09/a".IO.spurt("a"); "d09/.h".IO.spurt("h"); "d09/sub".IO.mkdir; (dir("d09").sort.map(*.Str).raku, dir("d09").head.^name, "d09".IO.dir.sort.map(*.Str).raku, "d09/".IO.dir.sort.map(*.Str).raku, "./d09".IO.dir.sort.map(*.Str).raku, dir("d09", :test(/^a/)).map(*.Str).raku, dir("d09", :test(*.starts-with("s"))).map(*.Str).raku, dir("d09", :test({ $_ eq "b" })).map(*.basename).raku, (try dir("m09")) // $!.^name, dir("d09").head.parent.Str, do { indir "d09", { dir.sort.map(*.Str).raku } }, dir("d09/sub").elems, "d09".IO.dir(:test(none <. ..>)).elems, dir("d09", :test(/./)).sort.map(*.Str).raku).join(" ") }
# rakudo 2026.08: ("d09/.h", "d09/a", "d09/b", "d09/sub").Seq IO::Path ("d09/.h", "d09/a", "d09/b", "d09/sub").Seq ("d09/.h", "d09/a", "d09/b", "d09/sub").Seq ("./d09/.h", "./d09/a", "./d09/b", "./d09/sub").Seq ("d09/a",).Seq ("d09/sub",).Seq ("b",).Seq X::IO::Dir d09 (".h", "a", "b", "sub").Seq 0 4 ("d09/.", "d09/..", "d09/.h", "d09/a", "d09/b", "d09/sub").Seq
```
rakupp 4.0.1: matches.

## D. Reading and writing whole files

### IO-14  spurt and slurp                                          D:partial R:partial V:quirk
`spurt` returns True; `:append` appends; a plain spurt overwrites; a
spurt with no data creates an empty file; `:createonly` on an existing
file is a Failure `X::AdHoc` ("Failed to open file …: File exists"); a Cool
is stringified, a Blob written raw. `slurp` gives a Str, or a `Buf[uint8]`
with `:bin`. `slurp` on a missing path or a directory **throws** `X::AdHoc`
("Failed to open file …"), as sub and as method. The docs say it fails;
Roast's `dies-ok` accepts either. Recorded as observed.
```
say do { (spurt("f10", "x"), slurp("f10").raku, spurt("f10", "y", :append), slurp("f10").raku, spurt("f10", "z"), slurp("f10").raku, spurt("f10", "q", :createonly).^name, spurt("f10", "q", :createonly).exception.^name, spurt("f10", "q", :createonly).exception.message, "n10".IO.spurt, "n10".IO.s, "n10".IO.slurp.raku, slurp("f10", :bin).^name, slurp("f10", :bin).raku, spurt("b10", Blob.new(65, 66)), slurp("b10").raku, spurt("c10", 42), slurp("c10").raku, (try slurp("m10")) // $!.^name, (try "m10".IO.slurp) // $!.^name ~ ":" ~ $!.message.substr(0, 19), (try slurp("d10dir".IO.mkdir)) // $!.^name).join(" ") }
# rakudo 2026.08: True "x" True "xy" True "z" Failure X::AdHoc Failed to open file …/f10: File exists True 0 "" Buf[uint8] Buf[uint8].new(122) True "AB" True "42" X::AdHoc X::AdHoc:Failed to open file X::AdHoc
```
rakupp 4.0.1-84: differs — a missing path throws `X::AdHoc` now; `:createonly` still fails with `X::IO::Exists`, `:bin` gives a `Blob`, and slurping a directory returns "".

### IO-15  Newlines and encodings                                   D:partial R:partial V:spec
Reading translates `\r\n` to `\n` (`\r` alone is kept); `:bin` and
`:!translate-nl` keep the bytes. `.lines` splits on the handle's `nl-in`
(`\n` and `\r\n` by default; `\r` alone is not a separator), `:!chomp`
keeps the terminator, `:nl-in` overrides. `.words`, `.comb`, `.split` on an
IO::Path read the **file**; `.split("\n")` keeps a trailing empty piece.
A utf16 spurt writes a BOM (`FF FE`) and slurp `:enc<utf16>` reads it back;
latin1 round-trips; slurping non-UTF-8 bytes as utf8 throws.
```
say do { spurt("nl11", "a\r\nb\rc\n"); (slurp("nl11").raku, slurp("nl11", :bin).elems, "nl11".IO.slurp(:!translate-nl).raku, "nl11".IO.lines.raku, "nl11".IO.lines(:!chomp).raku, "nl11".IO.lines(:nl-in("\r")).raku, "nl11".IO.words.raku, "nl11".IO.comb(2).raku, "nl11".IO.split("\n").raku, "nl11".IO.comb(/\w/).raku, spurt("u11", "é", :enc<utf16>), slurp("u11", :bin).list.raku, slurp("u11", :enc<utf16>).raku, spurt("l11", "é", :enc<latin1>).Bool, slurp("l11", :bin).elems, slurp("l11", :enc<latin1>).raku, (try slurp("l11")).^name).join(" ") }
# rakudo 2026.08: "a\nb\rc\n" 7 "a\r\nb\rc\n" ("a", "b\rc").Seq ("a\n", "b\rc\n").Seq ("a\nb", "c\n").Seq ("a", "b", "c").Seq ("a\n", "b\r", "c\n").Seq ("a", "b\rc", "").Seq ("a", "b", "c").Seq True (255, 254, 233, 0) "é" True 1 "é" Nil
```
rakupp 4.0.1: differs — `:!translate-nl`, `:!chomp` and `:nl-in` are ignored; `.words`, `.comb` and `.split` on an IO::Path operate on the path string, not the file; no BOM for utf16; invalid utf8 decodes silently.

## E. IO::Handle

### IO-16  Writing through a handle                                 D:partial R:partial V:spec
`print`, `say`, `put`, `printf`, `print-nl` and `write` return True; `say`
and `put` with several arguments join them without a separator; `.tell`
counts bytes written; `.gist` names the path and whether it is opened;
`close` is True and idempotent. On a closed handle `print` and `get`
throw `X::IO::Closed`, `native-descriptor` and `tell` throw `X::AdHoc`, and
`eof` is True.
```
say do { my $fh = open("w12", :w); ($fh.^name, $fh.opened, $fh.print("a"), $fh.say("b", "c"), $fh.put(1, 2), $fh.printf("%03d", 7), $fh.print-nl, $fh.write(Blob.new(88)), $fh.nl-out.raku, $fh.encoding, $fh.path.^name, $fh.Str, $fh.gist, $fh.t, $fh.native-descriptor.^name, $fh.tell, $fh.close, $fh.opened, $fh.close, $fh.gist, slurp("w12").raku, (try $fh.print("x")) // $!.^name, (try $fh.get) // $!.^name, (try $fh.native-descriptor) // $!.^name, (try $fh.tell) // $!.^name, $fh.eof).join(" ") }
# rakudo 2026.08: IO::Handle True True True True True True True "\n" utf8 IO::Path w12 IO::Handle<"w12".IO>(opened) False Int 12 True False True IO::Handle<"w12".IO>(closed) "abc\n12\n007\nX" X::IO::Closed X::IO::Closed X::AdHoc X::AdHoc True
```
rakupp 4.0.1-84: matches — all 25 fields, including the closed-handle `X::IO::Closed`/`X::AdHoc` split.

### IO-17  Reading lines                                            D:yes R:yes V:spec
`get` returns the next line chomped and Nil at the end; `.tell` after a
`get` is the byte offset consumed; `getc` reads one character; `.eof`
turns True once the last line is consumed. `lines(n)` leaves the handle
open and positioned after the n-th line unless `:close`. `lines(:!chomp)`
keeps terminators as translated (`"l2\n"` for an original `\r\n`), and
`:nl-in("\r")` finds nothing because `\r\n` was already translated on
decoding. The `lines` sub applied to a **Str** splits the string; only an
IO::Path or a handle reads a file.
```
say do { spurt("r13", "l1\nl2\r\nl3"); my $fh = open("r13"); ($fh.get.raku, $fh.tell, $fh.getc.raku, $fh.get.raku, $fh.eof, $fh.get.raku, $fh.eof, $fh.get.raku, $fh.close, open("r13").lines.raku, open("r13").lines(2).raku, do { my $h = open("r13"); my @l = $h.lines(2); @l.raku ~ " " ~ $h.opened ~ " " ~ $h.get.raku }, do { my $h = open("r13"); my @l = $h.lines(2, :close); $h.opened }, open("r13").lines(:!chomp).raku, open("r13", :!chomp).lines.raku, open("r13", :nl-in("\r")).lines.raku, open("r13").words.raku, open("r13").comb(3).raku, open("r13").split("\n").raku, open("r13").slurp.raku, lines("r13".IO).raku, lines("a\nb").raku, words("a b").raku, "r13".IO.lines(1).raku).join(" ") }
# rakudo 2026.08: "l1" 3 "l" "2" False "l3" True Nil True ("l1", "l2", "l3").Seq ("l1", "l2").Seq ["l1", "l2"] True "l3" False ("l1", "l2\n", "l3").Seq ("l1\n", "l2\n", "l3").Seq ("l1\nl2\nl3",).Seq ("l1", "l2", "l3").Seq ("l1\n", "l2\n", "l3").Seq ("l1", "l2", "l3").Seq "l1\nl2\nl3" ("l1", "l2", "l3").Seq ("a", "b").Seq ("a", "b").Seq ("l1",).Seq
```
rakupp 4.0.1-84: differs — measured now: `.lines` answers a List where Rakudo answers a Seq, `lines($n)` ignores the limit, `:!chomp`/`:nl-in` are ignored, and `.words`/`.comb`/`.split` on a handle read the path string.

### IO-18  Binary reads, seek, tell, Supply                          D:yes R:yes V:spec
`read(n)` gives a `Buf[uint8]` of at most n bytes and an empty one at the
end; `readchars(n)` gives characters from the current position; `seek`
takes `SeekFromBeginning`, `SeekFromCurrent` (which accounts for bytes the
decoder has read ahead) and `SeekFromEnd`, and returns True; `.slurp`
reads from the current position; `.Supply(:size(n))` emits chunks of n
characters. A `:bin` handle has `encoding` Nil and its `get` and `lines`
throw `X::IO::BinaryMode`; `.slurp` on it, or `.slurp(:bin)` on a text
handle, gives a Buf.
```
say do { my $fh = open("r13"); ($fh.read(3).raku, $fh.tell, $fh.readchars(2).raku, $fh.tell, $fh.seek(0, SeekFromBeginning), $fh.tell, $fh.readchars(1), $fh.seek(2, SeekFromCurrent), $fh.get.raku, $fh.seek(-2, SeekFromEnd), $fh.slurp.raku, $fh.eof, $fh.seek(0), $fh.read(100).elems, $fh.read(5).elems, $fh.read(5).^name, $fh.close, open("r13").Supply(:size(4)).list.raku, open("r13", :bin).read(2).raku, open("r13", :bin).encoding.raku, (try open("r13", :bin).get) // $!.^name, (try open("r13", :bin).lines.eager) // $!.^name, open("r13", :bin).slurp.^name, open("r13").slurp(:bin).^name, open("r13").readchars(100).chars).join(" ") }
# rakudo 2026.08: Buf[uint8].new(108,49,10) 3 "l2" 6 True 0 l True "l2" True "l3" True True 9 0 Buf[uint8] True ("l1\nl", "2\nl3") Buf[uint8].new(108,49) Nil X::IO::BinaryMode X::IO::BinaryMode Buf[uint8] Buf[uint8] 8
```
rakupp 4.0.1: differs — `readchars` ignores the position (`"l1"` after `read(3)`), `.slurp` on a positioned handle returns the whole file untranslated and leaves `eof` False, `.Supply` returns a hash-like value, a `:bin` handle reports `utf8` and lets `get` and `lines` through, and `.slurp` on it or `.slurp(:bin)` gives a Str.

### IO-19  open: modes, failures, encodings                          D:partial R:partial V:spec
Missing file: Failure `X::AdHoc` ("Failed to open file …"); a directory:
Failure `X::IO::Directory` (`.trying` "open"); `:x` on an existing file:
Failure; `:x` creates; `:a` appends; `:w` truncates; `:rw` reads and
writes; the long form `:mode<wo>, :create, :append` equals `:a`. Writing
to a read-only handle throws `X::AdHoc`. `:bin` with `:enc` throws
`X::IO::BinaryAndEncoding`; `:bin` reports encoding Nil; names are
normalized (`latin1` → `iso-8859-1`, `utf-8` → `utf8`); an unknown one
throws `X::Encoding::Unknown`. `:update` opens read-write without
creating. `.encoding($new)` switches on the fly; `.encoding("bin")` enters
binary mode and returns Nil.
```
say do { (open("m14").^name, open("m14").exception.^name, open("m14").exception.message.substr(0, 19), open("d14".IO.mkdir).^name, open("d14").exception.^name, open("d14").exception.trying, do { spurt("x14", "old"); open("x14", :x).^name ~ ":" ~ open("x14", :x).exception.^name }, do { open("n14", :x).close; "n14".IO.e }, do { open("x14", :a).spurt("+new", :close); slurp("x14").raku }, do { open("x14", :w).close; slurp("x14").raku }, do { my $h = open("x14", :rw); $h.print("ab"); $h.seek(0); $h.get.raku ~ " " ~ $h.close }, do { my $h = open("x14", :mode<wo>, :create, :append); $h.print("c"); $h.close; slurp("x14").raku }, do { my $h = open("x14", :r); (try $h.print("z")) // $!.^name }, (try open("x14", :bin, :enc<utf8>)) // $!.^name, open("x14", :bin).encoding.raku, open("x14").encoding, open("x14", :enc<latin1>).encoding, open("x14", :enc<utf-8>).encoding, (try open("x14", :enc<nope>)) // $!.^name, open("x14", :nl-out("!")).nl-out.raku, open("x14", :chomp(False)).chomp, open("x14", :update).^name, do { my $h = open("x14", :rw); $h.encoding("latin1") ~ " " ~ $h.encoding ~ " " ~ $h.encoding("bin").raku ~ " " ~ $h.encoding.raku }).join(" ") }
# rakudo 2026.08: Failure X::AdHoc Failed to open file Failure X::IO::Directory open Failure:X::AdHoc True "old+new" "" "ab" True "abc" X::AdHoc X::IO::BinaryAndEncoding Nil utf8 iso-8859-1 utf8 X::Encoding::Unknown "!" False IO::Handle iso-8859-1 iso-8859-1 Nil Nil
```
rakupp 4.0.1-84: differs — `:x` is a Failure now, and the encoding names canonicalize; measured beyond that point for the first time.

### IO-20  prompt and standard input                                D:yes R:yes V:spec
`prompt($msg)` prints and flushes the message, then returns the next line
chomped, passed through `val`, so a numeric line comes back as an
allomorph; at end of input it returns Nil. After that `$*IN.get` is Nil,
`.eof` True, `.lines` an empty Seq and `.slurp` `""`. With no file
arguments `$*ARGFILES` is `$*IN` itself, an IO::Handle on `<STDIN>`.
`$*IN.lines(1)` reads one line and leaves the rest for `.words`.
```
say do { (prompt("p> ").raku, prompt.^name, prompt.raku, prompt.raku, $*IN.get.raku, $*IN.eof, $*IN.lines.raku, $*IN.slurp.raku).join(" ") }
# rakudo 2026.08: p> "line1" IntStr "last" Nil Nil True ().Seq ""
say do { ($*IN.get.raku, get.raku, $*IN.getc.raku, $*IN.readchars(3).raku, lines().raku, $*IN.get.raku, $*ARGFILES.^name, $*ARGFILES.path.Str, $*ARGFILES.eof, $*IN.opened, $*IN.encoding, $*IN.eof).join(" ") }
# rakudo 2026.08: "line1" "42" "l" "ast" ().Seq Nil IO::Handle <STDIN> True True utf8 True
say do { ($*IN.lines(1).raku, $*IN.words.raku, $*IN.lines.raku, slurp().raku, $*IN.eof).join(" ") }
# rakudo 2026.08: ("line1",).Seq ("42", "last").Seq ().Seq "" True
```
rakupp 4.0.1: differs — `$*IN.lines(1)` ignores the limit and returns every line, so `.words` then sees nothing; `.lines` results are Lists (`()`) rather than Seqs; `$*IN.opened` is missing.

## F. Directories and the current directory

### IO-21  indir, chdir, $*CWD                                      D:yes R:yes V:spec
`indir` runs the block with `$*CWD` set (absolute) and returns the block's
value; a missing directory is a Failure `X::IO::Chdir` (os-error "does not
exist"), a file "is not a directory". `chdir` returns the new `$*CWD`, an
absolute IO::Path, and relative paths resolve against it from then on;
`chdir()` with no argument is `X::Multi::NoMatch`. `IO::Path.chdir($rel)`
is textual concatenation with a `:d` existence test by default (`:!d`
skips it); `..` and absolute arguments are resolved textually.
```
say do { "d15".IO.mkdir; (indir("d15", { $*CWD.basename }), indir("d15", { "f".IO.absolute.ends-with("d15/f") }), $*CWD.basename ne "d15", indir("m15", {;}).^name, indir("m15", {;}).exception.^name, indir("m15", {;}).exception.os-error, do { spurt("f15", "x"); indir("f15", {;}).exception.os-error }, indir("d15", { 42 }), chdir("d15").^name, $*CWD.basename, $*CWD.is-absolute, "q".IO.absolute.ends-with("d15/q"), chdir("..").basename ne "d15", chdir("m15").^name, chdir("m15").exception.^name, (try chdir()) // $!.^name, "d15".IO.chdir("sub").^name, "d15".IO.chdir("sub").exception.os-error, "d15".IO.chdir("sub", :!d).^name, "d15".IO.chdir("sub", :!d).Str.ends-with("d15/sub"), "d15".IO.chdir("..").Str.ends-with("rakudo") || "d15".IO.chdir("..").Str.ends-with("rakupp"), "/a/b".IO.chdir("../c", :!d).Str, "/a/b".IO.chdir("/x", :!d).Str, $*CWD.^name, $*TMPDIR.^name, $*TMPDIR.d, $*HOME.^name, $*SPEC.^name, $*SPEC.tmpdir.^name, $*SPEC.path.^name, $*SPEC.path.elems > 0).join(" ") }
# rakudo 2026.08: d15 True True Failure X::IO::Chdir does not exist is not a directory 42 IO::Path d15 True True True Failure X::IO::Chdir X::Multi::NoMatch Failure does not exist IO::Path True True /a/c /x IO::Path IO::Path True IO::Path IO::Spec::Unix IO::Path Seq True
```
rakupp 4.0.1: differs — `indir` on a missing directory throws instead of failing, so the line died there.

### IO-22  IO::Spec::Unix                                           D:yes R:yes V:spec
`canonpath(:parent)` also removes `x/..` pairs; `catdir("a/", "/b")`
collapses the doubled separator; `rel2abs` does not resolve `..`;
`abs2rel` of a path against itself is `.`; `split` returns an
`IO::Path::Parts`; `basename("/a/b/")` is `""`.
```
say do { my $s = IO::Spec::Unix; ($s.canonpath("a//b/./c/"), $s.canonpath("/../a"), $s.canonpath("a/../b"), $s.canonpath("a/../b", :parent), $s.canonpath("./"), $s.canonpath("").raku, $s.canonpath("/"), $s.canonpath("/.."), $s.canonpath("./a/"), $s.canonpath("a/.."), $s.canonpath("a/..", :parent), $s.canonpath("../a", :parent), $s.catdir("a", "b"), $s.catdir().raku, $s.catdir("a/", "/b"), $s.catpath("", "a", "b"), $s.catpath("", "/", "b"), $s.catpath("", "", "b"), $s.catpath("", "a/", "b"), $s.splitdir("/a/b").raku, $s.splitdir("a").raku, $s.splitdir("").raku, $s.splitpath("/a/b.txt").raku, $s.splitpath("b.txt").raku, $s.splitpath("/a/b/", :nofile).raku, $s.join("", "a", "b"), $s.join("", "/", "/"), $s.join("", ".", "b"), $s.join("", "a/", "b"), $s.join("", "", "b"), $s.split("/a/b/").raku, $s.split("/").raku, $s.split("a").raku, $s.split("/a").raku, $s.split("a/b//").raku, $s.is-absolute("/a"), $s.is-absolute("a"), $s.rel2abs("a", "/x"), $s.rel2abs("/a", "/x"), $s.rel2abs("a/../b", "/x"), $s.abs2rel("/x/a/b", "/x"), $s.abs2rel("/x", "/x"), $s.abs2rel("/y", "/x/a"), $s.abs2rel("/x/a", "/"), $s.curdir, $s.updir, $s.rootdir, $s.devnull, $s.dir-sep, $s.basename("/a/b"), $s.basename("/a/b/").raku, $s.extension("a.b.c"), $s.extension("a").raku, $s.tmpdir.^name).join(" ") }
# rakudo 2026.08: a/b/c /a a/../b b . "" / / a a/.. . ../a a/b "" a/b a/b /b b a/b ("", "a", "b") ("a",) ("",) ("", "/a/", "b.txt") ("", "", "b.txt") ("", "/a/b/", "") a/b / b a/b b IO::Path::Parts.new("","/a","b") IO::Path::Parts.new("","/","/") IO::Path::Parts.new("",".","a") IO::Path::Parts.new("","/","a") IO::Path::Parts.new("","a","b") True False /x/a /a /x/a/../b a/b . ../../y x/a . .. / /dev/null / b "" c "" IO::Path
```
rakupp 4.0.1: matches.

## G. Output routines, unopened handles, pipes, CatHandle

### IO-23  say, put, print, printf on a handle                       D:partial R:yes V:spec
`say` prints gists joined without separator plus `nl-out`; `put` prints
Strs; `print` adds nothing; bare `say`/`put` print the terminator alone.
`say` of a Junction prints the junction's gist, while `put` and `print`
autothread over it. Assigning `nl-out` changes what `say` appends.
```
say do { my $fh = open("o16", :w); $fh.say(1, 2); $fh.put(1, 2); $fh.print(1, 2); $fh.print-nl; $fh.say(); $fh.put(); $fh.say((1, 2)); $fh.put((1, 2)); $fh.say(any(1, 2)); $fh.put(any(1, 2)); $fh.print(any(1, 2)); $fh.print-nl; $fh.printf("%s-%s", 1, 2); $fh.print-nl; $fh.nl-out = "!"; $fh.say("z"); $fh.close; slurp("o16").raku }
# rakudo 2026.08: "12\n12\n12\n\n\n(1 2)\n1 2\nany(1, 2)\n1\n2\n12\n1-2\nz!"
```
rakupp 4.0.1: differs — `say` of a Junction autothreads (`1\n2\n`) and the `nl-out` assignment is ignored (`z\n`).

### IO-24  An unopened handle, locks, flush                          D:partial R:partial V:spec
`IO::Handle.new(:path)` is closed: `.opened` False, gist `(closed)`, `get`
and `print` throw `X::IO::Closed`, `close` is True, `eof` True, encoding
already `"utf8"` (Nil with `:bin`; `:bin` plus `:encoding` throws
`X::IO::BinaryAndEncoding`). On an open handle `lock`, `unlock` and
`flush` return True and `do-not-close-automatically` True; `flush` on a
closed handle fails with `X::IO::Flush`; an exclusive lock on a read-only
handle fails with `X::IO::Lock`, a shared one works.
```
say do { my $h = IO::Handle.new(:path("x17")); ($h.opened, $h.gist, (try $h.get) // $!.^name, (try $h.print("a")) // $!.^name, $h.close, $h.path.^name, $h.encoding.raku, $h.chomp, $h.nl-in.raku, $h.nl-out.raku, $h.eof, $h.Str, IO::Handle.new(:path("x17"), :bin).encoding.raku, (try IO::Handle.new(:path("x17"), :bin, :encoding<utf8>)) // $!.^name, do { my $w = open("x17", :w); $w.do-not-close-automatically ~ " " ~ $w.lock ~ " " ~ $w.unlock ~ " " ~ $w.flush ~ " " ~ $w.close ~ " " ~ ((try $w.flush) // $!.^name) }, do { my $ro = open("x17"); my $f = $ro.lock; $f.^name ~ ":" ~ $f.exception.^name ~ " " ~ $ro.lock(:shared) ~ " " ~ $ro.close }).join(" ") }
# rakudo 2026.08: False IO::Handle<"x17".IO>(closed) X::IO::Closed X::IO::Closed True IO::Path "utf8" True $("\n", "\r\n") "\n" True x17 Nil X::IO::BinaryAndEncoding True True True True True X::IO::Flush Failure:X::IO::Lock True True
```
rakupp 4.0.1-84: matches — an unopened handle, the lock refusal on a read-only handle, and `X::IO::Flush`.

### IO-25  IO::Pipe                                                 D:yes R:yes V:spec
`run(:out).out` is an IO::Pipe: `lines`, `get` (Nil at the end), `eof`,
`slurp(:close)`; `close` returns the Proc; `.path` and `.IO` are the
IO::Path type object; `.t` is False; `.proc` is the Proc; encoding utf8,
`:bin` gives Bufs; printing to a read pipe throws `X::AdHoc`; a `:in`
pipe is written with `print` and closed; gist `IO::Pipe<(IO)>`.
```
say do { my $p = run("printf", "hi\\nthere\\n", :out); ($p.out.^name, $p.out.lines.raku, $p.out.get.raku, $p.out.eof, $p.out.close.^name, $p.out.opened, run("printf", "x", :out).out.slurp(:close).raku, run("printf", "x", :out).out.path.raku, run("printf", "x", :out).out.IO.raku, run("printf", "x", :out).out.t, run("printf", "x", :out).out.proc.^name, run("printf", "x", :out).out.encoding, run("printf", "x", :out, :bin).out.read(1).raku, (try run("printf", "x", :out).out.print("y")) // $!.^name, do { my $q = run("cat", :in, :out); $q.in.print("via"); $q.in.close; $q.out.slurp.raku }, (try run("printf", "x", :out).out.native-descriptor.^name), run("printf", "x", :out).out.gist.substr(0, 14)).join(" ") }
# rakudo 2026.08: IO::Pipe ("hi", "there").Seq Nil True Proc False "x" IO::Path IO::Path False Proc utf8 Buf[uint8].new(120) X::AdHoc "via" Int IO::Pipe<(IO)>
```
rakupp 4.0.1-84: differs — the pipe is an `IO::Pipe` with `.proc` now; `$p.out` still rebuilds a fresh handle per call (so `.close` then `.opened` asks a different object), `.lines` does not advance the cursor, and a `:bin` pipe is not binary.

### IO-26  IO::CatHandle                                            D:yes R:yes V:spec
Reads its sources in turn as one stream: `lines`, `get`, `slurp`, `words`,
`read`, `readchars` cross the file boundary; `.path` is the current
source's IO::Path and Nil before the first read and after the last;
`.Str` is the current path; `.eof` is True at the end; an empty CatHandle
gives Nil and eof True; `next-handle` skips to the next source and returns
its IO::Handle; `:on-switch` runs for every source and once more with an
undefined argument at the end; a missing source makes `.lines` a Seq that
throws `X::AdHoc` on reification.
```
say do { spurt("c18a", "a1\na2\n"); spurt("c18b", "b1"); my $c = IO::CatHandle.new("c18a", "c18b"); ($c.^name, $c.lines.raku, do { my $d = IO::CatHandle.new("c18a", "c18b"); ($d.get, $d.path.^name, $d.get, $d.get, $d.path.Str, $d.get.raku, $d.eof, $d.path.raku).raku }, IO::CatHandle.new("c18a", "c18b").slurp.raku, IO::CatHandle.new("c18a", "c18b").words.raku, IO::CatHandle.new().get.raku, IO::CatHandle.new().eof, IO::CatHandle.new("c18a", "c18b").Str, do { my $n = IO::CatHandle.new("c18a", "c18b"); $n.next-handle.^name ~ " " ~ $n.get }, do { my @sw; my $e = IO::CatHandle.new("c18a", "c18b", :on-switch({ @sw.push(.defined ?? .path.Str !! "end") })); $e.slurp; @sw.raku }, IO::CatHandle.new("c18a", "m18").lines.^name, (try IO::CatHandle.new("c18a", "m18").lines.eager) // $!.^name, IO::CatHandle.new("c18a").opened, IO::CatHandle.new("c18a", "c18b").lines(2).raku, IO::CatHandle.new("c18a", "c18b").read(3).raku, IO::CatHandle.new("c18a", "c18b").readchars(4).raku, IO::CatHandle.new("c18a", "c18b").encoding).join(" ") }
# rakudo 2026.08: IO::CatHandle ("a1", "a2", "b1").Seq ("a1", "IO::Path", "a2", "b1", "c18b", "Nil", Bool::True, "Nil") "a1\na2\nb1" ("a1", "a2", "b1").Seq Nil True c18a IO::Handle b1 ["c18a", "c18b", "end"] Seq X::AdHoc True ("a1", "a2").Seq Buf[uint8].new(97,49,10) "a1\na" utf8
```
rakupp 4.0.1: differs — its `CatHandle` has no `get`, so the line died there.

## Counts

| | items |
|---|---|
| total | 26 |
| not fully stated by docs (D:yes) nor asserted by Roast (R:yes) | 7 |
| Rakudo bugs (do not imitate) | 0 |
| quirks (recorded, step two decides) | 1 — IO-14 `slurp` throws where the docs say it fails |
| rakupp 4.0.1 differs (before implementation) | 24 |
| rakupp 4.0.1 matches (before implementation) | 2 — IO-13, IO-22 |
| **rakupp 4.0.1-88 matches** | **12** — IO-01…05, IO-08, IO-10, IO-11, IO-13, IO-16, IO-22, IO-24 |
| rakupp 4.0.1-88 differs | 14 |

Implementation began 2026-09-20 from this sheet alone, with the Homebrew
Rakudo `v2026.08` as oracle and Roast as the gate; it is PARTIAL, and each
item's own line says where it stands.

What the first pass did. The handle type was the gate: `open`, the standard
handles and a child's pipe all answered `FileHandle`, a type Raku does not
have, and nine probe lines died at their first handle method. Handles now
report `IO::Handle` (a pipe `IO::Pipe`), carry `opened`, `proc` and
`do-not-close-automatically`, and a closed handle refuses reads and writes
with `X::IO::Closed` while `tell`/`seek`/`native-descriptor` raise
`X::AdHoc` — which made the rest of those lines measurable for the first
time. Alongside: `.absolute` is canonical, so path comparison works and
smartmatch against an IO::Path compares absolutes; `.volume` and `.parts`
exist; `cleanup` drops a `..` under the root; `IO::Path.new` refuses an
empty path and accepts `:basename`/`:dirname`; `eqv` on two paths compares
the `:CWD`; encoding names canonicalize and an unknown one is
`X::Encoding::Unknown`; and `:x` on an existing file is a Failure rather
than a throw. A refused `open` and a missing-file `slurp` keep the name
`X::IO::Open`, which IS-A `X::AdHoc`, so the `when X::AdHoc` written
against Rakudo still fires and `.payload` still reads the message — the
name only adds which call failed. The JS lane names it the same.

Still open, for the next pass: operations Rakudo turns into Failures still
throw (`indir` on a missing directory, a second `symlink`); Failure
exceptions lack `.path` and `.os-error`, and a file test on a missing path
answers a plain False instead of an `X::IO::DoesNotExist` Failure;
`.lines` answers a List where Rakudo answers a Seq, and `lines($n)`
ignores the limit; `.words`, `.comb` and `.split` on an IO::Path work on
the path string instead of the file; `:bin` handles are not binary and
`readchars`/`slurp` ignore the position; newline translation, `:!chomp`,
`:nl-in` and the utf16 BOM are missing; `say` of a Junction autothreads
and `nl-out` cannot be assigned; a pipe rebuilds per `.out` call; the
CatHandle has no `get`. One field is recorded as a deliberate divergence:
`"a".IO === "a".IO` is True here, because Rakudo's IO::Path WHICH carries
the object address and a path in this engine is a copied Str value with
nowhere to keep one (IO-06).

## Method (how this sheet was produced)

As for [Supply.md](Supply.md), with two additions: each engine ran in its
own fresh sandbox directory under the scratchpad, so file operations could
not interfere, and every probe received `line1\n42\nlast` on standard
input. Traps met: a Failure inspected with `.^name` or `.exception` alone
is still unhandled and warns at exit, so the `F` helper in IO-09 calls
`.so` first; `unlink` on a missing path returning True was found because
the guard that expected a Failure met a Bool; `"".IO` and
`"m10".IO.slurp` throw and needed `try`; a smartmatch with a Str on the
right tests string equality, not `IO::Path.ACCEPTS` (IO-06 got a second
probe for that). D flags from `doc/Type/IO/*.rakudoc`,
`doc/Type/independent-routines.rakudoc` and
`doc/Language/{io,io-guide}.rakudoc`; R flags from `S32-io/*.t` and
`S16-io/*.t`.
