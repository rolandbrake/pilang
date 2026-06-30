# 11. Functional Programming

Functional programming in Pilang is built around first-class functions, closures, and collection helpers. A function can be stored in a variable, passed as an argument, returned from another function, and combined with other functions.

Pilang supports a practical functional style without forcing every program to be written that way. You can mix functional helpers with normal loops, classes, maps, modules, and mutable objects.

## Core ideas

- Functions are values.
- Anonymous functions and arrow functions are useful for small callbacks.
- Closures can remember variables from their surrounding scope.
- `map`, `filter`, `reduce`, and `find` are available as global helpers.
- The `=>` pipeline operator passes a value into the next function call.
- The `func` module provides composition, currying, partial application, memoization, throttling, debouncing, and iterator-style helpers.

## Common pattern

```swift
numbers = [1, 2, 3, 4, 5]

evens = filter(numbers, fun (n) {
    return n % 2 == 0
})

squares = map(evens, n -> n * n)

total = reduce(squares, fun (acc, n) {
    return acc + n
}, 0)

print(total) // 20
```

The same transformation can be written as a pipeline. Each stage receives the
previous result as its first argument:

```swift
total = [1, 2, 3, 4, 5] =>
    filter(n -> n % 2 == 0) =>
    map(n -> n * n) =>
    reduce((acc, n) -> acc + n, 0)

print(total) // 20
```

The original `numbers` list is still available. The helpers return new result values, although the callback can still mutate objects if you explicitly write code that does so.

## Sections

- [11.1 map](11.1-map.md)
- [11.2 filter](11.2-filter.md)
- [11.3 reduce](11.3-reduce.md)
- [11.4 compose](11.4-compose.md)
- [11.5 Iterators](11.5-iterators.md)
