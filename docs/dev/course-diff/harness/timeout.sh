#!/bin/sh
# timeout.sh SECS CMD...   (macOS has no coreutils timeout)
SECS=$1; shift
perl -e '
  my $s=shift; my $pid=fork;
  if($pid==0){ exec @ARGV or exit 127 }
  $SIG{ALRM}=sub{ kill "KILL",$pid; kill "KILL",-$pid; waitpid($pid,0); exit 124 };
  alarm $s; waitpid($pid,0); exit($?>>8);
' "$SECS" "$@"
