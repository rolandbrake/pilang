# sys Module

The `sys` module exposes process, runtime, environment, and exit helpers.

```pilang
import sys
```

## Constants

- `sys.EXIT_SUCCESS`: success exit code
- `sys.EXIT_FAILURE`: failure exit code

```pilang
import sys.{EXIT_SUCCESS}
println(EXIT_SUCCESS)
```

## `sys.argv()`

Returns command-line arguments as a list of strings.

```pilang
println(sys.argv())
```

## `sys.exit(code = 0)`

Exits the process with the given numeric status code.

```pilang
sys.exit(sys.EXIT_SUCCESS)
```

## `sys.platform()`

Returns a string describing the current platform.

```pilang
println(sys.platform()) // Windows, Linux, macOS, ...
```

## `sys.version()`

Returns the Pilang runtime version string.

```pilang
println(sys.version())
```

## `sys.path()`

Returns the current working directory.

```pilang
println(sys.path())
```

## `sys.env(name)`

Reads an environment variable. Returns the value as a string, or `false` if the
variable is not set.

```pilang
println(sys.env("PATH"))
```

## `sys.gc()`

Runs garbage collection and returns `true`.

```pilang
println(sys.gc())
```

## `sys.mem()`

Returns memory usage information when supported by the platform, otherwise `0`.

```pilang
println(sys.mem())
```

## `sys.pid()`

Returns the current process id.

```pilang
println(sys.pid())
```
