# sys Module

The `sys` module exposes process, runtime, environment, and exit helpers.

```swift
import sys
```

## Constants

- `sys.EXIT_SUCCESS`: success exit code
- `sys.EXIT_FAILURE`: failure exit code

```swift
import sys.{EXIT_SUCCESS}
println(EXIT_SUCCESS)
```

## `sys.argv()`

Returns command-line arguments as a list of strings.

```swift
println(sys.argv())
```

## `sys.exit(code = 0)`

Exits the process with the given numeric status code.

```swift
sys.exit(sys.EXIT_SUCCESS)
```

## `sys.platform()`

Returns a string describing the current platform.

```swift
println(sys.platform()) // Windows, Linux, macOS, ...
```

## `sys.version()`

Returns the Pilang runtime version string.

```swift
println(sys.version())
```

## `sys.path()`

Returns the current working directory.

```swift
println(sys.path())
```

## `sys.env(name)`

Reads an environment variable. Returns the value as a string, or `false` if the
variable is not set.

```swift
println(sys.env("PATH"))
```

## `sys.gc()`

Runs garbage collection and returns `true`.

```swift
println(sys.gc())
```

## `sys.mem()`

Returns memory usage information when supported by the platform, otherwise `0`.

```swift
println(sys.mem())
```

## `sys.pid()`

Returns the current process id.

```swift
println(sys.pid())
```
