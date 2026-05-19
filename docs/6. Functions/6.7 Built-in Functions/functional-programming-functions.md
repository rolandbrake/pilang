# Functional Programming Functions

Functional helpers take functions as arguments. They are useful for transforming,
filtering, searching, and reducing collections.

## `map`

`map(collection, function)` calls `function` for each item and returns a list of
the results.

```pilang
let values = [1, 2, 3]
let doubled = map(values, n -> n * 2)

println(doubled) // [2, 4, 6]
```

## `filter`

`filter(collection, predicate)` keeps items for which `predicate(item)` is truthy.

```pilang
let values = [1, 2, 3, 4]
let evens = filter(values, n -> n % 2 == 0)

println(evens) // [2, 4]
```

## `reduce`

`reduce(collection, function, initial)` combines values into one result.

```pilang
let values = [1, 2, 3, 4]

let total = reduce(values, (acc, n) -> acc + n, 0)
println(total) // 10
```

## `find`

`find(collection, predicate)` returns the first item that satisfies the predicate.

```pilang
let names = ["Ada", "Grace", "Linus"]
let match = find(names, name -> len(name) > 4)

println(match) // Grace
```

## Function Values

Any Pilang function value can be passed to these helpers: named functions,
anonymous functions, or arrow functions.

```pilang
fun positive(n) {
    return n > 0
}

println(filter([-2, 0, 5], positive)) // [5]
```
