# Time Functions

Pilang provides two global time helpers.

## `time`

`time()` returns the current runtime time in milliseconds.

```swift
let started = time()

// work

let elapsed = time() - started
println(elapsed)
```

## `sleep`

`sleep(ms)` pauses execution for the requested number of milliseconds.

```swift
println("waiting")
sleep(1000)
println("done")
```

More detailed clock, formatting, parsing, interval, and timer helpers are
exported by the `time` module.
