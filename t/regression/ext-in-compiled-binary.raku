# Regression: a compiled program that loads a native extension used to SEGFAULT
# on the first call into it (Grand Review phase 2; docs/dev/findings/REVIEW-GRAND-DOCS.md).
#
# An extension resolves `rk_*` from its HOST at load time, the way a Python C
# extension resolves `Py_*`. The rakupp executable carries those symbols; a
# binary from `--exe`/`--aot`/`--bundle` carried none, so the extension's first
# call jumped through an unbound stub — exit 139, AFTER `load:` and `found:` had
# both reported success, which is what made it so quiet.
#
# The export costs ~580 KB (measured) and is therefore conditional: a program
# that names `rakupp-ext-load` gets it. A name the compiler cannot see — built
# at run time, or reached through EVAL — is caught by the loader instead, which
# refuses in words. Both halves are pinned here.
#
# On Rakudo there is no `rakupp-ext-load` and no `--exe`, so the file reports
# PASS after saying so: the contract it guards is this engine's.

my $ok = True;
sub check($got, $want, $label) {
    unless $got eqv $want { note "FAIL: $label — {$got.raku} vs {$want.raku}"; $ok = False }
}

my $loader = try &::('rakupp-ext-load');
unless $loader.defined {
    say "PASS";   # Rakudo: no extension ABI, nothing to pin
    exit 0;
}

# a C compiler is needed to build the extension; without one, say so and pass
my $cc = %*ENV<CC> || 'cc';
unless (try run($cc, '--version', :out, :err)).so {
    note "note: no C compiler ($cc) — the compiled-extension check did not run";
    say "PASS";
    exit 0;
}

my $dir = $*TMPDIR.add("rakupp-extreg-{$*PID}");
$dir.mkdir;
LEAVE { for $dir.dir { .unlink }; try $dir.rmdir }

# The smallest extension EXTENSIONS.md describes: one sub, one rk_* call.
$dir.add("hello.c").spurt(q:to/C/);
    #include "rakupp_ext.h"
    static RkValue my_answer(RkCtx c) { return rk_int(c, 42); }
    static const RkSubDef subs[] = { {"answer", my_answer}, {0, 0} };
    static const RkModule mod = { RAKUPP_EXT_ABI, "Reg::Ext", subs };
    RAKUPP_EXT_EXPORT const RkModule* rakupp_ext_init(unsigned host_abi) {
        return host_abi >= RAKUPP_EXT_ABI ? &mod : 0;
    }
    C

my $inc = $*EXECUTABLE.parent.add('../include/rakupp');
$inc = $*EXECUTABLE.parent.add('../src') unless $inc.add('rakupp_ext.h').e;
unless $inc.add('rakupp_ext.h').e {
    note "note: rakupp_ext.h not found beside the binary — the compiled-extension check did not run";
    say "PASS";
    exit 0;
}

my $lib = $dir.add('libhello' ~ ($*DISTRO.is-win ?? '.dll' !! $*KERNEL.name eq 'darwin' ?? '.dylib' !! '.so'));
# the host resolves `rk_*` at LOAD time, so the extension links against nothing
# (EXTENSIONS.md "Building": dynamic_lookup on macOS, the default elsewhere)
my @link = $*KERNEL.name eq 'darwin' ?? ('-Wl,-undefined,dynamic_lookup',) !! ();
my $build = run $cc, '-shared', '-fPIC', '-I', $inc.Str, |@link,
                 $dir.add('hello.c').Str, '-o', $lib.Str, :out, :err;
unless $build.exitcode == 0 {
    note "note: the extension did not build here — the compiled-extension check did not run";
    say "PASS";
    exit 0;
}

# 1. A program that NAMES the loader works compiled, exactly as interpreted.
my $prog = $dir.add('prog.raku');
# a NON-interpolating heredoc: in a qq one, `q{...}` would run its own braces
$prog.spurt(q:to/RAKU/.subst('LIBPATH', $lib.Str));
    my &l = &::(q{rakupp-ext-load});
    say "load: ", l(q{LIBPATH});
    my $f = &::(q{answer});
    say "call: ", $f();
    RAKU

my $interp = run $*EXECUTABLE, $prog.Str, :out, :err;
check($interp.out.slurp(:close), "load: True\ncall: 42\n", 'interpreted: the extension answers');

my $bin = $dir.add('prog-bin');
my $comp = run $*EXECUTABLE, '--exe', $prog.Str, '-o', $bin.Str, :out, :err;
check($comp.exitcode, 0, 'the program compiles');
if $comp.exitcode == 0 {
    my $ran = run $bin.Str, :out, :err;
    check($ran.exitcode, 0, 'the compiled program exits 0 (it died with SIGSEGV, exit 139)');
    check($ran.signal,   0, '…and is not killed by a signal');
    check($ran.out.slurp(:close), "load: True\ncall: 42\n", '…and prints what the interpreter prints');
}

# 2. A loader name the compiler cannot see is refused IN WORDS, never a crash.
my $sneaky = $dir.add('sneaky.raku');
$sneaky.spurt(q:to/RAKU/.subst('LIBPATH', $lib.Str));
    my $n = "rakupp-ext" ~ "-load";
    my &l = &::($n);
    say l(q{LIBPATH});
    RAKU
my $sbin = $dir.add('sneaky-bin');
my $scomp = run $*EXECUTABLE, '--exe', $sneaky.Str, '-o', $sbin.Str, :out, :err;
if $scomp.exitcode == 0 {
    my $sran = run $sbin.Str, :out, :err;
    check($sran.signal, 0, 'the unseen loader name does not crash the binary');
    check($sran.err.slurp(:close).contains('cannot host a native extension'), True,
          '…it says so instead');
}

if $ok { say "PASS" } else { say "FAIL"; exit 1 }
