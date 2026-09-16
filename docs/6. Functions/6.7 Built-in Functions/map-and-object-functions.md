# Map and Object Functions

Map and object helpers expose keys, values, copying, and prototype-style object
behavior.

## Global Helpers

- `keys(value)`: returns the keys from a map or object-like value
- `values(value)`: returns the values from a map or object-like value
- `clone(value)`: returns a copy of the value
- `id(value)`: returns a heap object's runtime identity as a decimal string

`id()` accepts exactly one heap object, including strings, lists, maps, functions,
classes, and instances. Numbers, booleans, and `nil` have no heap-object identity
and cause a runtime error. The string preserves the full 64-bit identifier without
numeric rounding. It is an identifier, not a memory address or a persistent ID
across program runs. Aliases share an ID; distinct objects have different IDs.
Mutation and garbage collection do not change a surviving object's ID.

```swift
let a = [1, 2]
let b = a
let c = [1, 2]
println(id(a) == id(b)) // true
println(id(a) == id(c)) // false
```

```swift
let user = {
    "name": "Ada",
    "active": true,
}

println(keys(user))
println(values(user))

let copy = clone(user)
```

## Object Prototype Methods

Objects also provide built-in methods through the base object behavior:

- `format()`: returns a formatted string representation
- `hash()`: returns a hash value
- `clone()`: returns a copy
- `extends(parent)`: sets a prototype parent
- `equals(other)`: equality hook
- `ident(other)`: identity hook
- `compare(other)`: comparison hook
- `type()`: returns the object type
- `name()`: returns the object name
- `setName(name)`: sets the object name
- `lock()`: prevents further mutation where supported
- `bracketAccess()`: controls bracket access behavior
- `get(key)`: reads a property
- `set(key, value)`: writes a property
- `has(key)`: checks whether a property exists
- `delete(key)`: removes a property
- `iterator()`: creates an iterator
- `next()`: advances an iterator

```swift
let point = {
    "x": 2,
    "y": 4,
}

println(point.get("x"))
point.set("x", 3)
println(point.has("x"))
```

Operator hooks such as `equals`, `compare`, `getItem`, `setItem`, and `compute`
are covered in [Operator Overloading](../../4.%20Operators/4.6-operator-overloading.md).
