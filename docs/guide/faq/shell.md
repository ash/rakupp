# FAQ — running external commands

Every snippet on this page was run on both Raku++ and Rakudo and produces
identical output. Where the two engines genuinely differ, it says so.

## Run a command and let its output through

```raku
shell('ls');            # one command line, through the system shell
run('ls', '-l');        # an argv — no shell, no word splitting
```

Both write the child's output to your terminal and return a `Proc`.

Use `run` when you have the arguments as separate values: nothing is re-parsed,
so a filename with a space in it stays one argument. Use `shell` when you want
the shell itself — pipes, redirection, globbing, `&&`.

```raku
run('rm', 'my file.txt');          # one file, whose name contains a space
shell('ls *.txt | wc -l');         # the shell does the glob and the pipe
```

## Capture the output instead of printing it

Add `:out`. Nothing is printed while the command runs; you get the text.

```raku
my $text = shell('ls', :out).out.slurp(:close);
say $text.lines.elems;
```

`:close` closes the pipe as it reads. Without it the handle stays open.

| you want | write |
|---|---|
| capture stdout | `shell('ls', :out).out.slurp(:close)` |
| capture stderr | `shell('cmd', :err).err.slurp(:close)` |
| capture both | `my $p = shell('cmd', :out, :err);` then read `$p.out` and `$p.err` |
| discard stdout (silent, not captured) | `shell('ls', :!out)` |
| let it print through | `shell('ls')` |
| the same with an argv | `run('ls', :out)` |

Both streams at once — keep the `Proc`, since each pipe is read separately:

```raku
my $p = shell('echo out; echo err 1>&2', :out, :err);
say "stdout: ", $p.out.slurp(:close).trim;   # out
say "stderr: ", $p.err.slurp(:close).trim;   # err
```

## Check whether it worked

```raku
say shell('exit 7').exitcode;   # 7
say shell('true').so;           # True   — a Proc is true when it succeeded
say shell('false').so;          # False
```

So the usual shape is:

```raku
my $p = shell('git rev-parse HEAD', :out, :err);
if $p {
    say "commit: ", $p.out.slurp(:close).trim;
}
else {
    note "git failed: ", $p.err.slurp(:close);
}
```

## Feed it input

Add `:in` and write to `$proc.in`. The command starts when you close the pipe.

```raku
my $p = run('wc', '-l', :in, :out);
$p.in.print("a\nb\nc\n");
$p.in.close;
say $p.out.slurp(:close).trim;   # 3
```

## What a Proc shows you

```raku
say run('echo', 'hi');
# Proc.new(in => IO::Pipe, out => IO::Pipe, err => IO::Pipe, os-error => Str,
#          exitcode => 0, signal => 0, pid => 58353, command => ("echo", "hi"))
```

`.command` is the argv you asked for — a one-element list for `shell`, since
`shell` takes a single command line:

```raku
say run('echo', 'z', :out).command.join(' ');   # echo z
```

## Where Raku++ and Rakudo differ

**Pass-through output is buffered, not live.** When you do *not* pass `:out`,
Rakudo hands the child your terminal directly, so a long-running command's output
appears as it is produced. Raku++ captures internally and echoes when the child
exits, so you see it all at once at the end. For anything that streams progress
over time, that is a visible difference. With `:out` both engines capture and
behave identically.

**Windows.** `shell` runs its argument through `%COMSPEC%` (`cmd.exe`), not
`/bin/sh` — so `shell('dir')` works and `shell('ls')` does not, unless you have
an `ls.exe` on `PATH`. `run` starts an executable directly; if the name is not an
`.exe` (a `.bat`, a `.cmd`, or a shell builtin) it is retried through the command
processor. A spawn that fails reports the OS error rather than a bare exit code.

## Gotchas

**`say shell('ls')` prints the listing, then a `Proc` line.** That is correct —
`shell` let the output through *and* `say` printed the returned `Proc`. If you
only want the text, capture it:

```raku
say shell('ls', :out).out.slurp(:close);
```

(Before v1.2.6 the `Proc` gisted as a dump of its internals, which included the
captured output — so the listing appeared *twice*. Fixed.)

**`:out` and `:!out` are different.** `:out` captures for you to read; `:!out`
discards. Leaving both off lets the output through.

**Reading a pipe twice gives you nothing the second time.** `slurp(:close)`
consumes it. Store the string if you need it more than once.

---

Back to the [FAQ index](README.md).
