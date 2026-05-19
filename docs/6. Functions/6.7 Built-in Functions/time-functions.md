# Time Functions

Pilang provides two global time helpers.

## `time`

`time()` returns the current time value supplied by the runtime.

```pilang
let started = time()

// work

let elapsed = time() - started
println(elapsed)
```

## `sleep`

`sleep(seconds)` pauses execution for the requested duration.

```pilang
println("waiting")
sleep(1)
println("done")
```

More detailed clock, formatting, parsing, interval, and timer helpers are
exported by the `time` module.
