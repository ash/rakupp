# `for $*IN.lines` over a pipe saw no line until the writer closed it.
#
# `$*IN.lines` loaded the whole input into the handle's line cache before
# handing out the first line, so a program reading frames from a live
# process waited for that process to end. `lines()` and `$*IN.get` already
# streamed. nige123/cli.321.do read its NDJSON frames with `getc` to get
# round it. The child below times each line against its own start; the
# writer holds the pipe open for two seconds after the first line, so a
# reader that waits for EOF reports that line late.
#
# Contract: exit 0 + last line PASS.
my @fail;
sub check(Bool $ok, $what) { @fail.push($what) unless $ok }

if $*DISTRO.is-win { say 'PASS'; exit }   # the writer is a POSIX shell pipeline

my $child = Q[my $t0 = now; for $*IN.lines -> $l { say "$l {now - $t0}"; $*OUT.flush }];
my $exe = $*EXECUTABLE.absolute;
my $p = run 'sh', '-c', "(echo one; sleep 2; echo two) | '$exe' -e '$child'", :out;
my @got = $p.out.lines.map(*.words);
check(@got.elems == 2, "two lines (got {@got.raku})");
if @got.elems == 2 {
    check(@got[0][0] eq 'one' && @got[0][1] < 1.5, "the first line arrives while the pipe is open (at {@got[0][1]} s)");
    check(@got[1][0] eq 'two' && @got[1][1] >= 1.5, "the second after the writer's pause (at {@got[1][1]} s)");
}

if @fail { note "FAILED: @fail.join('; ')"; say 'FAIL' } else { say 'PASS' }
