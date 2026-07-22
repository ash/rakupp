# t/regression — once broken, must stay fixed

Every file here is a self-contained repro of a bug that a change to rakupp
INTRODUCED and a later commit fixed. The suite (`t/run.raku`) auto-discovers
`*.raku` in this directory: each case must exit 0 and print `PASS` as its
last line. On failure, die with a message (non-zero exit) — no goldens.

Adding a case: reduce the bug to its minimal program, name the file after
the failure mode, and put a header comment saying what broke, what change
broke it, and the commit that fixed it. The best candidates come from
regressions the full-Roast gate caught mid-leg and from real-world user
programs (raku-corpus), which exercise paths roast never touches.
