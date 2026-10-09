# Nano Server C: Technical Documentation

This document explains how Nano Server C works on the inside: how a request moves through the code, what each module does, what the data structures look like and how to extend the server. For a short overview, build instructions and the list of supported HTTP features, see the [README](README.md).

The diagrams use [Mermaid](https://mermaid.js.org/), which GitHub renders automatically. To see them in the VS Code preview, install the *Markdown Preview Mermaid Support* extension.

All console output, traces and byte counts in this document come from running the code in this repository. Code marked as a **suggested change** is not in the repository yet.

## Contents

1. [Architecture](#1-architecture)
2. [Request lifecycle](#2-request-lifecycle)
3. [Socket setup: `server.c`](#3-socket-setup-serverc)
4. [Request parsing: `http_request.c`](#4-request-parsing-http_requestc)
5. [Response building: `http_response.c`](#5-response-building-http_responsec)
6. [File serving: `file_handler.c`](#6-file-serving-file_handlerc)
7. [Constants, utilities and memory](#7-constants-utilities-and-memory)
8. [Error handling](#8-error-handling)
9. [Build system](#9-build-system)
10. [Inspecting and debugging the server](#10-inspecting-and-debugging-the-server)
11. [Extending the server](#11-extending-the-server)
12. [Known issues](#12-known-issues)

---

## 1. Architecture

### Design at a glance

| Aspect | Design |
| --- | --- |
| Concurrency | One process, one thread, blocking I/O. Clients are served one after another. This is called an *iterative* server. |
| Connections | One request per connection. The server closes the socket after every response. |
| Content | Static files only, read from the server's current working directory. |
| Request handling | Every request is treated as a file download, whatever its method. |
| Dependencies | The C standard library and POSIX sockets. Nothing else. |

### Modules

```mermaid
graph TD
    main["main.c<br/>accept loop and logging"]
    server["server.c<br/>socket setup"]
    request["http_request.c<br/>request parsing"]
    file["file_handler.c<br/>file serving"]
    response["http_response.c<br/>response building"]
    constants["constants.h<br/>BUFFER_SIZE, SERVER_NAME"]
    utils["utils.c<br/>long_to_string (unused)"]

    main --> server
    main --> request
    main --> file
    file --> response
    main -.-> constants
    request -.-> constants
    file -.-> constants
    response -.-> constants
```

Solid arrows are function calls. Dotted arrows mean that the module includes `constants.h`. No module calls `utils.c` yet.

| Source | Header | Responsibility |
| --- | --- | --- |
| [src/main.c](src/main.c) | none | Entry point. Starts the server, runs the accept loop, logs each request and calls `send_file()`. |
| [src/server.c](src/server.c) | [include/server.h](include/server.h) | Creates the TCP socket, binds it to a port and starts listening. |
| [src/http_request.c](src/http_request.c) | [include/http_request.h](include/http_request.h) | Reads the request from the socket and extracts the method and the path. |
| [src/http_response.c](src/http_response.c) | [include/http_response.h](include/http_response.h) | Response data model, status phrases, MIME strings and serialization of the status line and headers. |
| [src/file_handler.c](src/file_handler.c) | [include/file_handler.h](include/file_handler.h) | Opens the requested file, picks its MIME type and streams it to the client. |
| [src/utils.c](src/utils.c) | [include/utils.h](include/utils.h) | The `long_to_string()` helper. Not used yet. |
| none | [include/constants.h](include/constants.h) | Constants shared by all modules. |

---

## 2. Request lifecycle

### Sequence

This is what happens when a browser asks for `/styles.css`:

```mermaid
sequenceDiagram
    autonumber
    participant C as Client
    participant M as main.c
    participant R as http_request.c
    participant F as file_handler.c
    participant H as http_response.c
    participant D as Disk

    M->>M: init_server(8080)
    C->>M: TCP connect, returned by accept()
    C->>M: GET /styles.css HTTP/1.1
    M->>R: read_request(fd, &request, buffer, 4096)
    R-->>M: method = GET, path = "styles.css"
    M->>F: send_file(fd, "styles.css")
    F->>D: fopen() to check that the file exists
    F->>D: get_file_size()
    F->>H: response_init(), response_add_header()
    F->>H: get_response_size()
    H-->>F: status line and headers in a buffer
    F->>C: send() status line and headers
    loop until the whole file is sent
        F->>D: fread() up to 4 KB
        F->>C: send() the chunk
    end
    F-->>M: return
    M->>M: free_request(&request)
    M->>C: close(fd)
```

### Main loop

[src/main.c](src/main.c) runs this loop forever:

```mermaid
flowchart TD
    A(["start"]) --> B["init_server(8080)<br/>socket, bind, listen"]
    B --> C["print 'Listening for connections...'"]
    C --> D{"accept()"}
    D -- "returns -1" --> E["perror('Accept failed')"]
    E --> C
    D -- "returns client fd" --> F{"read_request()"}
    F -- "n bytes" --> G["log method and path"]
    G --> H["send_file(fd, path)"]
    H --> I["free_request()"]
    I --> J["close(fd)"]
    F -- "0, client closed" --> K["log 'Client closed connection'"]
    K --> J
    F -- "-1, error" --> L["log 'Error reading request'"]
    L --> J
    J --> C
```

### What the console shows

Each step prints a line, so the console log follows the request:

```text
Server initialized on port 8080          ← init_server() finished

Listening for connections...             ← waiting in accept()
Request Method: GET                      ← main.c, after read_request()
Request Path: styles.css
Reading file: styles.css                 ← send_file() starts
File styles.css sent.                    ← last chunk sent
Listening for connections...             ← back to accept()
```

### What the operating system sees

`strace` lists every system call the server makes. This is the trace for one `curl http://localhost:8080/styles.css`, filtered to the interesting calls:

```bash
cd bin
strace -e trace=socket,bind,listen,accept,recvfrom,sendto,openat,close ./server
```

```text
socket(AF_INET, SOCK_STREAM, IPPROTO_IP) = 3
bind(3, {sa_family=AF_INET, sin_port=htons(8080), sin_addr=inet_addr("0.0.0.0")}, 16) = 0
listen(3, 3)                            = 0
accept(3, {sa_family=AF_INET, sin_port=htons(46542), sin_addr=inet_addr("127.0.0.1")}, [16]) = 4
recvfrom(4, "GET /styles.css HTTP/1.1\r\nHost: "..., 4095, 0, NULL, NULL) = 87
openat(AT_FDCWD, "styles.css", O_RDONLY) = 5
openat(AT_FDCWD, "styles.css", O_RDONLY) = 6
close(6)                                = 0
sendto(4, "HTTP/1.1 200 OK\r\nContent-Type: t"..., 131, 0, NULL, 0) = 131
openat(AT_FDCWD, "styles.css", O_RDONLY) = 6
sendto(4, "body {\n    font-family: 'Times N"..., 2460, 0, NULL, 0) = 2460
close(6)                                = 0
close(5)                                = 0
close(4)                                = 0
accept(3, ...
```

How to read it:

- **fd 3** is the listening socket and **fd 4** is the connection with this client. See [section 3](#3-socket-setup-serverc).
- `recv()` and `send()` show up as `recvfrom` and `sendto` because glibc implements them with those system calls.
- `recvfrom` asks for at most 4095 bytes: `BUFFER_SIZE - 1`, which leaves room for the `'\0'` terminator.
- The file is opened **three times**: once by `send_file()` to check that it exists, once by `get_file_size()` and once by `stream_file_to_client()`. See [section 6](#6-file-serving-file_handlerc).
- The headers and the body are sent with separate `sendto` calls. The headers take 131 bytes instead of the intended 148 because of a serialization bug. See [section 5](#serialization-layout-and-the-header-bug).
- The body is sent in a single call because `styles.css` (2,460 bytes) fits in one 4 KB chunk.

---

## 3. Socket setup: `server.c`

### `HttpServer`

```c
typedef struct {
    int socket_fd;              // listening socket
    int port;                   // port passed to init_server()
    struct sockaddr_in address; // IPv4 address the socket is bound to
} HttpServer;
```

### `HttpServer init_server(int port)`

| Step | Call | What it does |
| --- | --- | --- |
| 1 | `socket(AF_INET, SOCK_STREAM, 0)` | Creates an IPv4 TCP socket. |
| 2 | Fills `sockaddr_in` | `INADDR_ANY` means all network interfaces (`0.0.0.0`). `htons(port)` converts the port to network byte order. |
| 3 | `bind()` | Attaches the socket to the port. If the port is taken, the server prints the error and exits. |
| 4 | `listen(fd, 3)` | Marks the socket as passive. The kernel queues up to 3 connections while the server is busy with another client. |

If any step fails, the function calls `perror()` and `exit(EXIT_FAILURE)`.

### Listening socket and connected socket

A TCP server uses two kinds of sockets. The listening socket only accepts new connections. Each call to `accept()` returns a new connected socket that is used to talk to that one client:

```text
                         ┌───────────────────────────────────────┐
  client ── connect() ──▶│ fd 3: listening socket                │  created once by init_server()
                         │ bound to 0.0.0.0:8080, never closed   │
                         └───────────────────┬───────────────────┘
                                             │ accept()
                                             ▼
                         ┌───────────────────────────────────────┐
  client ◀─── bytes ────▶│ fd 4: connected socket                │  one per client
                         │ recv() the request, send() a response │  closed after the response
                         └───────────────────────────────────────┘
```

### Notes on the socket code

- **`SO_REUSEADDR` is not set.** The variable `opt` is declared but `setsockopt()` is never called. The server closes every connection itself, so the kernel keeps the port in the `TIME_WAIT` state for about a minute. A restart during that time fails with `Address already in use`. See [the fix](#reuse-the-port-after-a-restart).
- **The error check for `socket()` compares with `0`** ([src/server.c:14](src/server.c#L14)), but `socket()` returns `-1` on failure. A failure there goes unnoticed, and the error appears one step later in `bind()`.
- **`main.c` passes `server.address` to `accept()`**, so the client's address overwrites the server's address in that struct. This does no harm right now, because the struct isn't used after `bind()`.

---

## 4. Request parsing: `http_request.c`

### Data structures

```c
typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_UNKNOWN
} HttpMethod;

typedef struct {
    HttpMethod method;       // parsed from the request line
    char *path;              // heap copy of the path without its leading '/', or NULL
    char *raw_request;       // points into the caller's buffer, not owned
    size_t raw_request_size; // number of bytes received
} HttpRequest;
```

`read_request()` allocates `path` with `malloc()`, and `free_request()` releases it. `raw_request` borrows the buffer that the caller passed in, so it is valid only while that buffer is.

### Parsing functions

| Function | Returns | Purpose |
| --- | --- | --- |
| `int read_request(int client_socket, HttpRequest *req, char *buffer, size_t buffer_size)` | Bytes read, `0` if the client closed the connection, `-1` on error | Reads the request once and fills `req`. |
| `HttpMethod parse_method(const char *request_line)` | The method, or `HTTP_METHOD_UNKNOWN` | Compares the start of the line with the known method names. |
| `int parse_path(const char *request_line, char *path_buffer, size_t path_buffer_size)` | `0`, or `-1` if the line has no space | Copies the path, without its leading `/`, into `path_buffer`. |
| `void free_request(HttpRequest *req)` | nothing | Frees `req->path`. |

### Step by step

Take this request, as curl sends it:

```text
GET /pages/tutorial.html HTTP/1.1\r\n
Host: localhost:8080\r\n
Accept: */*\r\n
\r\n
```

1. **Read.** `recv()` reads up to 4,095 bytes into the 4,096-byte buffer, and the data is `'\0'`-terminated. There is only one `recv()` call, so anything after the first 4,095 bytes is never read.
2. **Isolate the request line.** The code looks for the first `\r\n`, then for `\n`, and if it finds neither it uses the whole buffer. That line is copied into `request_line_copy`. The headers after it are ignored.
3. **Parse the method.** `parse_method()` compares the start of the line with `"GET "`, `"POST "` and so on. The trailing space makes sure that a line such as `GETX /` doesn't match `GET`.
4. **Parse the path.** `parse_path()` works with two pointers:

   ```text
     G E T   / p a g e s / t u t o r i a l . h t m l   H T T P / 1 . 1
           ▲   ▲                                     ▲
           │   │                                     └─ path_end   = strchr(path_start, ' ')
           │   └─ path_start = first space + 2  (skips the space and the '/')
           └─ first space = strchr(request_line, ' ')

     path = "pages/tutorial.html"   (path_end - path_start = 19 bytes)
   ```

5. **Store the path.** The result is copied to the heap and saved in `req->path`.

### Parsing results

These results come from calling `parse_method()` and `parse_path()` directly:

| Request line | `method` | `path` | Notes |
| --- | --- | --- | --- |
| `GET /index.html HTTP/1.1` | `GET` | `"index.html"` | |
| `GET /pages/tutorial.html HTTP/1.1` | `GET` | `"pages/tutorial.html"` | |
| `POST /index.html HTTP/1.1` | `POST` | `"index.html"` | Served like a GET. |
| `DELETE /index.html HTTP/1.1` | `UNKNOWN` | `"index.html"` | ⚠️ Bug, explained below. |
| `GET / HTTP/1.1` | `GET` | `""` | An empty path. `fopen("")` fails, so the response is 404. |
| `GET /index.html?v=1 HTTP/1.1` | `GET` | `"index.html?v=1"` | The query string becomes part of the file name. |
| `GET //etc/passwd HTTP/1.1` | `GET` | `"/etc/passwd"` | ⚠️ An absolute path. See [Known issues](#12-known-issues). |
| `HELLO` | `UNKNOWN` | `NULL` | No space, so `parse_path()` returns `-1`. |

**Why `DELETE` is never recognized.** [src/http_request.c:94](src/http_request.c#L94) calls `strncmp(request_line, "DELETE ", 8)`. `"DELETE "` is only 7 characters long, so the 8th character compared is the `'\0'` at the end of the literal. In a real request that position holds the `/` of the path, so the comparison never matches. The fix is to compare 7 characters.

**Hidden assumptions in `parse_path()`:**

- It skips 2 characters after the first space without checking them. It assumes that the path starts with `/`.
- If the line ends right after the method and one space (`"GET "`), `path_start` points one byte past the end of the string, into uninitialized stack memory.
- If the path is `NULL` (the `HELLO` case), `main.c` still calls `send_file(NULL)`. The log shows `Reading file: (null)`, `fopen()` fails with `Bad address` and the client gets a 404.

---

## 5. Response building: `http_response.c`

### Status codes

| Enum value | Code | `get_status_phrase()` | Sent by the server |
| --- | --- | --- | --- |
| `HTTP_OK` | 200 | `"OK"` | ✅ |
| `HTTP_CREATED` | 201 | `"Created"` | |
| `HTTP_BAD_REQUEST` | 400 | `"Bad Request"` | |
| `HTTP_UNAUTHORIZED` | 401 | `"Unknown"` ⚠️ | |
| `HTTP_FORBIDDEN` | 403 | `"Unknown"` ⚠️ | |
| `HTTP_NOT_FOUND` | 404 | `"Not Found"` | ✅ |
| `HTTP_INTERNAL_ERROR` | 500 | `"Internal Server Error"` | |

`get_status_phrase()` has no `case` for 401 and 403, so they get the default phrase `"Unknown"`.

### MIME types

| Enum value | `get_mime_string()` |
| --- | --- |
| `MIME_TEXT_HTML` | `text/html; charset=UTF-8` |
| `MIME_TEXT_CSS` | `text/css; charset=UTF-8` |
| `MIME_TEXT_PLAIN` | `text/plain; charset=UTF-8` |
| `MIME_APPLICATION_JSON` | `application/json` |
| `MIME_IMAGE_PNG` | `image/png` |
| Any other value | `application/octet-stream` |

### `HttpResponse`

```c
typedef struct {
    char *key;
    char *value;
} HttpHeader;

typedef struct {
    HttpStatus status;
    MimeType content_type;

    HttpHeader extra_headers[20]; // custom headers, in the order they were added
    int header_count;

    char *body;                   // in-memory body, or NULL when the body is streamed
    size_t body_length;           // value of the Content-Length header
} HttpResponse;
```

`response_add_header()` and `response_set_body()` store **pointers** and don't copy the strings. The strings must stay valid until the response has been serialized. String literals always meet this requirement.

### Response functions

| Function | Purpose |
| --- | --- |
| `response_init(res, status, type)` | Sets the status and the content type, and clears the headers and the body. |
| `response_add_header(res, key, value)` | Adds a custom header. After 20 headers, new ones are silently ignored. |
| `response_set_body(res, body)` | Sets an in-memory body. The length is computed with `strlen()`, so it only works for text. |
| `get_response_size(res, buffer, buffer_size)` | Despite its name, it **serializes** the response into `buffer` and returns the number of bytes written. |
| `get_status_phrase(status)`, `get_mime_string(type)` | Convert an enum value to its text. |

### Example: building a response

```c
HttpResponse res;
char buffer[BUFFER_SIZE];

response_init(&res, HTTP_OK, MIME_TEXT_PLAIN);
response_add_header(&res, "Cache-Control", "no-store");
response_set_body(&res, "Hello from Nano Server C\n");

int length = get_response_size(&res, buffer, sizeof(buffer));
send(client_socket, buffer, length, 0);
```

The response this is meant to produce (every line ends in `\r\n`):

```text
HTTP/1.1 200 OK
Content-Type: text/plain; charset=UTF-8
Content-Length: 25
Connection: close
Server: AWetServerV1.0
Cache-Control: no-store

Hello from Nano Server C
```

Because of the bug described [below](#serialization-layout-and-the-header-bug), the current code produces `Server:Cache-Control: no-store` instead of the two separate lines.

### Two ways to send a body

```text
 In-memory body (the 404 response)             Streamed body (files)
 ─────────────────────────────────             ─────────────────────
 response_set_body(&res, json)                 res.body_length = file size  (body stays NULL)
 get_response_size() writes headers            get_response_size() writes headers only
   AND the body into one buffer                send() the headers
 send() the buffer in one call                 stream_file_to_client() sends the body in 4 KB chunks

 The body must fit in BUFFER_SIZE,             The body can be of any size
   or it is cut off without a warning
 Text only (length comes from strlen)          Works for binary files
```

### Serialization layout and the header bug

`get_response_size()` writes the response with several `snprintf()` calls and keeps a running total in `response_size`:

1. Status line: `snprintf()` returns 17, so `response_size = 17`.
2. Standard headers: written at `buffer + 17`. `snprintf()` returns 104. The code **assigns** the result (`response_size = 104`) instead of **adding** it (`response_size += 104`, which would give 121). See [src/http_response.c:59](src/http_response.c#L59).
3. Custom headers are written at `buffer + 104`. That position is 7 bytes into the `Server` line, so they overwrite it.

The byte layout of the headers for `GET /index.html`:

```text
offset  intended (148 bytes)                         actual (131 bytes)
──────  ───────────────────────────────────────────  ────────────────────────────────────────────
     0  HTTP/1.1 200 OK\r\n                          HTTP/1.1 200 OK\r\n
    17  Content-Type: text/html; charset=UTF-8\r\n   Content-Type: text/html; charset=UTF-8\r\n
    57  Content-Length: 777\r\n                      Content-Length: 777\r\n
    78  Connection: close\r\n                        Connection: close\r\n
    97  Server: AWetServerV1.0\r\n                   Server:
   104                                               Server-Token: 987654321\r\n   ← written at 104
   121  Server-Token: 987654321\r\n
   129                                               \r\n
   131                                               (end)
   146  \r\n
   148  (end)
```

The 404 response is affected too. It has no custom headers, so the empty line that ends the headers lands exactly where the `Server` line starts, and that header disappears. The fix is a single character: change `=` to `+=` on line 59.

---

## 6. File serving: `file_handler.c`

### `send_file()`

```mermaid
flowchart TD
    A["send_file(fd, filename)"] --> B{"fopen(filename)"}
    B -- "NULL" --> C["response_init(404, JSON)<br/>response_set_body(error JSON)"]
    C --> D["get_response_size()<br/>send() headers and body"]
    D --> Z(["return"])
    B -- "file opened" --> E["get_mimetype_for_file()"]
    E --> F["get_file_size()"]
    F --> G["add the Server-Token header"]
    G --> H["get_response_size()<br/>send() headers"]
    H --> I["stream_file_to_client()"]
    I --> J["fclose()"]
    J --> Z
```

| Function | Purpose |
| --- | --- |
| `void send_file(int client_socket, char *filename)` | Sends a whole HTTP response for a file, or a 404 if it can't be opened. |
| `long get_file_size(char *filename)` | Opens the file, seeks to the end and returns the position (`ftell()`), or `-1`. |
| `MimeType get_mimetype_for_file(char *filename)` | Picks the MIME type from the extension. |
| `void stream_file_to_client(int client_socket, char *filename)` | Sends the file contents in `BUFFER_SIZE` chunks. |

### From URL path to file

Paths are resolved against the server's **current working directory**, so the result depends on where you start the server:

| Server started in | Request | File opened | Result |
| --- | --- | --- | --- |
| `bin/` | `GET /index.html` | `bin/index.html` | 200 |
| `bin/` | `GET /pages/tutorial.html` | `bin/pages/tutorial.html` | 200 |
| project root | `GET /index.html` | `./index.html` | 404 |
| project root | `GET /bin/index.html` | `bin/index.html` | 200, but the page's `/pages/tutorial.html` link returns 404 |

### MIME detection

`get_mimetype_for_file()` uses `strrchr(filename, '.')` to find the **last** dot in the whole path, then compares the rest of the string with known extensions. The comparison is case-sensitive:

| File name | Text after the last dot | `Content-Type` |
| --- | --- | --- |
| `index.html` | `.html` | `text/html; charset=UTF-8` |
| `pages/tutorial.html` | `.html` | `text/html; charset=UTF-8` |
| `data.json` | `.json` | `application/json` |
| `archive.tar.gz` | `.gz` | `text/plain; charset=UTF-8` |
| `PHOTO.PNG` | `.PNG` | `text/plain; charset=UTF-8` (uppercase doesn't match) |
| `README` | none | `text/plain; charset=UTF-8` |
| `v1.2/notes` | `.2/notes` | `text/plain; charset=UTF-8` (the dot belongs to a directory name) |

### Streaming

```text
  file on disk                file_buffer[4096]              client socket
 ┌──────────────┐   fread()   ┌──────────────┐    send()    ┌──────────────┐
 │  the file    │ ──────────▶ │ ≤ 4096 bytes │ ───────────▶ │    fd 4      │
 └──────────────┘             └──────────────┘              └──────────────┘
                     repeat until fread() returns 0
```

Memory use stays at 4 KB whatever the file size. For example, `bin/image_0001.png` is 2,056,264 bytes, so it is sent as 502 full chunks plus a final chunk of 72 bytes: 503 `send()` calls.

### Notes on the file handling code

- **Each file is opened three times:** in `send_file()` (only to check that it exists), in `get_file_size()` and in `stream_file_to_client()`. `stream_file_to_client()` doesn't check whether its `fopen()` succeeded, so the server crashes if the file is deleted between the first and the third open.
- **`send()`'s return value is never checked.** If the client disconnects in the middle of a download, the next `send()` raises `SIGPIPE`. By default that signal **kills the server**: the process ends with exit code 141 (128 + 13, the number of `SIGPIPE`). See [the fix](#survive-clients-that-disconnect-early).
- **Directories.** On Linux, `fopen()` succeeds on a directory, `ftell()` reports `LONG_MAX` and `fread()` fails. The client gets `200 OK` with `Content-Length: 9223372036854775807` and an empty body.
- **Every 200 response includes `Server-Token: 987654321`.** It is a test header with no real purpose.

---

## 7. Constants, utilities and memory

### `constants.h`

| Constant | Value | Used for |
| --- | --- | --- |
| `BUFFER_SIZE` | `4096` | The request buffer, the request line copy, the path buffer, the response buffer and the file chunk buffer. |
| `SERVER_NAME` | `"AWetServerV1.0"` | The value of the `Server` header. |

### `long_to_string()`

`char *long_to_string(long number)` converts a number to a string allocated with `malloc()`. It calls `snprintf(NULL, 0, ...)` first to find out how many bytes it needs. The caller must `free()` the result. No other code calls it yet.

### Memory per request

| Buffer | Where | Size | Lives until |
| --- | --- | --- | --- |
| `request_buffer` | `main()`, stack | 4,096 B | The end of the loop iteration |
| `request_line_copy` | `read_request()`, stack | 4,096 B | `read_request()` returns |
| `path_buffer` | `read_request()`, stack | 4,096 B | `read_request()` returns |
| `req->path` | `read_request()`, **heap** | Path length + 1 | `free_request()` |
| `response_buffer` | `send_file()`, stack | 4,096 B | `send_file()` returns |
| `file_buffer` | `stream_file_to_client()`, stack | 4,096 B | `stream_file_to_client()` returns |

`req->path` is the only heap allocation, and `main()` frees it after every request, so serving requests doesn't leak memory.

---

## 8. Error handling

| Situation | What the code does | What happens |
| --- | --- | --- |
| The port is already in use | `bind()` fails, then `perror()` and `exit()` | The server stops with `Error while trying to bind socket: Address already in use`. |
| `accept()` fails | `perror("Accept failed")` and `continue` | The loop goes on. |
| A client connects and closes without sending anything | `recv()` returns 0 | The log shows `Client closed connection`. |
| `recv()` fails | `read_request()` returns -1 | The log shows `Error reading request`. |
| The request line has no space | `path` is `NULL`, and `fopen(NULL)` fails | 404, and the log shows `Reading file: (null)`. |
| The file doesn't exist or can't be read | `fopen()` fails | 404 with a JSON body. A missing permission is also reported as 404, not 403. |
| The path is a directory | `fopen()` succeeds, the size is wrong | 200 with an invalid `Content-Length` and no body. |
| The client disconnects during a download | `send()` raises `SIGPIPE` | **The server process is killed.** |

---

## 9. Build system

```mermaid
flowchart LR
    S["src/*.c"] -- "gcc -Iinclude -Wall -g -c" --> O["build/*.o"]
    O -- "gcc (link)" --> E["bin/server"]
    I["include/*.h"] -.->|"included by the sources,<br/>not tracked by make"| S
```

| Variable | Value | Meaning |
| --- | --- | --- |
| `CC` | `gcc` | Compiler. |
| `CFLAGS` | `-Iinclude -Wall -g` | Header search path, common warnings, debug symbols for gdb. |
| `SRCS` | Every `.c` under `src/` | Found with `find`, so new source files are compiled automatically. |
| `OBJS` | `build/<name>.o` for each source | Built with the pattern rule `build/%.o: src/%.c`. |
| `TARGET` | `bin/server` | The final executable. |

| Command | Effect |
| --- | --- |
| `make` | Compiles the changed `.c` files and links `bin/server`. |
| `make clean` | Deletes `build/` **and the whole `bin/` directory**. |

### Things to watch out for

- **Changing a header doesn't trigger a rebuild.** The pattern rule only depends on the `.c` file. After you edit `constants.h`, plain `make` reports nothing to do. Use `rm -rf build && make` instead, or apply the [suggested fix](#track-header-dependencies-in-the-makefile).
- **`make clean` deletes the sample site**, because `index.html`, `styles.css`, the image and `pages/` live in `bin/` next to the executable. Restore them with `git checkout -- bin`.
- **`bin/server` is tracked by git**, so every rebuild shows up as a modified file in `git status`.
- **`compile_flags.txt`** contains `-Iinclude` so that clangd and other editor tools can find the headers.

---

## 10. Inspecting and debugging the server

### With curl

| Command | What it shows |
| --- | --- |
| `curl -i http://localhost:8080/index.html` | The status line, the headers and the body. |
| `curl -s -D - -o /dev/null http://localhost:8080/index.html` | Only the headers. |
| `curl -i http://localhost:8080/missing.html` | The 404 response and its JSON body. |
| `curl -X DELETE http://localhost:8080/index.html` | A file is served anyway. The console logs `Request Method: UNKNOWN`. |
| `curl --path-as-is http://localhost:8080//etc/hostname` | Shows that absolute paths are not blocked. |

### Raw requests with netcat

netcat lets you type the request bytes yourself, which is useful for testing requests that a browser would never send:

```bash
printf 'GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n' | nc -q 2 localhost 8080
printf 'HEAD /index.html HTTP/1.1\r\n\r\n' | nc -q 2 localhost 8080   # the body is sent anyway
printf 'HELLO\r\n\r\n' | nc -q 2 localhost 8080                       # malformed request line, returns 404
```

### With strace

See [What the operating system sees](#what-the-operating-system-sees) for a full example.

### With gdb

The Makefile compiles with `-g`, so you can stop the server in the middle of a request and step through it. The memory addresses in your session will differ:

```text
$ cd bin
$ gdb -q ./server
(gdb) break send_file
Breakpoint 1 at 0x1551: file src/file_handler.c, line 10.
(gdb) run
Server initialized on port 8080

Listening for connections...
Request Method: GET
Request Path: index.html
                                  ← from another terminal: curl http://localhost:8080/index.html
Breakpoint 1, send_file (client_socket=4, filename=0x55555555b2b0 "index.html") at src/file_handler.c:10
10      void send_file(int client_socket, char *filename) {
(gdb) next
15          printf("Reading file: %s \n", filename);
(gdb) print filename
$1 = 0x55555555b2b0 "index.html"
(gdb) continue
```

---

## 11. Extending the server

Each recipe below is a **suggested change** that is not in the repository yet.

### Add a MIME type

Add a value to the enum, its string and its extension. For JavaScript:

```c
// include/http_response.h
typedef enum {
    MIME_TEXT_HTML,
    MIME_TEXT_CSS,
    MIME_TEXT_PLAIN,
    MIME_APPLICATION_JSON,
    MIME_IMAGE_PNG,
    MIME_TEXT_JAVASCRIPT
} MimeType;

// src/http_response.c, inside get_mime_string()
case MIME_TEXT_JAVASCRIPT: return "text/javascript; charset=UTF-8";

// src/file_handler.c, inside get_mimetype_for_file()
} else if (strcmp(file_extension, ".js") == 0) {
    return MIME_TEXT_JAVASCRIPT;
}
```

### Serve `index.html` for `/`

In `main.c`, before calling `send_file()`:

```c
char *path = request.path;
if (path != NULL && path[0] == '\0') {
    path = "index.html";   // "GET /" serves the home page
}
send_file(new_socket, path);
```

### Ignore the query string

In `main.c`, after `read_request()` (`main.c` also needs `#include <string.h>`):

```c
if (request.path != NULL) {
    char *query = strchr(request.path, '?');
    if (query != NULL) {
        *query = '\0';   // "index.html?v=1" becomes "index.html"
    }
}
```

### Answer `HEAD` without a body

Give `send_file()` a flag and skip the streaming step when it is false:

```c
// include/file_handler.h and src/file_handler.c
void send_file(int client_socket, char *filename, int include_body);

    // in send_file(), replace the streaming call with:
    if (include_body) {
        stream_file_to_client(client_socket, filename);
    }

// src/main.c
send_file(new_socket, request.path, request.method != HTTP_METHOD_HEAD);
```

### Block unsafe paths

Reject absolute paths and `..` segments before you touch the disk, and answer with `403 Forbidden`. This check is strict on purpose: it also rejects harmless names such as `a..b`.

```c
// src/main.c
static int is_safe_path(const char *path) {
    return path != NULL
        && path[0] != '/'               // no absolute paths ("GET //etc/passwd")
        && strstr(path, "..") == NULL;  // no parent directories ("GET /../secret")
}

    // in the main loop, instead of calling send_file() directly:
    if (!is_safe_path(request.path)) {
        HttpResponse res;
        char buffer[BUFFER_SIZE];
        response_init(&res, HTTP_FORBIDDEN, MIME_APPLICATION_JSON);
        response_set_body(&res, "{\"Error 403\": \"Forbidden.\"}");
        int length = get_response_size(&res, buffer, sizeof(buffer));
        send(new_socket, buffer, length, 0);
    } else {
        send_file(new_socket, request.path);
    }
```

Also add `case HTTP_FORBIDDEN: return "Forbidden";` to `get_status_phrase()`, or the status line will say `403 Unknown`.

### Survive clients that disconnect early

Ignore `SIGPIPE` at startup. A failed `send()` then returns `-1` instead of killing the process:

```c
// src/main.c
#include <signal.h>

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    HttpServer server = init_server(8080);
    ...
```

On Linux you can instead pass `MSG_NOSIGNAL` as the last argument of each `send()`.

### Reuse the port after a restart

In `init_server()`, before `bind()`. The variable `opt` already exists:

```c
setsockopt(server.socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

### Track header dependencies in the Makefile

Ask gcc to write a `.d` dependency file next to each object, and include those files. The `-include` line must be at the **end** of the Makefile. If it comes before `all`, the first rule in the `.d` files becomes the default target.

```make
CFLAGS = -Iinclude -Wall -g -MMD -MP
DEPS = $(OBJS:.o=.d)

# ... the rest of the Makefile ...

-include $(DEPS)
```

After this change, editing `include/constants.h` rebuilds exactly the four sources that include it.

### Handle several clients at once

The server is iterative: one slow client blocks everyone else. The usual next steps, from simplest to most scalable:

1. **`fork()` per connection.** The child process handles the client and exits, while the parent goes back to `accept()`.
2. **One thread per connection** with `pthread_create()`.
3. **An event loop** with `poll()` or `epoll()`, where a single thread serves many non-blocking sockets.

---

## 12. Known issues

| Issue | Location | Effect | Fix |
| --- | --- | --- | --- |
| Header length assigned instead of added | [src/http_response.c:59](src/http_response.c#L59) | The `Server` header is overwritten. | `=` → `+=` |
| `DELETE` compared with the wrong length | [src/http_request.c:94](src/http_request.c#L94) | `DELETE` is logged as `UNKNOWN`. | Compare 7 characters. |
| `SIGPIPE` not handled | [src/file_handler.c:85](src/file_handler.c#L85) | A client that disconnects during a download kills the server. | [Ignore `SIGPIPE`](#survive-clients-that-disconnect-early). |
| Paths not sanitized | [src/main.c:71](src/main.c#L71) | Any file the process can read is reachable with `..` or a leading `//`. | [Block unsafe paths](#block-unsafe-paths). |
| `HEAD` responses include the body | [src/main.c:71](src/main.c#L71) | Breaks the HTTP rules for `HEAD`. | [Skip the body](#answer-head-without-a-body). |
| No `SO_REUSEADDR` | [src/server.c:11](src/server.c#L11) | A quick restart fails with `Address already in use`. | [Set the option](#reuse-the-port-after-a-restart). |
| `socket()` checked against `0` | [src/server.c:14](src/server.c#L14) | Socket creation errors aren't detected. | Compare with `< 0`. |
| No phrases for 401 and 403 | [src/http_response.c:5](src/http_response.c#L5) | The status line would say `Unknown`. | Add the two `case`s. |
| `NULL` path passed to `send_file()` | [src/main.c:71](src/main.c#L71) | Only works because `fopen(NULL)` fails. | Check `request.path` before the call. |
| Unchecked skip in `parse_path()` | [src/http_request.c:117](src/http_request.c#L117) | Reads past the end of the string for a line like `"GET "`. | Check that the path starts with `/`. |
| Directories are served as files | [src/file_handler.c:33](src/file_handler.c#L33) | 200 with an invalid `Content-Length`. | Use `stat()` and check `S_ISREG()`. |
| `fopen()` not checked when streaming | [src/file_handler.c:80](src/file_handler.c#L80) | Crash if the file disappears between the opens. | Open the file once and pass the `FILE *`. |
| Header changes not tracked | [Makefile](Makefile) | Edits to `.h` files need a manual rebuild. | [Add `-MMD -MP`](#track-header-dependencies-in-the-makefile). |
| `make clean` removes the sample site | [Makefile:44](Makefile#L44) | `bin/*.html`, the CSS and the image are deleted. | Keep the site in its own directory, or delete only `bin/server`. |
