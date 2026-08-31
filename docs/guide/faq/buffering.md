# FAQ — buffering: why output does not appear when you expect it

Short answer: `$*OUT` and `$*ERR` are **unbuffered**, so a `say` reaches
wherever it is going the moment you write it. A file handle you `open`
**holds back 8 KiB**, so what you write is not on disk until you flush it or
close it. Everything on this page is that one sentence and its consequences.

`.out-buffer` is the knob, on any handle, readable and assignable:

```raku
$*OUT.out-buffer = 0;                     # write through, every time
my $log = open $path, :w, :!out-buffer;   # the same, from the moment it opens
$log.flush;                               # or: push out what is pending, once
```

Everything below is verified on both engines. Where they differ, the page says
so — there is exactly one difference, and it is in the last section.

## Why anyone cares

Buffering is invisible until something else is reading your output *while you
are still running*. Then it is the difference between a live log and a wall of
text at the end:

- a build or test runner whose progress you are watching
- a program whose stdout is a pipe — `| tee`, `| grep`, a CI collector,
  `podman exec`, a supervisor capturing a service's log
- a file being `tail -f`-ed
- anything that might be killed before it finishes, taking the buffer with it

If your output goes to a terminal and you never look at it until the program
ends, none of this matters.

## The knob

`.out-buffer` is how many bytes a handle may hold back before they must be
written out. It reads:

```raku
say $*OUT.out-buffer;      # 0
say $*ERR.out-buffer;      # 0
```

and assigns, taking three shapes:

| you write | it means | it reads back as |
|---|---|---:|
| `$fh.out-buffer = 0` or `= False` | no buffer: every write goes out | `0` |
| `$fh.out-buffer = True` | the default block | `8192` |
| `$fh.out-buffer = 4096` | that many bytes | `4096` |

The same three at `open`, as an adverb — `:!out-buffer` is the one you will
actually write:

```raku
my $fh = open $path, :w, :!out-buffer;    # unbuffered from the start
my $fh = open $path, :w, :out-buffer(64_000);
```

Changing the size **flushes what is already pending**, so the new size applies
to what comes next and nothing is stranded under the old one.

## Where output can sit

There are three places, and they are independent. When output is late, work out
which one is holding it.

### 1. Your own `$*OUT` / `$*ERR`

Nowhere: both are unbuffered. A `say` is a write, and `note` — which goes to
`$*ERR` — is too. There is nothing to fix here, which is the point.

You can go the other way if you want the throughput back. A program that writes
hundreds of thousands of lines into a pipe pays one `write(2)` per line, and
buying a block back is a fair trade when nobody is watching:

```raku
$*OUT.out-buffer = 65_536;    # now say() batches; flush or exit to release it
```

(Raku++ honours that; Rakudo ignores a size on its own standard handles and
stays unbuffered. Neither will ever lose the output — what is pending is
written at exit.)

### 2. A file handle you opened

This one holds 8 KiB, and it is the usual answer to "the log file is empty and
my program is still running":

```raku
my $path = $*TMPDIR.add("demo");
my $fh = open $path, :w;
$fh.say("line 1");
say "after say:   {$path.s} bytes on disk";
$fh.flush;
say "after flush: {$path.s} bytes on disk";
$fh.say("line 2");
$fh.close;
say "after close: {$path.s} bytes on disk";
```

```
after say:   0 bytes on disk
after flush: 7 bytes on disk
after close: 14 bytes on disk
```

That first line is the one place on this page where Rakudo answers differently —
it prints `7`, because its handles write through. The last section says why.

Three ways out, in rough order of how often you want them:

```raku
my $fh = open $path, :w, :!out-buffer;   # a log something else is tailing
$fh.flush;                               # a checkpoint you chose
$fh.close;                               # you are done with it
```

`spurt` and `slurp` never come into it — they open, do the whole job, and close.

**A handle that goes out of scope is not closed.** There is no destructor
waiting to flush it for you; see
[garbage-collection.md](garbage-collection.md#does-dropping-a-filehandle-close-it).
Raku++ does flush every still-open write handle at exit, so a program that
simply ends does not lose its output — but a handle you dropped an hour ago is
still holding its last 8 KiB until then. Close it, or open it unbuffered.

### 3. A child process's output

`run` and `shell` **without** `:out` hand the child your own stdout, so its
output appears as it is produced. With `:out` you asked for the text, and text
you have to wait for:

```raku
my $t0 = now;
sub at { "{(now - $t0).round}s" }

run 'bash', '-c', 'echo passed-through; sleep 1';
say at(), ": run returned";

my $text = run('bash', '-c', 'echo captured; sleep 1', :out).out.slurp(:close);
say at(), ": ", $text.chomp;
```

```
passed-through
1s: run returned
2s: captured
```

`passed-through` is on screen immediately, before its `sleep`; `captured` cannot
be, because it does not exist as a string until the child has closed its stdout.
`qqx`/`qx` are the capturing kind too, by definition — `say qqx[echo ok; sleep
10]` waits ten seconds on both engines, and no setting changes that.

If you want the text *and* want it live, tap it as it arrives:

```raku
my $p = Proc::Async.new('bash', '-c', 'echo one; sleep 1; echo two');
my $t0 = now;
react {
    whenever $p.stdout.lines { say "{(now - $t0).round}s: $_" }
    whenever $p.start { done }
}
```

```
0s: one
1s: two
```

Each line is delivered as its newline arrives, so a partial line waits for the
rest of itself rather than being cut in half at whatever byte the pipe happened
to hand over.

## The child has its own buffer, and it is not yours to set

This is the one that wastes an afternoon. Your side can be perfect and the
output still arrives in a lump, because the *child* is buffering — most
runtimes' standard library switches to a block buffer the moment stdout is not
a terminal, which is exactly what happens when your program is in a pipeline or
under `podman exec`, `docker exec`, `ssh` or a CI runner.

```raku
run 'python3', '-c', 'import time; print("a"); time.sleep(1); print("b")';
```

Run that with your own stdout on a terminal and `a` appears at once. Run the
same program with `| cat` after it and nothing appears for a second, then both
lines together. Nothing on your side changed; the child looked at its stdout,
saw a pipe rather than a terminal, and picked a block buffer.

You cannot reach into it. You tell the child, in its own language — and note
that `stdbuf` sets the *C library's* buffering, so it does nothing for a runtime
that buffers on its own account (Python among them, which is why Python has
`-u`):

| child | say this |
|---|---|
| Python | `python3 -u …`, or `PYTHONUNBUFFERED=1` |
| Raku (Rakudo or Raku++) | nothing — `$*OUT` is already unbuffered |
| a C program, or anything you cannot change | `stdbuf -oL -eL CMD`, where you have it |
| `grep` | `--line-buffered` |
| `sed` | `-u` on GNU, `-l` on BSD (macOS) |
| `awk` | `fflush()` after each record |

```raku
run 'python3', '-u', '-c', 'import time; print("a"); time.sleep(1); print("b")';
```

Now `a` arrives immediately whatever your stdout is.

The general remedy when the child has no such switch and you cannot change it:
give it a pseudo-terminal, and it will line-buffer itself the way it does
interactively. `unbuffer CMD` (from expect) is the portable spelling; your
platform's `script` can do it too, with an invocation that differs between GNU
and BSD.

## Something is still late — which of the three is it?

Work outwards.

1. **Does it appear when you run it on a terminal, but not through a pipe?**
   Then it is a *buffer that switched modes*, and since `$*OUT` never does,
   it is the child (section 3) or a handle (section 2).
2. **Is it your own `say`?** It is already out. If you cannot see it, something
   downstream is holding it — the next program in the pipe, or the terminal.
3. **Is it a file you wrote?** `flush`, `close`, or `:!out-buffer` (section 2).
4. **Is it a child's output?** Drop `:out` to pass it through, or tap it with
   `Proc::Async` (section 3). If it is still late, it is the child's own buffer.
5. **Is it a child that is a Raku program?** Then it is not buffering, and the
   problem is somewhere in 1–4 instead.

## Where Raku++ and Rakudo differ

One place: the default on a handle you `open`.

```raku
my $fh = open $path, :w;
say $fh.out-buffer;
```

Raku++ says `8192`, Rakudo says `1` — that is Rakudo's way of spelling "write
through", and it does. Every other value and behaviour on this page is the same
on both, standard handles included.

The reason is structural rather than a preference. A Raku++ handle has no
persistent file descriptor behind it, so an unbuffered write costs an
open/write/close for every `print`; a block amortises that. When you want the
Rakudo behaviour, ask for it — `:!out-buffer` at `open`, which costs you one
adverb and is portable across both engines.
