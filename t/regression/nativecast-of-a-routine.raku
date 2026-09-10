# Regression: `nativecast(Pointer, &some-sub)` answered a Pointer to address 0.
# Nothing said no — the cast fell through the type ladder to the "nothing else
# matched" arm — so a program that wrote that pointer into a C struct handed
# the library a NULL function pointer, and failed later and elsewhere. (It was
# GUI::Wings's Win32 backend writing WNDCLASSEXW.lpfnWndProc by hand: the class
# registered with no window procedure at all.) Rakudo refuses this cast too;
# what a callback needs is a DECLARED callback parameter, which is where either
# engine mints the function pointer.
# Contract: exit 0 + last line PASS.
use NativeCall;
my @fail;

sub target(int64 $a, int64 $b --> int64) { $a + $b }

my $got = 'no exception';
{
    CATCH { default { $got = .message } }
    my $p = nativecast(Pointer, &target);
    $got = "returned a Pointer to {+$p}";
}
@fail.push("it still answers: $got") unless $got.contains('no address to cast');
@fail.push("the message does not say where a callback goes: $got")
    unless $got.contains('is native');

note @fail.join("\n") if @fail;
say @fail ?? "FAIL" !! "PASS";
exit @fail ?? 1 !! 0;
