# os Module

The `os` module provides operating-system helpers. Availability can vary by
platform; browser/WASM builds may not support these functions.

```pilang
import os:o
```

## Constants

- `os.SIGINT`
- `os.SIGTERM`
- `os.SIGABRT`

```pilang
println(o.SIGTERM)
```

## `os.run(command)`

Runs a shell command and waits for it to finish. Returns a map with `stdout`,
`stderr`, and `code`.

```pilang
let result = o.run("echo hello")
println(result.stdout)
println(result.code)
```

## `os.spawn(command)`

Starts a command without waiting for completion. Returns the process id when
supported.

```pilang
let pid = o.spawn("echo hello")
println(pid)
```

## `os.which(command)`

Finds an executable on the system path. Returns a path string or `nil`/falsey
value when not found.

```pilang
println(o.which("git"))
```

## `os.signal(...)`

Configures a signal action. The action string is `"ignore"` or `"default"`.

```pilang
println(o.signal(o.SIGINT, "ignore"))
println(o.signal(o.SIGINT, "default"))
```

## `os.kill(pid, signal = SIGTERM)`

Terminates a process by id where supported.

```pilang
// o.kill(pid, o.SIGTERM)
```

## `os.hostname()`

Returns the host name.

```pilang
println(o.hostname())
```

## `os.cpus()`

Returns CPU information for the host.

```pilang
println(o.cpus())
```

## `os.ram()`

Returns RAM information for the host.

```pilang
println(o.ram())
```

## `os.user()`

Returns information about the current user.

```pilang
println(o.user())
```
