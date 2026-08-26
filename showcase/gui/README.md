# gui — a native macOS app, `react`/`whenever` as the event loop

A desktop Counter app whose widgets are Supplies: a button click, a once-a-second
clock and Ctrl+C are three `whenever` blocks inside one `react`. The GUI is real
Cocoa — NSWindow, NSTextField, NSButton — reached through `objc_msgSend` over
NativeCall, with no C glue and no bindings distribution.

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
already the main thread. macOS only, arm64 and x86-64 (a Rosetta Rakudo works).

## How it holds together

- The **main thread** owns AppKit: it pumps `nextEventMatchingMask:` inside an
  autorelease pool, drains a Channel of marshalled UI closures, and *reconciles*
  widget state — `$l.text`, `window.title` — into Cocoa only when it changed.
- The **`app` body runs on a worker**, so `react` can park there without
  freezing the GUI. Builders (`window`, `label`, `button`) send their AppKit
  work to the main thread and wait for the ack.
- A **click** travels AppKit → a runtime-minted Objective-C class whose action
  method is a Raku sub (`class_addMethod` with a NativeCall closure) → a
  `Supplier.emit` → the worker's `whenever`.
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
