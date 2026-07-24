# Networking with Raku++

Raku++ speaks TCP over the network using the same asynchronous socket API as
Rakudo: `IO::Socket::Async` for clients and servers, `signal()` for graceful
shutdown, and — with a matching system OpenSSL — `IO::Socket::Async::SSL` for
TLS. Every example below was run against `rakupp` and its output is shown as
produced.

The async model is cooperative: only one thread runs interpreter code at a time,
and workers yield the GIL at blocking points (`await`, `sleep`, socket reads).
`await` and a `react { whenever … }` block are the two ways to wait for I/O.

---

## An HTTP client

Connect, send a request, and collect the response. Inside a `react` block a
`whenever` on the socket's `.Supply` receives each chunk of incoming bytes and
the block ends when the server closes the connection.

```raku
my $conn = await IO::Socket::Async.connect('example.com', 80);
await $conn.write("GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n".encode);

my $response = Buf.new;
react {
    whenever $conn.Supply(:bin) -> $chunk {
        $response.append($chunk);
    }
}

say $response.decode('latin-1').lines[0];   # HTTP/1.1 200 OK
say "received {$response.bytes} bytes";      # received 828 bytes
```

`.Supply(:bin)` yields `Blob` chunks; drop the `:bin` to get decoded text. Use
`await $conn.write(...)` to be sure the request is fully sent before reading.

### Streaming with `.tap` instead of `react`

When you want to keep the socket around rather than block in a `react`, tap the
Supply and await a Promise that its `done` callback keeps:

```raku
my $conn = await IO::Socket::Async.connect('example.com', 80);
await $conn.write("GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n".encode);

my $body     = Buf.new;
my $finished = Promise.new;
$conn.Supply(:bin).tap(
    -> $chunk { $body.append($chunk) },
    done => { $finished.keep },
);
await $finished;
say "{$body.bytes} bytes";
```

---

## A TCP server

`IO::Socket::Async.listen` returns a Supply that emits one connection per client.
Tap the connection's own Supply to read what the client sends.

```raku
react {
    whenever IO::Socket::Async.listen('127.0.0.1', 15480) -> $conn {
        whenever $conn.Supply(:bin) -> $data {
            await $conn.write("echo: ".encode ~ $data);
            $conn.close;
        }
    }
}
```

Connecting to it with any client (`nc 127.0.0.1 15480`, another Raku++ program,
a browser…) and sending `hello` gets back `echo: hello`.

---

## Graceful shutdown with signals

`signal(SIGINT)` is a Supply that emits each time the process receives that OS
signal — the idiomatic way to stop a server on Ctrl-C. `done` ends the `react`
block cleanly.

```raku
say 'Server running — press Ctrl-C to stop';

react {
    whenever signal(SIGINT) {
        say 'Shutting down…';
        done;
    }
}
```

`signal(SIGINT, SIGTERM)` reacts to several signals at once. A second Ctrl-C
after `done` no longer re-fires the handler (the tap is torn down when the
`react` block ends).

---

## HTTPS / TLS

With the `IO::Socket::Async::SSL` module installed (and its OpenSSL bindings
resolvable — see the note below), Raku++ performs a real TLS handshake through
the system OpenSSL and streams the decrypted response like any other socket.

```raku
use IO::Socket::Async::SSL;

my $conn = await IO::Socket::Async::SSL.connect('example.com', 443, :insecure);
await $conn.write("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n".encode);

my $response = Buf.new;
react {
    whenever $conn.Supply(:bin) -> $chunk {
        $response.append($chunk);
        done if $response.bytes > 300;
    }
}
say $response.decode('latin-1').lines[0];    # HTTP/1.1 200 OK
```

Notes and current limits:

- **Architecture must match the OpenSSL library.** `IO::Socket::Async::SSL`
  `dlopen`s the system `libssl`; the interpreter and that library must be the
  same architecture. If you built an arm64 `rakupp` but only an x86_64 OpenSSL is
  installed (or vice-versa) the load fails with *"Cannot load native library
  '…/libssl.dylib'"* — build a matching `rakupp` (e.g. an x86_64 build to use an
  x86_64 Homebrew OpenSSL).
- **`:insecure` skips certificate verification.** The certificate-verifying path
  (hostname / chain validation, i.e. without `:insecure`) is not yet complete, so
  use `:insecure` for now and treat the transport as encrypted-but-unauthenticated.
- The plain TLS transport (handshake, encrypted read/write) works end to end; a
  higher-level HTTP client on top of it is a further layer.

---

## What's available

| API | Status |
|---|---|
| `IO::Socket::Async.connect` / `.listen` | works (client + server) |
| `$sock.Supply(:bin)` reads, `.write` / `.print` | works |
| `signal(SIGINT, …)` for shutdown | works |
| `IO::Socket::Async::SSL` (TLS, `:insecure`) | works on an arch-matched build |
| TLS certificate verification | not yet |
