# FAQ — Raku++ in an iOS or Android app

Yes, an app can carry the engine. It runs as native code, built for the phone's
processor and called through the same C ABI any other host uses, not as
WebAssembly in a web view. This page answers the questions that come first;
the build flags, the source changes and the host-side details are in
[EMBEDDING.md](../EMBEDDING.md#mobile-apps-ios-and-android).

## The short answer

- Compile the runtime's sources with Xcode's clang (iOS) or the NDK's clang
  (Android) into a framework or a `.so`, and call `rk_new`, `rk_run` and
  `rk_set_output` from Swift, Kotlin or Flutter's `dart:ffi`.
- Run every program on a thread of your own with a large stack, never on the UI
  thread, and with a fresh interpreter each time.
- Expect about 16 MB per architecture on iOS and 17–20 MB per ABI on Android.
  The stores send each device only its own.

The Learn Raku app, the Course of Raku for iOS and Android, works this way: the
course's examples have Run buttons, and there is a playground.

## Native, or Raku.js in a web view?

Both run Raku++. Natively, Raku code gets what the browser build cannot give it:

| | native library | Raku.js in a web view |
|---|---|---|
| `start`, `await`, `hyper` | work: real threads | no: the WebAssembly build is single-threaded |
| recursion depth | as deep as the stack you give it | about 200 nested calls |
| speed | the `rakupp` interpreter | the same interpreter, as WebAssembly |
| between the UI and the engine | a C call | JavaScript and a message bridge |
| added size | 16–20 MB per architecture | about 7 MB of `.wasm`, plus the web view |

Neither has a JIT. Both platforms forbid an app to map executable memory, and
the browser build has none either.

## What can a program do on a phone?

Most of the language is unaffected, and nearly every example in the Course of
Raku runs unchanged. The differences are about the platform:

- **Files** are the app's own: its sandbox, and the working directory the host
  chooses for the run.
- **Processes**: `run`, `shell` and `Proc::Async` cannot start anything on
  iOS.
- **Network**: sockets work. HTTPS does not, because it goes through the
  system's OpenSSL by NativeCall and neither platform offers an app one.
- **Input**: `get`, `lines` and `prompt` read whatever the host feeds them, as
  a preset string or line by line as the user types.

## Can the app stop a runaway program?

Not with the 5.4.1 ABI alone: there is no `rk_interrupt` yet, and an interpreter
cannot be freed while it is running. Learn Raku adds one with a small patch: a
flag checked at each statement, block entry and loop iteration, which ends the
program with `X::AdHoc "Interrupted"`. It also caps the output it buffers, so a
`loop { say 1 }` stops at a megabyte instead of filling memory.

## Will the App Store and Google Play accept it?

Apple's App Review guideline 2.5.2 allows an educational app to run code that
the user writes, provided the app does not download code to run. Google Play
restricts downloading native code; an interpreter running programs typed into
the app is not that. In both cases the interpreter has to be inside the app as
submitted, which a native library is.

## Where do I start?

[EMBEDDING.md](../EMBEDDING.md#mobile-apps-ios-and-android) has the source list,
the compiler and linker flags for both platforms, the four source changes 5.4.1
needs on them, and how the host side handles threads, output, input and
stopping.
