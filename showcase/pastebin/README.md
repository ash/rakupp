# pastebin — an HTTP server on raw sockets

A pastebin served over HTTP/1.1, written directly on `IO::Socket::INET` with no
framework. It parses requests by hand, keeps pastes in memory, and serves a small
HTML UI plus a raw-text endpoint. Compile it and you have a single native binary
you can drop on a host and point a URL at.

## Run it

```sh
build/rakupp showcase/pastebin/pastebin.raku       # then open http://127.0.0.1:8080
PORT=9000 build/rakupp showcase/pastebin/pastebin.raku
# or compile a standalone binary:
build/rakupp --exe -o pastebin showcase/pastebin/pastebin.raku && ./pastebin
```

Open `http://127.0.0.1:8080` in a browser: paste some text, submit, and you land
on the paste's page. The server terminal logs each request (`  GET /`, `  POST /paste`).

## Routes

| Route | What it does |
|---|---|
| `GET /` | create form + list of recent pastes |
| `POST /paste` | store form field `content=…` → `303` redirect to the new paste |
| `GET /p/<id>` | view one paste (HTML) |
| `GET /raw/<id>` | view one paste (`text/plain`) |

## From the command line

```
$ curl -i -X POST --data 'content=hello from curl' http://127.0.0.1:8080/paste
HTTP/1.1 303 See Other
Location: /p/100

$ curl http://127.0.0.1:8080/raw/100
hello from curl
```

Paste ids are short base-36 (`100`, `101`, …).

## How it works

- **No HTTP library.** Each accepted connection is read with `recv`; the request
  line and headers are split by hand, and for a `POST` the body is pulled until
  `Content-Length` bytes have arrived.
- **Routing** is a small `given`/`when` over the method and path (with a regex for
  `/p/<id>` and `/raw/<id>`); `handle-request` is a pure function of
  `(method, path, body)`, which is what the test suite drives.
- **Responses** are assembled as raw HTTP: a status line, `Content-Type` /
  `Content-Length` / `Connection: close`, a blank line, then the body. The CSS lives
  in a non-interpolating `Q` heredoc so its braces stay literal.
- **Store** — an in-memory hash plus an insertion-order list for the "recent" view.
