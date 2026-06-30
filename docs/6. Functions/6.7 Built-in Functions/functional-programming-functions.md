# Functional Programming Functions

Functional helpers take functions as arguments. They are useful for _transforming,
filtering, searching, and reducing collections.

## `map`

`map(list, function)` calls `function` for each item and returns a list of the
results.

```swift
let values = [1, 2, 3]
let doubled = map(values, n -> n * 2)

println(doubled) // [2, 4, 6]
```

## `filter`

`filter(list, predicate)` keeps items for which `predicate(item)` is truthy.

```swift
let values = [1, 2, 3, 4]
let evens = filter(values, n -> n % 2 == 0)

println(evens) // [2, 4]
```

## `reduce`

`reduce(list, function, initial = first_item)` combines values into one result.
When no initial value is supplied, the first list item is used as the initial
accumulator.

```swift
let values = [1, 2, 3, 4]

let total = reduce(values, (acc, n) -> acc + n, 0)
println(total) // 10
```

## `find`

`find(collection, predicate)` returns the index of the first item that satisfies
the predicate. It works with lists and strings and returns `-1` when no item
matches.

```swift
let names = ["Ada", "Grace", "Linus"]
let match = find(names, name -> len(name) > 4)

println(match) // 1
```

## `func` Module Helpers

The `func` module exports higher-order helpers for composing, wrapping, and
calling functions.

```swift
import func:f
```

- `f.compose(fun, ...)`: returns a right-to-left composition, so `compose(f, g)(x)` calls `f(g(x))`
- `f.pipe(fun, ...)`: returns a left-to-right composition, so `pipe(f, g)(x)` calls `g(f(x))`
- `f.juxt(fun, ...)`: returns a function that calls every function with the same arguments and returns a list
- `f.curry(fun, arity = inferred)`: returns a curried wrapper
- `f.partial(fun, arg, ...)`: returns a wrapper with leading arguments pre-bound
- `f.spread(fun)`: returns a wrapper that expects one list and spreads it into positional arguments
- `f.unspread(fun)`: returns a wrapper that packs positional arguments into one list
- `f.memoize(fun, key_fn = nil)`: returns a wrapper that caches results
- `f.once(fun)`: returns a wrapper that calls `fun` once and reuses the first result
- `f.throttle(ms, fun)`: returns a wrapper that calls `fun` at most once per interval
- `f.debounce(ms, fun)`: returns a wrapper that suppresses repeated calls during the interval
- `f.thunk(fun, arg, ...)`: returns a zero-argument wrapper that calls `fun` later
- `f.iterate(seed, fun)`: returns a zero-argument generator-like function
- `f.apply(fun, args)`: calls `fun` with positional arguments from a list
- `f.noop()`: returns `nil`

```swift
import func:f

let inc = x -> x + 1
let double = x -> x * 2

let _transform = f.pipe(inc, double)
println(_transform(3)) // 8

fun add(a, b) {
    return a + b
}

println(f.apply(add, [2, 5])) // 7
```

## Function Values

Any Pilang function value can be passed to these helpers: named functions,
anonymous functions, or arrow functions.

```swift
fun positive(n) {
    return n > 0
}

println(filter([-2, 0, 5], positive)) // [5]
```
