# Collection Functions

Collection functions work with lists, tuples, sets, strings, maps, and other
iterable values.

## Stack-Like List Helpers

- `push(list, value)`: adds `value` to the end of a list and returns the updated list
- `pop(list)`: removes and returns the last item
- `peek(list)`: returns the last item without removing it
- `empty(collection)`: returns whether the collection is empty

```pilang
let items = [1, 2]

push(items, 3)
println(peek(items)) // 3
println(pop(items))  // 3
println(items)       // [1, 2]
```

## Inserting and Removing

- `insert(collection, index, value)`: inserts a value at an index
- `remove(collection, value_or_index)`: removes an item, depending on the collection type
- `slice(collection, start, end)`: returns a portion of the collection

```pilang
let values = [10, 30]
insert(values, 1, 20)
println(values) // [10, 20, 30]
```

## Size and Search

- `len(value)`: returns the length or size of a value
- `contains(collection, value)`: returns whether `value` is present
- `index(collection, value)`: returns the index of `value`
- `count(collection, value)`: counts matching values

```pilang
let letters = ["a", "b", "a"]

println(len(letters))           // 3
println(contains(letters, "b")) // true
println(count(letters, "a"))    // 2
```

## Combining and Repeating

- `concat(a, b)`: combines compatible collections
- `repeat(value, amount)`: repeats a string, list, tuple, or compatible value
- `copy(value)`: returns a copied value

```pilang
println(concat([1, 2], [3])) // [1, 2, 3]
println(repeat("ha", 3))    // hahaha
```

## Constructors

- `range(start, end)`: creates a range
- `tuple(value)`: converts a compatible value to a tuple
- `set(value)`: creates a set from an iterable value

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
