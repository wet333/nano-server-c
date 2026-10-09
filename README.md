# Nano Server C

A minimal HTTP/1.1 static file server written from scratch in C, using only the C standard library and POSIX sockets.

Nano Server C is a learning project. It shows what a web server does under the hood: it opens a TCP socket, reads raw bytes from a client, parses the HTTP request line and writes a well-formed HTTP response back. The whole server is under 600 lines of C and has no external dependencies.

For a walkthrough of the internals, with diagrams, see the [technical documentation](Docs.md).

## Features

- Serves static files from the directory it is started in.
- Parses the HTTP request line to get the method and the requested path.
- Builds HTTP/1.1 responses with a status line and the `Content-Type`, `Content-Length`, `Connection` and `Server` headers.
- Picks the `Content-Type` from the file extension (HTML, CSS, JSON, PNG and plain text).
- Sends files in 4 KB blocks, so a large file is never loaded fully into memory.
- Returns `404 Not Found` with a JSON body when the requested file doesn't exist.

## Quick start

You need Linux or macOS, `gcc` and `make`.

```bash
make          # compiles src/ into bin/server
cd bin
./server      # listens on port 8080
```

Open <http://localhost:8080/index.html> in a browser, or use curl:

```bash
curl -i http://localhost:8080/index.html
```

The `bin/` directory also holds a demo site: a medieval castle's homepage in 1990s web style. Each "chamber" is one page about one HTTP feature the server implements (the request line, methods, status codes, headers, content types and streaming a 2 MB file), with a button that sends a real request and shows the real answer. All links are relative, so the site also works if you start the server from the project root and open <http://localhost:8080/bin/index.html>.

Press `Ctrl+C` to stop the server. It closes its socket before exiting, and you can start it again right away.

Each request is logged to the console:

```text
Server initialized on port 8080

Listening for connections...
Request Method: GET
Request Path: index.html
Reading file: index.html
File index.html sent.
Listening for connections...
```

> [!WARNING]
> `make clean` deletes the whole `bin/` directory, including the sample site. Restore it with `git checkout -- bin`.

## How it works

```text
  client                                       nano-server-c
    │                                                │  socket() → bind() → listen()
    │  TCP connect to :8080                          │
    │ ─────────────────────────────────────────────▶ │  accept()
    │                                                │
    │  GET /pages/status.html HTTP/1.1               │  recv() up to 4 KB
    │  Host: localhost:8080                          │  parse method and path
    │ ─────────────────────────────────────────────▶ │  "/pages/status.html" → ./pages/status.html
    │                                                │
    │  HTTP/1.1 200 OK                               │  send() status line + headers
    │  Content-Type: text/html; charset=UTF-8        │
    │  Content-Length: 9967                          │
    │  ...                                           │
    │                                                │
    │  <file contents>                               │  send() file in 4 KB chunks
    │ ◀───────────────────────────────────────────── │
    │                                                │  close(), then back to accept()
```

1. **Startup.** `init_server()` creates an IPv4 TCP socket, binds it to port 8080 on all network interfaces and starts listening.
2. **Accept.** The main loop in `main.c` blocks on `accept()` and handles one client at a time.
3. **Read and parse.** `read_request()` reads up to 4 KB from the socket and parses the first line of the request into an `HttpRequest` struct with the method and the path.
4. **Resolve the path.** The leading `/` is removed and the path is opened relative to the server's working directory. For example, `/pages/status.html` becomes `./pages/status.html`.
5. **Respond.** `send_file()` fills an `HttpResponse` struct, serializes the status line and headers, and sends them. Then `stream_file_to_client()` sends the file body in 4 KB chunks. If the file can't be opened, the server sends a 404 response instead.
6. **Close.** The server closes the connection (`Connection: close`) and waits for the next client.

## HTTP protocol support

This section lists which parts of HTTP the server implements. ✅ means implemented, ⚠️ means partly implemented and ❌ means not implemented.

### Requests

| Feature | Status | Notes |
| --- | --- | --- |
| Request line | ✅ | The method and the path are taken from the first line. |
| Methods | ⚠️ | `GET`, `POST`, `PUT`, `DELETE`, `PATCH`, `HEAD` and `OPTIONS` are recognized and logged, but every request is answered as a `GET`. Unknown methods are also answered. |
| HTTP version | ❌ | The version in the request line is ignored. |
| Request headers | ❌ | They are read from the socket but not parsed. |
| Request body | ❌ | Ignored. |
| Query strings | ❌ | `/index.html?v=1` is treated as a file literally named `index.html?v=1`, so it returns 404. |
| Percent-encoding | ❌ | Sequences such as `%20` are not decoded. |
| Request size | ⚠️ | Only one `recv()` call is made, so only the first 4 KB of a request is read. |

### Responses

| Feature | Status | Notes |
| --- | --- | --- |
| Status line | ✅ | Always `HTTP/1.1`. |
| Status codes | ⚠️ | Only `200 OK` and `404 Not Found` are sent. `201`, `400`, `401`, `403` and `500` are defined in the `HttpStatus` enum but not used yet. |
| `Content-Type` | ✅ | Chosen from the file extension. See [MIME types](#mime-types). |
| `Content-Length` | ✅ | The size of the file in bytes. |
| `Connection: close` | ✅ | One request per connection. Keep-alive is not supported. |
| `Server` | ✅ | `AWetServerV1.0`, defined in `include/constants.h`. |
| Custom headers | ✅ | Up to 20 per response with `response_add_header()`. Every `200` response includes a demo header, `Server-Token: 987654321`. |
| `HEAD` without a body | ❌ | `HEAD` responses include the full body. |
| Chunked transfer encoding | ❌ | The file is sent in 4 KB pieces, but as a normal body with `Content-Length`, not with `Transfer-Encoding: chunked`. |
| `Date`, caching (`ETag`, `Last-Modified`), `Range`, compression | ❌ | Not implemented. |
| Default file for `/` | ❌ | `GET /` returns 404. There is no automatic `index.html` and no directory listing. |
| HTTPS | ❌ | Plain HTTP only. |

### MIME types

| Extension | `Content-Type` |
| --- | --- |
| `.html`, `.htm` | `text/html; charset=UTF-8` |
| `.css` | `text/css; charset=UTF-8` |
| `.json` | `application/json` |
| `.png` | `image/png` |
| Any other extension, or no extension | `text/plain; charset=UTF-8` |

To add a type, add a value to the `MimeType` enum and update `get_mime_string()` in [src/http_response.c](src/http_response.c) and `get_mimetype_for_file()` in [src/file_handler.c](src/file_handler.c).

### Example responses

A file that exists:

```text
$ curl -i http://localhost:8080/index.html
HTTP/1.1 200 OK
Content-Type: text/html; charset=UTF-8
Content-Length: 777
Connection: close
Server: AWetServerV1.0
Server-Token: 987654321

<!DOCTYPE html>
...
```

A file that doesn't exist:

```text
$ curl -i http://localhost:8080/missing.html
HTTP/1.1 404 Not Found
Content-Type: application/json
Content-Length: 32
Connection: close
Server: AWetServerV1.0

{"Error 404": "File Not found."}
```

## Project structure

```text
nano-server-c/
├── src/
│   ├── main.c            # Entry point: accept loop, request logging, calls send_file()
│   ├── server.c          # Socket setup: socket(), bind(), listen()
│   ├── http_request.c    # Reads the request and parses the method and path
│   ├── http_response.c   # Response struct, status phrases, MIME strings, header serialization
│   ├── file_handler.c    # Opens files, picks the MIME type, sends files to the client
│   └── utils.c           # long_to_string() helper (not used yet)
├── include/              # A header for each module, plus constants.h
├── bin/                  # The compiled server and the demo site it serves
│   ├── index.html        # Home page: the map of all chambers
│   ├── pages/            # One page per HTTP feature
│   ├── styles.css        # The one stylesheet for every page
│   ├── images/           # Parchment and stone background tiles (PNG)
│   ├── image_0001.png    # 2 MB image for the streaming demo
│   └── samples/          # info.json and notes.txt, to test other MIME types
├── build/                # Object files (generated, ignored by git)
├── DESIGN.md             # Design decisions for the demo site
├── Makefile
└── compile_flags.txt     # Include path for clangd and other editor tools
```

## Configuration

The server has no command-line options. All settings are constants in the source code, so you must rebuild after you change them. `make` doesn't detect changes to header files, so after you edit `include/constants.h`, run `rm -rf build && make`.

| Setting | Location | Default |
| --- | --- | --- |
| Port | `init_server(8080)` in [src/main.c](src/main.c) | `8080` |
| Buffer size for requests and file chunks | `BUFFER_SIZE` in [include/constants.h](include/constants.h) | `4096` bytes |
| `Server` header value | `SERVER_NAME` in [include/constants.h](include/constants.h) | `AWetServerV1.0` |
| Pending connection queue | `listen(..., 3)` in [src/server.c](src/server.c) | `3` |

## Limitations and known issues

Nano Server C is made for learning, not for production use.

- **Don't expose it to an untrusted network.** Request paths are not checked, so a client can use `..` segments or an absolute path (for example `GET //etc/passwd`) to read any file that the server process can read. The server also listens on all network interfaces, so other devices on your network can reach it.
- **One client at a time.** The server is single-threaded and blocking. A slow client delays every other client.
- **Directories.** A request for a directory, such as `/pages`, returns `200` with an invalid `Content-Length` and an empty body.

See [Known issues in Docs.md](Docs.md#12-known-issues) for the full list, with the location of each problem and how to fix it.
