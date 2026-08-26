# gui — native desktop apps, `react`/`whenever` as the event loop

Two desktop apps whose widgets are Supplies. In the Counter, a button click, a
once-a-second clock and Ctrl+C are three `whenever` blocks inside one `react`;
the Calculator adds a keypad of sixteen more. The GUI is real native widgets —
NSWindow, NSTextField, NSButton on macOS; GtkWindow, GtkLabel, GtkButton on
Linux — reached through NativeCall, with no C glue and no bindings distribution.

The framework is the **GUI::Wings** module from
[github.com/ash/raku-modules](https://github.com/ash/raku-modules):

```sh
export RAKULIB=$HOME/raku-modules/GUI-Wings/lib
```

```raku
use GUI::Wings;

app 'Counter', {
    my $n = 0;
    window :title('Camelia counts'), :size(480, 220), {
        my $l = label 'clicked 0 times', :font(28);
        my $b = button 'Click me';

        react {
            whenever $b.clicks        { $l.text = "clicked {++$n} times" }
            whenever Supply.interval(1) { window.title = DateTime.now.hh-mm-ss }
            whenever signal(SIGINT)   { done }
        }
    }
}
```

## Run it

```sh
RAKUPP_MAIN_THREAD=1 build/rakupp showcase/gui/counter.raku    # Raku++
RAKUPP_MAIN_THREAD=1 build/rakupp showcase/gui/calculator.raku                # the calculator
raku showcase/gui/counter.raku                                 # Rakudo, unchanged
```

The window comes up, the title ticks like a clock, the label counts your
clicks; close the window or Ctrl+C to quit. `WINGS_AUTODRIVE=3` makes the app
click its own button three times and then end through its own SIGINT path —
the whole demo self-verifies in about four seconds. `WINGS_DEBUG=1` narrates
clicks and reconciliations on stderr.

AppKit accepts windows only on the process main thread, which is what
`RAKUPP_MAIN_THREAD=1` provides under Raku++; under Rakudo the mainline is
already the main thread. The API floor is macOS 10.12.2 (tested on 15.7), arm64
and x86-64 alike; the Raku++ side needs a build from current `main` — release
binaries predate the main-thread hook — while any recent Rakudo works as
released.

Neither program mentions Cocoa, so neither is macOS-only: `GUI::Wings` splits
into a toolkit-free front and `GUI::Wings::Backend::{Cocoa,Gtk,Win32}`, chosen
by OS and overridable with `WINGS_BACKEND`. The same two files run on GTK3
unchanged — `WINGS_BACKEND=Gtk raku showcase/gui/counter.raku` — which is how
they were verified here against Homebrew GTK. The Win32 backend is written but
has never been run.

## How it holds together

- The **pump thread** owns the toolkit: it pumps events inside an autorelease
  pool (on Cocoa), drains a Channel of marshalled UI closures, and *reconciles*
  widget state — `$l.text`, `window.title` — into the toolkit only when it
  changed. On macOS that thread must be the process main thread; GTK and Win32
  only require that one thread does all the calls, which this is.
- The **`app` body runs on a worker**, so `react` can park there without
  freezing the GUI. Builders (`window`, `label`, `button`) send their toolkit
  work to the pump thread and wait for the ack.
- A **click** travels from the toolkit into a Raku closure — on Cocoa through a
  runtime-minted Objective-C class whose action method is a Raku sub
  (`class_addMethod` with a NativeCall closure), on GTK through
  `g_signal_connect_data` — then a `Supplier.emit` → the worker's `whenever`.
- **No NSRect ever crosses the FFI.** Both macOS ABIs pass an NSPoint/NSSize
  (two doubles) exactly like two `num64` arguments, so geometry goes through
  `setContentSize:` / `setFrameOrigin:` / `setFrameSize:` and the same module
  binary-honestly serves arm64 Raku++ and an x86-64 Rakudo.

`raw-cocoa.raku` is the same idea with no framework at all — sixty lines of
bare `objc_msgSend` that open a window, mint a target class and pump events —
kept as the reference for what Wings wraps:

```sh
RAKUPP_MAIN_THREAD=1 build/rakupp showcase/gui/raw-cocoa.raku
```
