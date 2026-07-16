# rakus — a static HTTP file server

Point it at a directory and it serves the files inside over HTTP/1.1 — the Raku
answer to `python3 -m http.server`, on nothing but `IO::Socket::INET`. Where the
[pastebin](../pastebin/) is a single-purpose app with a hand-wired route table,
rakus is a reusable *server*: give it a folder and it figures out the rest.

## Run it

```sh
build/rakupp showcase/rakus/rakus.raku                  # serves ./public on :8080
build/rakupp showcase/rakus/rakus.raku 9000             # choose the port
build/rakupp showcase/rakus/rakus.raku 9000 ~/site      # choose port and root
# or compile a standalone binary:
build/rakupp --exe -o rakus showcase/rakus/rakus.raku && ./rakus 8080 ~/site
```

Then open <http://127.0.0.1:8080/>. With no root given it serves the bundled
[`public/`](public/) folder — a landing page, a stylesheet, an SVG logo, and a
`files/` directory with no index (so you can see the auto listing).

## What it does

- **Correct `Content-Type` by extension** — text *and* binary. HTML, CSS, JS,
  JSON, SVG, PNG, JPEG, … (files are read as raw bytes and streamed back, so
  images arrive intact).
- **`index.html`** when a directory has one; otherwise an **auto directory
  listing** with sizes and links.
- **`GET` and `HEAD`**; other methods get `405`.
- **`301`** to add a missing trailing slash on a directory (so relative links
  resolve), **`403`** on `..` path traversal, **`404`** for anything missing.
- **Concurrent** — one `start` thread per connection.
- Logs each request to stderr: `  200 GET  /style.css`.

## From the command line

```
$ curl -sI http://127.0.0.1:8080/logo.svg
HTTP/1.1 200 OK
Content-Type: image/svg+xml
Content-Length: 289
Server: rakus
Connection: close

$ curl -s http://127.0.0.1:8080/files/ | grep -o 'Index of[^<]*'
Index of /files/

$ curl -so /dev/null -w '%{http_code}\n' http://127.0.0.1:8080/nope
404
```

## How it works

- **No HTTP library.** Each connection is read with `recv` until the header
  terminator; the request line is split into method and target by hand.
- **Path handling.** The target is URL-decoded and the query stripped; a `..`
  segment is rejected; the rest is joined onto the (absolute) document root.
- **Serving.** A directory resolves to `index.html` or a generated listing; a
  file is `slurp`-ed as a `Buf` and its bytes written straight to the socket with
  a `Content-Length`. The headers go out with `print`, the body with `write` — so
  binary files are byte-exact.
- **Byte-exact bodies.** Every response body is a `Buf` (generated HTML is
  `.encode`-d), so `Content-Length` is always the true byte count.
