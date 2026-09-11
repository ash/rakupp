# Windows gate: the platform identity Raku code branches on, and the two FFI
# properties the Win32 API needs from the engine. Run by release.yml on both
# Windows legs (MSVC and MinGW-w64) — nothing here needs a C compiler, a
# PowerShell or a desktop session, only DLLs every Windows has.
#
# What it pins, and why each was once wrong:
#   $*DISTRO.is-win was hard-coded False, so every Windows branch in every
#   module (and in this engine's own tooling) took the POSIX arm.
#   The FFI's fallback path — the one Windows actually uses, libffi not being
#   something the platform ships — held eight integer arguments and passed
#   them as C `long`, which is 32 bits here: CreateWindowExW (twelve) and
#   CreateFontW (fourteen) could not be called at all, and any pointer that
#   crossed lost its top half.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check($got, $want, $what) {
    @fail.push("$what: got {$got.raku}, want {$want.raku}") unless $got eqv $want;
}

unless $*KERNEL.name eq 'win32' || (try $*DISTRO.is-win) {
    note "not Windows ({$*KERNEL.name}), nothing to gate";
    say "PASS";
    exit 0;
}

# ---- identity ---------------------------------------------------------------
check($*KERNEL.name,        'win32',    '$*KERNEL.name');
check($*DISTRO.name,        'mswin32',  '$*DISTRO.name');   # Rakudo: osname, lowercased
check($*DISTRO.is-win,      True,       '$*DISTRO.is-win');
check($*DISTRO.path-sep,    ';',        '$*DISTRO.path-sep');
check($*VM.config<osname>,  'MSWin32',  '$*VM.config<osname>');
check($*VM.config<exe>,     '.exe',     '$*VM.config<exe>');

# ---- the FFI, both ways it can be taken -------------------------------------
# The same crossings twice: as the engine finds the FFI (libffi if some DLL on
# PATH happens to provide it) and with the fallback forced. On Windows the
# second is the one users get.
my $child = q:to/END/;
    use NativeCall;
    sub wstr(Str $s) {
        my $c = CArray[uint16].new;
        my $i = 0;
        for $s.encode('utf-16').list -> $u { $c[$i++] = $u }
        $c[$i] = 0;
        $c;
    }
    # fourteen arguments: past the eight the fallback prototype used to hold
    sub CreateFontW(int32, int32, int32, int32, int32, uint32, uint32, uint32,
                    uint32, uint32, uint32, uint32, uint32, CArray[uint16] --> Pointer)
        is native('gdi32') { * }
    sub DeleteObject(Pointer --> int32) is native('gdi32') { * }
    sub GetModuleHandleW(Pointer --> Pointer) is native('kernel32') { * }
    sub GetModuleFileNameW(Pointer, CArray[uint16], uint32 --> uint32) is native('kernel32') { * }
    sub EnumUILanguagesW(&cb (Pointer, int64 --> int32), uint32, int64 --> int32)
        is native('kernel32') { * }
    sub LoadLibraryW(CArray[uint16] --> Pointer) is native('kernel32') { * }
    sub EnumSystemLocalesW(&cb (Pointer --> int32), uint32 --> int32) is native('kernel32') { * }
    sub GetProcAddress(Pointer, Str --> Pointer) is native('kernel32') { * }

    my $font = CreateFontW(-12, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 0, 0, wstr('Segoe UI'));
    say "font=", ($font.defined && +$font != 0) ?? 'made' !! 'null';
    DeleteObject($font) if $font;

    # A handle is a 64-bit address here. Truncating it does not merely give a
    # smaller number — the handle stops working, which is what this asks.
    my $h = GetModuleHandleW(Pointer);
    my $buf = CArray[uint16].new; $buf[$_] = 0 for ^260;
    my $len = GetModuleFileNameW($h, $buf, 260);
    say "handle-above-4g=", (+$h > 0xFFFFFFFF) ?? 'yes' !! 'no';   # informational: ASLR decides
    say "handle-usable=", $len > 0 ?? 'yes' !! 'no';

    # A returned handle, put straight back into the next call. This is the
    # 3.26.0 failure exactly: LoadLibraryW's HMODULE came back as the low half
    # of itself (0x00007ff888990000 reported as -2003238912), so GetProcAddress
    # was asked about a module that was not there. A truncating engine cannot
    # find DefWindowProcW; nothing else about the pair can fail.
    my $u32 = LoadLibraryW(wstr('user32.dll'));
    say "user32=", ($u32.defined && +$u32 != 0) ?? 'loaded' !! 'null';
    say "defwindowproc=", (GetProcAddress($u32, 'DefWindowProcW') andthen (+$_ != 0)) ?? 'found' !! 'null';

    # Does a callback's RETURN value cross back into C? Every check above is
    # about what goes IN; this is the other direction, and it is invisible
    # until something acts on it. EnumSystemLocalesW stops the moment its
    # callback answers false, and a Windows box has dozens of locales — so one
    # call means the answer never arrived (or arrived as zero), and many means
    # it did. GUI::Wings needs this: WM_CTLCOLORSTATIC gives a control its
    # colour through the window procedure's return value alone.
    my $seen-locales = 0;
    EnumSystemLocalesW(-> Pointer $name --> int32 { $seen-locales++; 1 }, 2);   # LCID_SUPPORTED
    say "locale-callbacks=", $seen-locales;

    # The lParam we hand in must reach the callback with all 64 bits: 0x1234500000
    # arrives as 0x34500000 through a 32-bit `long`.
    my $seen = -1;
    EnumUILanguagesW(-> Pointer $name, int64 $lp --> int32 { $seen = $lp; 1 }, 0, 78187069440);
    say "callback-lparam=", $seen;
    END

sub crossings($label, %extra) {
    my %env = %*ENV, |%extra;
    my $p = run($*EXECUTABLE, '-e', $child, :out, :err, :%env);
    my $out = $p.out.slurp(:close);
    my $err = $p.err.slurp(:close);
    @fail.push("$label: exit {$p.exitcode}: {$err.lines.head // ''}") if $p.exitcode != 0;
    my %got;
    for $out.lines { %got{.split('=')[0]} = .split('=')[1] if .contains('=') }
    %got;
}

for ('as found', {}), ('fallback', { RAKUPP_FFI => '0' }) -> ($label, %extra) {
    my %g = crossings($label, %extra);
    check(%g<font>          // '<none>', 'made', "$label: CreateFontW (14 arguments)");
    check(%g<handle-usable> // '<none>', 'yes',  "$label: a module handle survives the crossing");
    check(%g<user32>         // '<none>', 'loaded', "$label: LoadLibraryW answers a handle");
    given (%g<locale-callbacks> // '0').Int {
        when 0  { note "$label: EnumSystemLocalesW never called back — the return path is untested here" }
        # Reported, not failed, until it has been SEEN once: this gate has to
        # be able to tell us the answer without turning main red on a guess.
        when 1  { note "$label: EnumSystemLocalesW called back ONCE, so the callback's `return 1`"
                     ~ " did not reach C — every callback that answers C by its return value is"
                     ~ " broken here (WM_CTLCOLORSTATIC is one)" }
        default { note "$label: a callback's return value reaches C ($_ locales enumerated)" }
    }
    check(%g<defwindowproc>  // '<none>', 'found',  "$label: that handle still works as one");
    # The callback fires for the system UI language on any Windows; if a runner
    # ever has none, say so rather than passing quietly.
    given %g<callback-lparam> // '<none>' {
        when '-1'  { note "$label: EnumUILanguagesW never called back — the lParam width is untested here" }
        when '78187069440' { }
        default    { @fail.push("$label: callback lParam came back as $_, want 78187069440") }
    }
    note "$label: module handle above 4 GB: {%g<handle-above-4g> // '<none>'}";
}

note @fail.join("\n") if @fail;
say @fail ?? "FAIL" !! "PASS";
exit @fail ?? 1 !! 0;
