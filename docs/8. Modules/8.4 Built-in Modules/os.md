# os Module

The `os` module provides operating-system helpers. Availability can vary by
platform; browser/WASM builds may not support these functions.

```swift
import os:o
```

## Constants

- `os.SIGINT`
- `os.SIGTERM`
- `os.SIGABRT`

```swift
println(o.SIGTERM)
```

## `os.run(command)`

Runs a shell command and waits for it to finish. Returns a map with `stdout`,
`stderr`, and `code`.

```swift
let result = o.run("echo hello")
println(result.stdout)
println(result.code)
```

## `os.spawn(command)`

Starts a command without waiting for completion. Returns the process id when
supported.

```swift
let pid = o.spawn("echo hello")
println(pid)
```

## `os.which(command)`

Finds an executable on the system path. Returns a path string or `nil`/falsey
value when not found.

```swift
println(o.which("git"))
```

## `os.signal(...)`

Configures a signal action. The action string is `"ignore"` or `"default"`.

```swift
println(o.signal(o.SIGINT, "ignore"))
println(o.signal(o.SIGINT, "default"))
```

## `os.kill(pid, signal = SIGTERM)`

Terminates a process by id where supported.

```swift
// o.kill(pid, o.SIGTERM)
```

## `os.hostname()`

Returns the host name.

```swift
println(o.hostname())
```

## `os.cpus()`

Returns CPU information for the host.

```swift
println(o.cpus())
```

## `os.ram()`

Returns RAM information for the host.

```swift
println(o.ram())
```

## `os.user()`

Returns information about the current user.

```swift
println(o.user())
```
