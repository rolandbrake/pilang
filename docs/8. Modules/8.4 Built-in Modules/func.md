# func Module

The `func` module provides higher-order helpers for composing and wrapping
functions.

```swift
import func:f
```

## `func.compose(fn, ...)`

Returns a function that calls the provided functions from right to left.

```swift
let inc = x -> x + 1
let double = x -> x * 2
let composed = f.compose(double, inc)

println(composed(3)) // 8
```

## `func.pipe(fn, ...)`

Returns a function that calls the provided functions from left to right.

```swift
let piped = f.pipe(inc, double)
println(piped(3)) // 8
```

## `func.juxt(fn, ...)`

Returns a function that calls every provided function with the same arguments and
returns a list of results.

```swift
let both = f.juxt(x -> x + 1, x -> x * 2)
println(both(5)) // [6, 10]
```

## `func.curry(fn, arity = inferred)`

Returns a curried wrapper. Arguments can be supplied gradually until the wrapped
function has enough arguments.

```swift
fun add(a, b) {
    return a + b
}

let curried = f.curry(add)
println(curried(2)(3)) // 5
```

## `func.partial(fn, arg, ...)`

Returns a wrapper with leading arguments pre-bound.

```swift
let plus5 = f.partial(add, 5)
println(plus5(7)) // 12
```

## `func.spread(fn)`

Returns a wrapper that expects one list argument and spreads it into positional
arguments.

```swift
let spread_add = f.spread(add)
println(spread_add([2, 4])) // 6
```

## `func.unspread(fn)`

Returns a wrapper that packs positional arguments into a single list.

```swift
fun sum_pair(pair) {
    return pair[0] + pair[1]
}

let unspread_sum = f.unspread(sum_pair)
println(unspread_sum(2, 4)) // 6
```

## `func.memoize(fn)`

Returns a wrapper that caches results by argument list.

```swift
let calls = 0

fun square(x) {
    calls++
    return x * x
}

let memo = f.memoize(square)
println(memo(6))
println(memo(6))
println(calls) // 1
```

## `func.once(fn)`

Returns a wrapper that calls `fn` once and then returns the first result for all
later calls.

```swift
let count = 0
let init = f.once(() -> {
    count++
    return count
})

println(init()) // 1
println(init()) // 1
```

## `func.throttle(ms, fn)`

Returns a wrapper that only calls `fn` when at least `ms` milliseconds have passed
since the last call. Calls made too soon return the previous result.

```swift
let limited = f.throttle(1000, () -> time())
println(limited())
println(limited())
```

## `func.debounce(ms, fn)`

Returns a wrapper that delays useful execution until the debounce window has
passed. This is useful for event-style flows.

```swift
let debounced = f.debounce(250, () -> "saved")
println(debounced())
```

## `func.thunk(fn, arg, ...)`

Returns a zero-argument function that calls `fn` later with captured arguments.

```swift
let later = f.thunk(add, 2, 3)
println(later()) // 5
```

## `func.iterate(seed, fn)`

Returns a generator-like function. The first call returns `seed`; later calls
apply `fn` to the previous value.

```swift
let powers = f.iterate(1, x -> x * 2)
println(powers()) // 1
println(powers()) // 2
println(powers()) // 4
```

## `func.apply(fn, args)`

Calls `fn` with a list of positional arguments.

```swift
println(f.apply(add, [3, 9])) // 12
```

## `func.noop()`

Does nothing and returns `nil`.

```swift
println(f.noop()) // nil
```
