# FAQ — is HTTP built into the engine?

No. **TCP sockets are built in; HTTP is not.** Nothing in the engine parses a
request line, a header block or a chunked body, and there is no HTTP client in
it either.

What prompts the question is that a socket program needs no `use`:

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

That runs as written — and it runs identically on Rakudo, because
`IO::Socket::Async` and `IO::Socket::INET` are core-setting classes *there* too.
No module is missing; none was ever needed.

## Connecting to the echo server

Leave the server running and open a second terminal. The asynchronous client is
the mirror image of the server — `.connect` where the server has `.listen`:

```raku
my $conn = await IO::Socket::Async.connect('127.0.0.1', 15480);
await $conn.write("hello\n".encode);
react { whenever $conn.Supply -> $text { print $text } }
```

The synchronous class is shorter when you only have one thing to say, and needs
no `react` around it:

```raku
my $conn = IO::Socket::INET.new(:host<127.0.0.1>, :port(15480));
$conn.print("hello\n");
print $conn.recv;
$conn.close;
```

Either one prints, on both engines:

```
echo: hello
```

With no Raku involved at all, `nc 127.0.0.1 15480` does the same — type a line,
press Enter, and the reply comes back. Piping into it is a race, though:
`echo hello | nc 127.0.0.1 15480` makes netcat close at end of input, and it can
leave before the reply arrives.

What the program speaks is raw TCP: `"echo: "` is a byte prefix, not a protocol.

## What the engine actually provides

| Built in, no `use` | |
|---|---|
| `IO::Socket::Async` | `.listen`, `.connect`, `.Supply(:bin)`, `.write` / `.print` / `.put` / `.say`, `.close` |
| `IO::Socket::INET` | synchronous client and server |
| `react` / `whenever` / `await`, `signal()` | the concurrency around them |

The other half of the answer is a grep over the engine, from a checkout:

```bash
grep -rnoE '"[A-Za-z:]*HTTP[A-Za-z:]*"' src/
```

No output. Not one type, class or method name in the engine contains `HTTP`.

## HTTP is a text protocol, so you can write it yourself

For a one-off request that is often all you want. A client:

```raku
my $conn = await IO::Socket::Async.connect('example.com', 80);
await $conn.write("GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n".encode);

my $response = Buf.new;
react { whenever $conn.Supply(:bin) -> $chunk { $response.append($chunk) } }

say $response.decode('latin-1').lines[0];
say "received {$response.bytes} bytes";
```
```
HTTP/1.1 200 OK
received 828 bytes
```

And a server, which is the echo server above with a status line in front of the
body:

```raku
react {
    whenever IO::Socket::Async.listen('127.0.0.1', 15481) -> $conn {
        whenever $conn.Supply -> $request {
            my $path = $request.lines[0].words[1];
            my $body = "You asked for $path\n";
            await $conn.write: join('',
                "HTTP/1.1 200 OK\r\n",
                "Content-Type: text/plain\r\n",
                "Content-Length: {$body.encode.bytes}\r\n",
                "Connection: close\r\n\r\n",
                $body).encode;
            $conn.close;
        }
    }
}
```
```
$ curl -i http://127.0.0.1:15481/hello
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: 21
Connection: close

You asked for /hello
```

Both snippets produce that output on Raku++ and on Rakudo alike. What you do
not get for free is everything after the first request: keep-alive, chunked
encoding, redirects, compression, timeouts, TLS. That is what the modules are
for.

## HTTP from a module

Install one and `use` it — `rakupp install HTTP::Tiny`, or zef. Where the two
engines currently stand:

| | Raku++ | Rakudo |
|---|---|---|
| `HTTP::Tiny` over `http://` | `200`, 559 bytes | `200`, 559 bytes |
| `HTTP::Tiny` over `https://` | **`599 Internal Exception`** — see below | `200` |
| `Cro::HTTP::Client` over `http://` | `200` | `200` |
| `Cro::HTTP::Client` over `https://` | **hangs** | `200` |
| `Cro::HTTP::Server` + `Cro::HTTP::Router` | serves, routes, answers | same |

The Cro server is a live regression here — `t/regression/cro-live-server.raku`
starts a real `Cro::HTTP::Server` with a router and fetches from it over real
sockets in the same process.

The two failures are both Raku++ gaps, and neither is in the TLS transport:

- **`HTTP::Tiny` over https** reports `HTTPS requests not supported:
  IO::Socket::SSL:ver<0.0.2+> must be installed` even when that module is
  installed and `use IO::Socket::SSL` loads it. Its `can-ssl` probe asks
  `$*REPO.repo-chain.map(*.?candidates: 'IO::Socket::SSL', :ver<0.0.2+>)`, and
  Raku++ has no `.candidates` method on a `CompUnit::Repository` — the `.?`
  call answers `Nil`, so the probe reads as "not installed".
- **`Cro::HTTP::Client` over https** hangs. The layers under it do not:
  `IO::Socket::Async::SSL.connect` returns a verified connection, and
  `Cro::TLS::Connector.connect` returns its transform.

## TLS

Encryption is a module too — `IO::Socket::Async::SSL`, driving the system
OpenSSL through Raku++'s own NativeCall. Certificates are verified by default.
It is the plain client from above with `::SSL` on the class and 443 for the
port:

```raku
use IO::Socket::Async::SSL;

my $conn = await IO::Socket::Async::SSL.connect('example.com', 443);
await $conn.write("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n".encode);

my $response = Buf.new;
react { whenever $conn.Supply(:bin) -> $chunk { $response.append($chunk) } }

say $response.decode('latin-1').lines[0];
say "received {$response.bytes} bytes";
```
```
HTTP/1.1 200 OK
received 828 bytes
```

The same request and the same 828 bytes as the unencrypted client, decrypted on
the way in. A self-signed, expired, untrusted-root or wrong-host certificate is
refused with `X::IO::Socket::Async::SSL::Verification`; `:insecure` skips the
checks, leaving the transport encrypted but unauthenticated.
[NETWORKING.md](../NETWORKING.md) has the verification detail.

## Why there is no HTTP client in the engine, even for its own installer

`rakupp install` and `rakupp upgrade` both fetch over the network, and both do
it by running `curl` in a subprocess; unpacking is `tar`, the same way. No HTTP
client, no TLS stack and no tar reader lives in the engine, in any language, so
it carries no certificate store, no redirect policy and no protocol version to
keep current.

JSON is the exception, and a deliberate one. The ecosystem index *is* JSON, and
the engine parses it with a codec of its own — `Rakupp::Internals::JSON`, also
reachable under the `Rakudo::Internals::JSON` name the ecosystem uses:

```raku
say Rakupp::Internals::JSON.from-json('{"ver":"1.2.3"}');
```
```
{ver => 1.2.3}
```

Nothing is installed for that to work, which is the point: an installer that
needed a JSON module before it could install anything would have nowhere to
start.

### Where that name comes from

It is borrowed, not invented. Raku's standard library has no dependency-free
JSON, so the toolchain leans on an undocumented Rakudo internal instead — and
the clearest example is **zef**, whose own `from-json` and `to-json` are
one-line wrappers around `::("Rakudo::Internals::JSON")`, looked up dynamically
so zef compiles anywhere and only needs the name to exist when it runs. Zef's
comment beside them gives the reason, and it is the same one as above: the
compiler can already parse JSON, so making the installer depend on a module for
it would be absurd — it is the thing that installs modules.

zef is not alone. `OpenSSL` reads its own `resources/libraries.json` that way,
and nine of the two hundred most-depended-on distributions call into
`Rakudo::Internals` for something — JSON, a Windows test, a directory walk.

So any engine that wants to run real code has to answer a name from another
implementation's private surface. Raku++ answers it, and the code behind it is
its own, which is why there are two spellings for one codec:
`Rakupp::Internals::JSON` is the first-party name, what Raku++'s own tooling
calls; `Rakudo::Internals::JSON` is a compatibility alias. The alias says who
is asking, not where the code came from — there is no Rakudo in it.
[RAKUDO-INTERNALS.md](../../dev/ecosystem/RAKUDO-INTERNALS.md) has the full
census and the policy.

Programs that want HTTP reach for a module, exactly as they do on Rakudo.

## Limits worth knowing

- **IPv4 only.** Every socket path in the engine is `AF_INET`; there is no IPv6.
- **`IO::Socket::INET.new` throws** `X::AdHoc` on a refused connect or a failed
  bind, as Rakudo's does. The message text differs: Raku++ names the endpoint
  (`Cannot connect to 127.0.0.1:1: Connection refused`), Rakudo does not
  (`Could not connect to socket: Connection refused`).

---

See also [NETWORKING.md](../NETWORKING.md) for the socket API in full, and
[modules.md](modules.md) for installing one.

Back to the [FAQ index](README.md).
