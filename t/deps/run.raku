# The missing-modules gate (DEPS-PLAN D0) — network-free: a local fixture
# index and locally built archives stand in for the ecosystem
# (RAKUPP_INSTALL_INDEX accepts a file path, and archive paths in a file index
# resolve beside it), and every run gets a HOME of its own, so no check reads
# or writes the user's store. What it pins:
#
#   - without --auto nothing is fetched: the error keeps its line 1, lists
#     every module the program is missing, and gives the two commands that
#     install them, spelled with the name the engine was invoked by (D2)
#   - with --auto a missing module is fetched and the program runs; stdout is
#     the program's alone, stderr says what was installed (D2)
#   - the next run needs neither --auto nor an index (D2)
#   - a name in no index fails, line 1 byte for byte (D1/D2)
#   - `require` never fetches; neither do --sandbox or RAKUPP_DEPS=0 (D2)
#   - the typo guard, and a pinned `use` passing it (D1)
#   - an unreachable index is tried once, not once per process (D1)
#   - a script keeps the versions its first run chose (D3)
#   - --auto --bundle fetches at compile time and the binary carries the
#     module; --bundle alone says how (D4)
#
# Before the feature every "fetches" check fails and every "stays fatal"
# check passes: that is what shows the checks can fail.
#
#   build/rakupp t/deps/run.raku      (POSIX: tar, a sha1 tool, /bin/sh)

use Data::Native;

my $EXE = $*EXECUTABLE.absolute;
my $tmp = $*TMPDIR.add("deps-gate-$*PID");
$tmp.mkdir;
LEAVE { run "rm", "-rf", $tmp.Str unless %*ENV<DEPS_GATE_KEEP> }

my $ok = 0;
my $bad = 0;
sub check(Bool() $cond, $desc) {
    if $cond { $ok++;  say "ok - $desc" }
    else     { $bad++; say "NOT OK - $desc" }
}

sub sha1-of(Str $path) {
    for ('shasum', '-a', '1'), ('sha1sum',), ('openssl', 'sha1', '-r') -> @tool {
        my $p = try run |@tool, $path, :out, :err;
        next unless $p && $p.exitcode == 0;
        return ~$0 if $p.out.slurp(:close) ~~ / ( <[0..9 a..f]> ** 40 ) /;
    }
    die "no sha1 tool";
}

# ---- fixture distributions --------------------------------------------------
# Each provides one module whose `hello` names the dist and version, so a check
# can tell which copy loaded. :uses puts a `need` in the module (a runtime dep
# the META declares; `need`, so its `hello` is not imported over this one's); :test-uses puts one in the test file only (a dep the
# META forgets — the shape a suite must fail on).
my $arch = $tmp.add('archives');
$arch.mkdir;
sub make-dist(Str $name, Str $version, :@uses, :@test-uses, Str :$auth = 'test:deps') {
    my $safe  = $name.subst('::', '-', :g);
    my $droot = $tmp.add("build-$safe-$version");
    my $rel   = "lib/{$name.split('::').join('/')}.rakumod";
    $droot.add($rel).parent.mkdir;
    $droot.add($rel).spurt:
        @uses.map({ "need $_;\n" }).join
        ~ "unit module $name;\n"
        ~ "our sub hello() is export \{ '$name $version' \}\n";
    my $deps = @uses.map({ "\"$_\"" }).join(', ');
    $droot.add('META6.json').spurt: qq:to/END/;
        \{ "name": "$name", "version": "$version", "auth": "$auth",
           "provides": \{ "$name": "$rel" \}, "depends": [$deps] \}
        END
    $droot.add('t').mkdir;
    $droot.add('t/01-load.rakutest').spurt:
        @test-uses.map({ "use $_;\n" }).join
        ~ "use $name;\nprint \"1..1\\nok 1\\n\";\n";
    my $tar = $arch.add("$safe-$version.tar.gz").Str;
    my $p = run 'tar', '-czf', $tar, '-C', $tmp.Str, "build-$safe-$version", :err;
    die "tar failed" if $p.exitcode != 0;
    my $sha = sha1-of($tar);
    $tar.IO.rename($arch.add("$sha.tar.gz"));
    { :$name, :$version, :$auth, :$rel, :@uses, path => "$sha.tar.gz" }
}

sub entry(%d) {
    my $deps = %d<uses>.map({ "\"$_\"" }).join(', ');
    qq[\{ "name": "%d<name>", "version": "%d<version>", "auth": "%d<auth>",
          "dist": "{%d<name>}:ver<{%d<version>}>:auth<{%d<auth>}>",
          "provides": \{ "%d<name>": "%d<rel>" \},
          "depends": [$deps], "path": "%d<path>" \}]
}

my %hello1  = make-dist('Deps::Hello', '1.0');
my %hello2  = make-dist('Deps::Hello', '2.0');
my %uses    = make-dist('Deps::Uses', '0.1', :uses(['Deps::Hello']));
my %other   = make-dist('Deps::Other', '0.1');
my %opt     = make-dist('Deps::Optional', '0.1');
my %popular = make-dist('Deps::Popular', '1.0');
my %populer = make-dist('Deps::Populer', '0.1');   # one letter from Deps::Popular
my %sloppy  = make-dist('Deps::Sloppy', '0.1', :test-uses(['Deps::Other']));
my %late    = make-dist('Deps::Late', '0.1');
# Deps::Popular's standing: ten dists that depend on it. Index entries only —
# nothing ever fetches them, so their archives need not exist.
my @fans = (1..10).map(-> $i {
    qq[\{ "name": "Deps::Fan$i", "version": "0.1", "auth": "test:deps",
          "dist": "Deps::Fan{$i}:ver<0.1>:auth<test:deps>",
          "provides": \{ "Deps::Fan$i": "lib/Deps/Fan$i.rakumod" \},
          "depends": ["Deps::Popular"], "path": "missing-fan-$i.tar.gz" \}]
});

# index 1: Deps::Hello 1.0 only; index 2 adds 2.0 (the pin checks)
my @base = (%uses, %other, %opt, %popular, %populer, %sloppy, %late).map(&entry);
$arch.add('index.json').spurt:  '[ ' ~ (entry(%hello1), |@base, |@fans).join(",\n  ") ~ ' ]';
$arch.add('index2.json').spurt: '[ ' ~ (entry(%hello1), entry(%hello2), |@base, |@fans).join(",\n  ") ~ ' ]';
my $INDEX  = $arch.add('index.json').Str;
my $INDEX2 = $arch.add('index2.json').Str;

# ---- running the engine -----------------------------------------------------
# Every run: its own HOME unless one is passed, no RAKULIB, RAKUPP_DEPS unset,
# cwd a directory with no lib/ in it.
my $work = $tmp.add('work');
$work.mkdir;
my $homes = 0;
sub fresh-home(--> Str) {
    my $h = $tmp.add('home-' ~ ++$homes);
    $h.mkdir;
    $h.Str
}
sub rk(*@args, Str :$home!, Str :$index = $INDEX, :%env, Str :$cwd = $work.Str) {
    my %e = HOME => $home, RAKULIB => '', RAKUPP_DEPS => '',
            RAKUPP_INSTALL_INDEX => $index, RAKUPP_INSTALL_REA_INDEX => '', |%env;
    my $p = run 'env', |%e.map({ "{.key}={.value}" }), $EXE, |@args, :out, :err, :$cwd;
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    { exit => (try $p.exitcode) // 1, :$out, :$err }
}
sub store-has(Str $home, Str $name, Str $version? --> Bool) {
    my $d = $home.IO.add('.raku/dist');
    return False unless $d.d;
    ?$d.dir.first(-> $f {
        my %m = (try from-json($f.slurp)) // {};
        (%m<name> // '') eq $name && (!$version.defined || (%m<version> // %m<ver> // '') eq $version)
    })
}
sub installed-lines(Str $err) { $err.lines.grep(*.starts-with('installed ')).List }

$work.add('prog.raku').spurt("use Deps::Hello;\nprint hello();\n");
$work.add('two.raku').spurt("need Deps::Uses;\nneed Deps::Other;\nprint Deps::Uses::hello(), '|', Deps::Other::hello();\n");

# ---- D2: without --auto, nothing is fetched and the error says what to do --
my $h0 = fresh-home;
my %plain = rk('two.raku', :home($h0));
check %plain<exit> != 0 && !store-has($h0, 'Deps::Uses') && !store-has($h0, 'Deps::Other'),
      'D2: without --auto a missing module stays an error and nothing is installed';
check %plain<err>.lines[0] eq 'Could not find Deps::Uses in:', 'D2: …line 1 is the usual message';
check %plain<err>.contains('two.raku uses 2 modules that are not installed: Deps::Uses, Deps::Other'),
      'D2: …every missing module is listed, not only the first';
check %plain<err>.contains('to install them:          rakupp install two.raku'),
      'D2: …with the command that installs them';
check %plain<err>.contains('to install them and run:  rakupp --auto two.raku'),
      'D2: …and the one that installs them and runs';
check !%plain<err>.contains('ecosystem index'), 'D2: …and the index is not consulted';
my %one = rk('-e', 'use Deps::Hello; try require Deps::Optional', :home($h0));
check %one<err>.contains('The program uses a module that is not installed: Deps::Hello')
      && %one<err>.contains('to install it:          rakupp install Deps::Hello')
      && %one<err>.contains("to install it and run:  rakupp --auto -e '…'"),
      'D2: -e: one module, by name, and a `require`d one is not listed';
# the hint names the engine the way it was invoked
my $alias = $tmp.add('raku');
$EXE.IO.symlink($alias) unless $alias.e;   # (the link is the ARGUMENT: $target.symlink($link))
my $pa = run 'env', "HOME=$h0", 'RAKULIB=', "RAKUPP_INSTALL_INDEX=$INDEX", $alias.Str, 'prog.raku',
             :out, :err, :cwd($work.Str);
$pa.out.slurp(:close);
my $pa-err = $pa.err.slurp(:close);
check $pa-err.contains(' raku install prog.raku') && $pa-err.contains(' raku --auto prog.raku')
      && !$pa-err.contains('rakupp install') && !$pa-err.contains('rakupp --auto'),
      'D2: run as `raku`, the hint says `raku`';

# ---- D2: --auto fetches, the program runs ----------------------------------
my $h1 = fresh-home;
my %first = rk('--auto', 'prog.raku', :home($h1));
check %first<exit> == 0, 'D2: with --auto a program using a module that is not installed runs';
check %first<out> eq 'Deps::Hello 1.0', 'D2: …stdout is the program\'s output and nothing else';
check installed-lines(%first<err>) eqv ('installed Deps::Hello:ver<1.0>:auth<test:deps> for prog.raku',),
      'D2: …stderr has one line naming the full identity and the program';
check store-has($h1, 'Deps::Hello', '1.0'), 'D2: …into the ordinary store under HOME';

# ---- D2: the next run needs no --auto and no index -------------------------
my %second = rk('prog.raku', :home($h1), :index($tmp.add('no-such-index.json').Str));
check %second<exit> == 0 && %second<out> eq 'Deps::Hello 1.0',
      'D2: the next run, without --auto and with the index gone, gives the same stdout';
check %second<err> eq '', 'D2: …and says nothing on stderr';

# ---- D2: -q keeps the installed line ----------------------------------------
my $hq = fresh-home;
my %quiet = rk('-q', '--auto', 'prog.raku', :home($hq));
check %quiet<out> eq 'Deps::Hello 1.0' && installed-lines(%quiet<err>).elems == 1,
      'D2: -q does not drop the installed line (it reports a store change)';

# ---- D2: -e, EVAL, and a whole program in one go ----------------------------
my %e = rk('--auto', '-e', 'use Deps::Hello; print hello()', :home(fresh-home));
check %e<out> eq 'Deps::Hello 1.0'
      && installed-lines(%e<err>) eqv ('installed Deps::Hello:ver<1.0>:auth<test:deps> for -e',),
      'D2: --auto -e fetches too, and says "for -e"';
my %ev = rk('--auto', '-e', 'EVAL q[use Deps::Hello; print hello()]', :home(fresh-home));
check %ev<exit> == 0 && %ev<out> eq 'Deps::Hello 1.0', 'D2: a `use` inside EVAL fetches';
my $h2 = fresh-home;
my %two = rk('--auto', 'two.raku', :home($h2));
check %two<exit> == 0 && %two<out> eq 'Deps::Uses 0.1|Deps::Other 0.1',
      'D2: a program `need`ing two missing modules, one with a dependency, runs';
check store-has($h2, 'Deps::Uses') && store-has($h2, 'Deps::Other') && store-has($h2, 'Deps::Hello'),
      'D2: …all three distributions are in the store';
check installed-lines(%two<err>).elems == 3, 'D2: …one installed line per distribution';

# ---- D1/D2: a name in no index fails as it always did ----------------------
my $hn = fresh-home;
my %none = rk('--auto', '-e', 'use Deps::Nowhere; print 1', :home($hn));
check %none<exit> != 0, 'D1: a name in no index stays fatal';
check %none<err>.lines[0] eq 'Could not find Deps::Nowhere in:',
      'D1: …line 1 is byte for byte the old message';
check %none<err>.contains('(not in the ecosystem index either)'),
      'D1: …with one line saying the index was asked';
my %nonetype = rk('--auto', '-e', 'try EVAL q[use Deps::Nowhere]; print $!.^name', :home($hn));
check %nonetype<out> eq 'X::CompUnit::UnsatisfiedDependency',
      'D1: …and the exception is still X::CompUnit::UnsatisfiedDependency';

# ---- D2: require never fetches ----------------------------------------------
my $hr = fresh-home;
my %req = rk('--auto', '-e', 'try require Deps::Optional; print "survived"', :home($hr));
check %req<out> eq 'survived' && !store-has($hr, 'Deps::Optional'),
      'D2: `try require` of an absent module installs nothing, even with --auto';
check installed-lines(%req<err>).elems == 0, 'D2: …and says nothing about installing';

# ---- D2: the off switches ---------------------------------------------------
my $hoff = fresh-home;
my %off = rk('--auto', 'prog.raku', :home($hoff), :env(RAKUPP_DEPS => '0'));
check %off<exit> != 0 && %off<err>.lines[0] eq 'Could not find Deps::Hello in:'
      && !store-has($hoff, 'Deps::Hello'),
      'D2: RAKUPP_DEPS=0 makes --auto inert: the miss is fatal, the store untouched';
check !%off<err>.contains('ecosystem index'), 'D2: …without consulting the index';
my %offopt = rk('prog.raku', :home($hoff), :env(RAKUPP_OPT => '--auto', RAKUPP_DEPS => '0'));
check %offopt<exit> != 0 && !store-has($hoff, 'Deps::Hello'),
      'D2: …also when --auto arrives through RAKUPP_OPT';
my $hsb = fresh-home;
my %sb = rk('--sandbox', '--auto', '-I', $work.Str, 'prog.raku', :home($hsb));
check %sb<exit> != 0 && !store-has($hsb, 'Deps::Hello'),
      'D2: --sandbox leaves the miss fatal and the store untouched, --auto or not';

# ---- D1: a suite that uses an undeclared module still fails ----------------
# `rakupp install` runs Deps::Sloppy's suite, which uses Deps::Other without
# declaring it. Fetching there would make it pass here and fail under Rakudo —
# so not even a RAKUPP_OPT=--auto in the environment may reach the suite.
my $hs = fresh-home;
my %sl = rk('install', 'Deps::Sloppy', :home($hs), :env(RAKUPP_OPT => '--auto'));
check %sl<exit> != 0 && !store-has($hs, 'Deps::Sloppy') && !store-has($hs, 'Deps::Other'),
      'D1: a dist suite using an undeclared module fails, and nothing is fetched for it';

# ---- D1: the typo guard -----------------------------------------------------
my $ht = fresh-home;
my %typo = rk('--auto', '-e', 'use Deps::Populer; print hello()', :home($ht));
check %typo<exit> != 0 && !store-has($ht, 'Deps::Populer'),
      'D1: a name one letter from a much more depended-on one is not fetched';
check %typo<err>.lines[0] eq 'Could not find Deps::Populer in:',
      'D1: …line 1 is the usual message';
check %typo<err>.contains('did you mean Deps::Popular?'), 'D1: …and the neighbour is named';
check %typo<err>.contains('use Deps::Populer:auth<'), 'D1: …with the spelling that passes the guard';
my %pinned = rk('--auto', '-e', 'use Deps::Populer:auth<test:deps>; print hello()', :home($ht));
check %pinned<exit> == 0 && %pinned<out> eq 'Deps::Populer 0.1',
      'D1: the same `use` with :auth<…> passes the guard and installs';
my %popok = rk('--auto', '-e', 'use Deps::Popular; print hello()', :home(fresh-home));
check %popok<exit> == 0 && %popok<out> eq 'Deps::Popular 1.0',
      'D1: the popular name itself installs';

# ---- D1: an unreachable index is tried once, not once per process -----------
# curl through a shim that counts its calls; port 9 refuses at once.
my $shim = $tmp.add('shim');
$shim.mkdir;
my $calls = $tmp.add('curl-calls');
my $real-curl = (run 'sh', '-c', 'command -v curl', :out).out.slurp(:close).trim;
$shim.add('curl').spurt("#!/bin/sh\necho call >> '$calls'\nexec '$real-curl' \"\$@\"\n");
$shim.add('curl').chmod(0o755);
my $hu = fresh-home;
my %env-u = PATH => "$shim:{%*ENV<PATH>}";
my %u1 = rk('--auto', 'prog.raku', :home($hu), :index('http://127.0.0.1:9/index.json'), :env(%env-u));
check %u1<exit> != 0 && %u1<err>.lines[0] eq 'Could not find Deps::Hello in:',
      'D1: with the index unreachable the miss is fatal, line 1 as ever';
check %u1<err>.contains('could not reach the ecosystem index'), 'D1: …and says why';
check $calls.e, 'D1: …after trying the network';
my $before = $calls.e ?? $calls.lines.elems !! 0;
my %u2 = rk('--auto', 'prog.raku', :home($hu), :index('http://127.0.0.1:9/index.json'), :env(%env-u));
check %u2<exit> != 0 && ($calls.e ?? $calls.lines.elems !! 0) == $before,
      'D1: a second process right after does not try the network again';

# ---- D3: a script keeps the versions its first run chose --------------------
my $hp = fresh-home;
my %p1 = rk('--auto', 'prog.raku', :home($hp));
check %p1<out> eq 'Deps::Hello 1.0', 'D3: first run gets 1.0, the newest in the index';
check $hp.IO.add('.raku/rakupp-install/scripts').d
      && $hp.IO.add('.raku/rakupp-install/scripts').dir.elems == 1,
      'D3: …and writes one pin record';
my %i2 = rk('install', '--no-test', 'Deps::Hello:ver<2.0>', :home($hp), :index($INDEX2));
check %i2<exit> == 0 && store-has($hp, 'Deps::Hello', '2.0'), 'D3: 2.0 installed beside it';
my %p2 = rk('prog.raku', :home($hp), :index($INDEX2));
check %p2<out> eq 'Deps::Hello 1.0', 'D3: the script still loads 1.0';
my %p2e = rk('-e', 'use Deps::Hello; print hello()', :home($hp), :index($INDEX2));
check %p2e<out> eq 'Deps::Hello 2.0', 'D3: a -e program, which has no record, loads the newest';
$work.add('prog2.raku').spurt("use Deps::Hello:ver<2.0>;\nprint hello();\n");
my %p3 = rk('prog2.raku', :home($hp), :index($INDEX2));
check %p3<out> eq 'Deps::Hello 2.0', 'D3: a `use` the pin cannot satisfy resolves again';
check %p3<err> eq '', 'D3: …from the store, with nothing to install';

# ---- D4: --auto --bundle fetches at compile time ----------------------------
my $hb0 = fresh-home;
my %b0 = rk('--bundle', 'prog.raku', '-o', $tmp.add('bundled-plain').Str, :home($hb0));
check !store-has($hb0, 'Deps::Hello')
      && %b0<err>.contains('rakupp --auto --bundle prog.raku -o OUT'),
      'D4: --bundle alone fetches nothing and says how to';
my $hb = fresh-home;
my $bin = $tmp.add('bundled').Str;
my %b = rk('--auto', '--bundle', 'prog.raku', '-o', $bin, :home($hb));
check %b<exit> == 0 && installed-lines(%b<err>).elems == 1,
      'D4: --auto --bundle fetches the missing module while compiling';
if $bin.IO.e {
    my $p = run 'env', "HOME={fresh-home}", 'RAKULIB=', $bin, :out, :err, :cwd($work.Str);
    check $p.out.slurp(:close) eq 'Deps::Hello 1.0', 'D4: …and the binary carries it to an empty HOME';
    $p.err.slurp(:close);
}
else {
    check False, 'D4: …and the binary carries it to an empty HOME';
}
$work.add('late.raku').spurt("EVAL q[use Deps::Late; print hello()];\n");
my $bin2 = $tmp.add('bundled-late').Str;
my %bl = rk('--auto', '--bundle', 'late.raku', '-o', $bin2, :home(fresh-home));
if $bin2.IO.e {
    my $hl = fresh-home;
    my $p = run 'env', "HOME=$hl", 'RAKULIB=', "RAKUPP_INSTALL_INDEX=$INDEX",
                $bin2, '--auto', :out, :err, :cwd($work.Str);
    $p.out.slurp(:close); $p.err.slurp(:close);
    check $p.exitcode != 0 && !store-has($hl, 'Deps::Late'),
          'D4: a compiled binary never fetches at run time';
}
else {
    check False, 'D4: a compiled binary never fetches at run time';
}

say "deps gate: $ok ok, $bad failed";
exit 1 if $bad;
