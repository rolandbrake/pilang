# Collection Functions

Collection functions work with lists, tuples, sets, strings, maps, and other
iterable values.

## Stack-Like List Helpers

- `push(collection, value, ...)`: appends values to a list, or single-character strings to a string, and returns the new length
- `pop(collection)`: removes and returns the last item from a list or the last character from a string
- `peek(collection)`: returns the last item or character without removing it
- `empty(collection)`: returns whether the collection is empty

```pilang
let items = [1, 2]

push(items, 3)
println(peek(items)) // 3
println(pop(items))  // 3
println(items)       // [1, 2]
```

## Inserting and Removing

- `insert(collection, index, value)`: inserts a value into a list or string and returns the collection
- `remove(collection, index_or_key)`: removes and returns a list item or string character; for maps, removes a key and returns the map
- `slice(start, end, step = 1)`: creates a slice descriptor for bracket slicing

```pilang
let values = [10, 30]
insert(values, 1, 20)
println(values) // [10, 20, 30]

println(values[slice(0, 2)]) // [10, 20]
```

## Size and Search

- `len(value)`: returns the length or size of a value
- `contains(collection, value)`: returns whether `value` is present
- `index(collection, value)`: returns the index of `value`, or `-1` when it is not found
- `count(collection, value)`: counts matching values

```pilang
let letters = ["a", "b", "a"]

println(len(letters))           // 3
println(contains(letters, "b")) // true
println(count(letters, "a"))    // 2
```

## Combining and Repeating

- `concat(a, b)`: combines compatible collections
- `repeat(value, amount)`: repeats a string, list, or tuple
- `copy(value)`: returns a copied value
- `join(collection, separator = "")`: joins a list or tuple into a string

```pilang
println(concat([1, 2], [3])) // [1, 2, 3]
println(repeat("ha", 3))    // hahaha
println(join(["pi", "lang"], "-")) // pi-lang
```

## Constructors

- `range(end)`, `range(start, end)`, `range(start, end, step)`: creates a range
- `tuple()`, `tuple(value)`, `tuple(a, b, ...)`: creates a tuple or converts a list, tuple, or string to a tuple
- `set(value = nil)`: creates a set from an iterable value

```pilang
let unique = set([1, 1, 2, 3])
println(unique)
```

## Set Functions

- `union(a, b)`: returns values from either set
- `intersection(a, b)`: returns values shared by both sets
- `difference(a, b)`: returns values in `a` but not in `b`
- `symmetric_diff(a, b)`: returns values that appear in exactly one set
- `issubset(a, b)`: returns whether `a` is a subset of `b`
- `issuperset(a, b)`: returns whether `a` is a superset of `b`
- `isdisjoint(a, b)`: returns whether the sets share no values

```pilang
let a = set([1, 2, 3])
let b = set([3, 4])

println(union(a, b))
println(intersection(a, b))
println(difference(a, b))
```

## `col` Module Helpers

The `col` module exports additional collection helpers:

- `col.sort(list)`: sorts a numeric or string list in place and returns `nil`
- `col.unshift(collection, value, ...)`: prepends values to a list or string and returns the new length
- `col.append(collection, value, ...)`: appends values to a list or string and returns the new length
- `col.reverse(collection)`: returns a reversed copy of a list or string
- `col.shuffle(list)`: shuffles a list in place and returns it
- `col.copy(collection)`: copies a list, string, or set
- `col.zip(a, b, ...)`: returns a list of grouped items from lists or strings
- `col.join(collection, separator = "")`: joins a list or tuple into a string
- `col.is_iterable(value)`: returns whether a value is iterable
- `col.add(set, value, ...)`: adds values or iterable contents to a set and returns it
- `col.clear(collection)`: clears a list, string, or set and returns it

```pilang
import col

let values = [3, 1, 2]
col.sort(values)
println(values) // [1, 2, 3]

println(col.zip(["x", "y"], [1, 2])) // [["x", 1], ["y", 2]]
println(col.join(["x", "y"], ""))    // xy
```
