# How Raku++ learned to speak HTTPS

This is the story of getting one real HTTPS request to work — from *"OpenSSL
won't even load"* to a live `HTTP/1.1 200 OK` fetched over TLS through Raku++'s
own NativeCall and the system OpenSSL. It is a companion to the round-by-round
[LONGREAD](../LONGREAD.md); this doc zooms in on a single feature and what it
took.

The short version: **"get HTTPS working" was never one feature.** It was a chain
of independent bugs, each hidden behind the last, and nearly every one turned out
to be a *general* correctness bug — not anything TLS-specific. Fixing them left
the interpreter more correct across the board; the Roast conformance numbers went
*up* the whole way.

## The starting point

The goal was ordinary Raku:

```raku
use IO::Socket::Async::SSL;
my $conn = await IO::Socket::Async::SSL.connect('example.com', 443, :insecure);
await $conn.write("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n".encode);
react {
    whenever $conn.Supply(:bin) -> $bytes { ... }
}
```

At the start, `use IO::Socket::Async::SSL` didn't even load. `IO::Socket::Async::SSL`
sits on top of the `OpenSSL` binding, which talks to the system `libssl` through
NativeCall (Raku's `is native` FFI). None of that stack was ready.

## The layers, in the order they fell

**1. NativeCall had to grow up.** OpenSSL is pure FFI, so the whole `is native`
surface came first: `is rw` out-parameters, mixed integer/float arguments
(without libffi — using one over-wide function-pointer signature that exploits
the independent integer and float register banks on the SysV-AMD64 and AArch64
ABIs), `nativecast`, `cglobal`, `Pointer[T]`, `CArray` returns, CStruct field
layout and access, and callbacks (a Raku sub handed to C as a function pointer,
via a small trampoline pool). See [NATIVE.md](NATIVE.md).

**2. The distribution plumbing.** `OpenSSL::NativeLib` reads a bundled
`libraries.json` to find `libssl`'s path — so `%?RESOURCES` (a distribution's
resource files), `Rakudo::Internals::JSON`, and `$*VM.platform-library-name` all
had to work.

**3. Fully-qualified `our sub`s.** `OpenSSL::Version::version_num()` was undefined
because a file-scoped `unit module Foo;` never set the package prefix — so the
module's `our sub`s published under bare names, not `Foo::name`. Fixing that made
~49 more Roast assertions pass on its own.

**4. Code-ref native libraries and pointer round-tripping.** OpenSSL declares its
subs `is native(&gen-lib)` — the library name comes from *calling* a sub, not a
string literal. And `SSL_CTX_new` returns a struct pointer that has to survive a
trip back into `SSL_new`. With those, real OpenSSL calls started working —
`OpenSSL::Version::version_num` returned `30600020`, byte-identical to Rakudo.

**5. The bug that wasn't NativeCall.** The TLS setup then died with *"No such
method 'new' for invocant of type 'Any'"*. The cause: a typed attribute
`has Supplier::Preserving $!bytes-received` defaulted to `Any` instead of the
*type object*, so `$!bytes-received .= new` was really `Any.new`. That is a
general OOP correctness bug — any `has SomeClass $.x; … $!x .= new` was broken.

**6. Buffers that C fills.** `BIO_read` and `SSL_read` write into a buffer you
pass them — but a `Buf` argument was being passed as a throwaway copy, so the
decrypted bytes never came back. Native `Buf` arguments now copy back to the
caller, like `CArray` does.

**7. The handshake completes — after two more general bugs.** With the buffers
fixed, the handshake reached the read/write pump and *hung*. Two culprits, both
far from TLS:

- A native function returning `int32 -1` (BIO_read's "would block") came back in
  a 64-bit register **zero-extended** as `4294967295`, so `$rc < 0` was never
  true and `flush-read-bio` spun forever. Native integer returns now truncate and
  sign-extend to their declared width.
- OpenSSL's error-drain loop, `while ($err != 0|WANT_READ|WANT_WRITE)`, never
  terminated because `0 != 0|1|2` returned `True`. It must be `False`: a *negated*
  comparison over an `any` junction flips to `all` by De Morgan
  (`X != any(…)` is `all(X != …)`). That is a general junction bug.

With those two, `SSL_connect` returned success — **the TLS handshake completed**,
against a real server, with real certificates exchanged.

**8. The last mile: delivering the response.** The decrypted bytes reached the
socket's internal `Supplier`, but `$conn.Supply(:bin)` — which the module exposes
as `$!bytes-received.Supply.Channel.Supply` — delivered nothing. On a *live*
supply, `.Channel` was snapshotting an empty list. Making that chain forward
emits was the final piece:

```
=== GOT 828 bytes ===
HTTP/1.1 200 OK
Date: Fri, 24 Jul 2026
```

## The method

The technique that worked, over and over: shadow the installed module locally
(the module search path checks `lib/` first), sprinkle `note "DBG …"` markers,
binary-search the hang, then fix the **root primitive** rather than the symptom —
and isolate every suspected bug in a five-line reproducer before believing it.
Two theories cost real time and turned out wrong: a supposed scheduler deadlock
that was really the `int32` infinite loop, and a rabbit hole around OpenSSL 3.x's
renamed `SSL_get_peer_certificate` (aliased to `SSL_get1_peer_certificate`) that
only matters on the certificate-verifying path.

## Where it stands

A full **`:insecure`** HTTPS request works end to end: handshake, encrypted
request, and the decrypted response body. Two things remain: **certificate
verification** (hostname/chain validation via X.509 — the non-`:insecure` path
still needs work) and a higher-level **`Cro::HTTP::Client`** on top of the
transport. And a practical note — `IO::Socket::Async::SSL` `dlopen`s the system
`libssl`, so the `rakupp` binary and that library must be the same architecture.

Every fix along the way was a real correctness improvement, not a TLS shim. The
transport is real; the road from here is the layers above it.

See [NETWORKING.md](NETWORKING.md) for the working socket and HTTPS examples, and
[NATIVE.md](NATIVE.md) for the NativeCall surface underneath.
