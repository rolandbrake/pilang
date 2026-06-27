# time Module

The `time` module provides wall-clock values, monotonic clocks, parsing,
formatting, timers, and intervals.

```swift
import time:t
```

Time objects are maps with fields such as `year`, `month`, `day`, `hour`,
`minute`, `second`, `millisecond`, `weekday`, `yearday`, `utc`, `unix`, and
`millis`.

## `time.now()`

Returns the current local time object.

```swift
let now = t.now()
println(now.year)
println(now.month)
```

## `time.utc()`

Returns the current UTC time object.

```swift
let now = t.utc()
println(now.utc) // true
```

## `time.unix()`

Returns the current Unix timestamp in seconds.

```swift
println(t.unix())
```

## `time.millis()`

Returns the current wall-clock time in milliseconds.

```swift
let start = t.millis()
println(start)
```

## `time.clock()`

Returns a monotonic clock value in seconds, useful for measuring elapsed time.

```swift
let start = t.clock()
let elapsed = t.clock() - start
println(elapsed)
```

## `time.parse(text, format)`

Parses a time string using format tokens such as `%Y`, `%m`, `%d`, `%H`, `%M`,
`%S`, and `%f`.

```swift
let value = t.parse("2026-05-18 14:30:00", "%Y-%m-%d %H:%M:%S")
println(value.year)
```

## `time.of(year, month, day, hour = 0, minute = 0, second = 0)`

Creates a local time object from numeric parts.

```swift
let date = t.of(2026, 5, 18, 14, 30, 0)
println(date.day)
```

## `time.format(time_value, format)`

Formats a time object with `strftime`-style tokens. `%f` is replaced with
milliseconds.

```swift
let date = t.of(2026, 5, 18)
println(t.format(date, "%Y/%m/%d"))
```

## `time.iso(time_value)`

Formats a time object as an ISO-like string.

```swift
println(t.iso(t.utc()))
```

## `time.timer(ms, callback)`

Sleeps for `ms` milliseconds, calls the callback once, and returns a timer object.

```swift
t.timer(100, () -> println("done"))
```

## `time.interval(ms, callback)`

Repeats: sleep, call callback, and continue while the callback returns truthy.
Returns a timer object after the callback returns falsey.

```swift
let count = 0

t.interval(100, () -> {
    count++
    println(count)
    return count < 3
})
```
