# System Functions

System functions are global helpers for errors, assertions, and small runtime
utilities.

## `error`

`error(message)` raises a runtime error with the provided message.

```pilang
fun require_positive(n) {
    if n <= 0 {
        error("expected a positive number")
    }

    return n
}
```

## `assert`

`assert(condition, message = nil)` checks that a condition is truthy. If the
condition is falsey, execution stops with an assertion error.

```pilang
let items = [1, 2, 3]

assert(len(items) == 3, "items should contain three values")
```

Falsey values include `false`, `nil`, `0`, empty strings, empty lists, empty
maps, empty modules, zero-size tensors, and empty ranges.

## `zen`

`zen()` prints or returns Pilang's built-in zen text.

```pilang
zen()
```

For process, platform, environment, and memory helpers, use the `sys` and `os`
modules.
