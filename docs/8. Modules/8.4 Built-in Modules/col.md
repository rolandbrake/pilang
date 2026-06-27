# col Module

The `col` module provides collection utilities for lists, strings, tuples, maps,
sets, and other iterable values.

```swift
import col
```

## `col.peek(collection)`

Returns the last item of a list or the last character of a string without
removing it.

```swift
println(col.peek([1, 2, 3])) // 3
println(col.peek("abc"))     // c
```

## `col.sort(collection)`

Sorts a numeric or string list in place and returns `nil`.

```swift
let values = [3, 1, 2]
col.sort(values)
println(values) // [1, 2, 3]
```

## `col.unshift(collection, value, ...)`

Prepends one or more values to a list or string and returns the new length.

```swift
let values = [2, 3]
col.unshift(values, 1)
println(values) // [1, 2, 3]
```

```swift
let text = "lang"
col.unshift(text, "Pi")
println(text) // Pilang
```

## `col.append(collection, value, ...)`

Appends one or more values to a list or string and returns the new length.

```swift
let values = [1]
col.append(values, 2, 3)
println(values) // [1, 2, 3]
```

```swift
let text = "Pi"
col.append(text, "lang")
println(text) // Pilang
```

## `col.contains(collection, value)`

Returns whether a collection contains a value. For maps, it checks for a key. For
strings, the value must be a string substring.

```swift
println(col.contains([1, 2, 3], 2))      // true
println(col.contains("Pilang", "lang")) // true
println(col.contains({"x": 1}, "x"))    // true
```

## `col.indexOf(collection, value)`

Returns the first index of a value in a list, tuple, or string. Returns `-1` when
the value is not found.

```swift
println(col.indexOf(["a", "b", "a"], "b")) // 1
println(col.indexOf("banana", "na"))       // 2
```

## `col.reverse(collection)`

Returns a reversed copy of a list or string.

```swift
println(col.reverse([1, 2, 3])) // [3, 2, 1]
println(col.reverse("abc"))     // cba
```

## `col.shuffle(list)`

Shuffles a list in place and returns the same list.

```swift
let values = [1, 2, 3, 4]
col.shuffle(values)
println(values)
```

## `col.copy(collection)`

Copies a list, string, or set. List copies are shallow: contained objects are not
deep-cloned.

```swift
let original = [1, 2]
let cloned = col.copy(original)
col.append(cloned, 3)

println(original) // [1, 2]
println(cloned)   // [1, 2, 3]
```

## `col.zip(a, b, ...)`

Combines lists or strings by index. The result length is the shortest input
length.

```swift
println(col.zip([1, 2], ["a", "b"])) // [[1, "a"], [2, "b"]]
println(col.zip("ab", [1, 2]))       // [["a", 1], ["b", 2]]
```

## `col.join(collection, separator = "")`

Converts each item in a list or tuple to text and joins the items with an
optional separator. Passing a string returns the string unchanged.

```swift
println(col.join(["pi", "lang"], "-")) // pi-lang
println(col.join([1, 2, 3], ", "))     // 1, 2, 3
```

## `col.is_iterable(value)`

Returns whether a value is iterable.

```swift
println(col.is_iterable([1, 2])) // true
println(col.is_iterable(10))     // false
```

## `col.add(set, value, ...)`

Adds values to a set and returns it. If an added value is iterable, its contents
are added.

```swift
let names = set(["Ada"])
col.add(names, "Grace")
println(names)
```

## `col.clear(collection)`

Removes all items from a mutable collection and returns it.

```swift
let values = [1, 2, 3]
col.clear(values)
println(values) // []
```
