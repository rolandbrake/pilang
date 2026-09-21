# `net.socket`

`net.socket` provides synchronous TCP client sockets. It is available in native
Pilang builds and is not available in WebAssembly builds.

```swift
import net.socket

let client = socket.Socket(socket.AF_INET, socket.SOCK_STREAM)
socket.connect(client, "example.com", 80, {"timeout": 5000})
socket.send_all(client, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")

let response = socket.receive(client, 4096)
println(response.data)
socket.close(client)
```

## `Socket(family = AF_INET, type = SOCK_STREAM, protocol = IPPROTO_TCP)`

Creates an unconnected TCP socket. This version supports `SOCK_STREAM` only.

## `connect(socket, host, port, options = {})`

Connects a socket. `options.timeout` is an optional timeout in milliseconds
for connection, sending, and receiving.

For a one-line connection, `connect(host, port, options = {})` is retained as
a convenience helper and returns a connected socket.

## Constants

- `AF_INET`: IPv4 address family.
- `AF_INET6`: IPv6 address family.
- `SOCK_STREAM`: TCP stream socket type.
- `IPPROTO_TCP`: TCP protocol.

## Local echo server

Use this with a TCP echo server running on port `9000`:

```swift
import net.socket

let client = socket.Socket(socket.AF_INET, socket.SOCK_STREAM)
socket.connect(client, "127.0.0.1", 9000)
socket.send_all(client, "hello from Pilang\n")

let reply = socket.receive(client, 1024)
println(reply.data)
socket.close(client)
```

## Reading a complete response

`receive` returns as soon as data is available; a protocol response may need
multiple reads. Keep reading until `eof` is true:

```swift
import net.socket

let client = socket.Socket(socket.AF_INET, socket.SOCK_STREAM)
socket.connect(client, "example.com", 80)
socket.send_all(client, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")

let response = socket.receive(client)
while not response.eof {
    println(response.data)
    response = socket.receive(client)
}
println(response.data)
socket.close(client)
```

## `send_all(socket, data)`

Writes the complete string and returns the number of bytes sent. This first
version is intended for text protocols; arbitrary binary payloads will be
supported when Pilang gains a byte-buffer value.

## `receive(socket, max_bytes = 4096)`

Reads up to `max_bytes` and returns a map with `data` and `eof`. `eof` is true
when the remote peer has closed the connection.

## `close(socket)`

Closes a socket. Unclosed sockets are also closed when garbage collected.
