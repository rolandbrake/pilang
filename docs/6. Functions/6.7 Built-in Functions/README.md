# 6.7 Built-in Functions

Built-in functions are callable values provided by Pilang. Some are available as
global functions, such as `println`, `len`, and `map`. Others are exported from
built-in modules, such as `math.sqrt`, `fs.read`, or `tensor.zeros`.

This section focuses on the function families. The module system itself is
covered in [Modules](../../8.%20Modules/README.md).

## Global Constants

- `INF`: positive infinity
- `NAN`: not-a-number

## Global Function Families

- [Collection Functions](collection-functions.md): `push`, `pop`, `len`, `range`, sets, copying, slicing, and collection helpers
- [Functional Programming Functions](functional-programming-functions.md): `map`, `filter`, `reduce`, and `find`
- [I/O Functions](io-functions.md): `print`, `println`, `printf`, `log`, and `input`
- [Map and Object Functions](map-and-object-functions.md): `keys`, `values`, `clone`, and object prototype helpers
- [Mathematical Functions](mathematical-functions.md): `abs`, `min`, `max`, `pow`, `round`, and random helpers
- [String Functions](string-functions.md): `char`, `ord`, `trim`, `upper`, and `lower`
- [System Functions](system-functions.md): `error`, `assert`, and `zen`
- [Time Functions](time-functions.md): `sleep` and `time`
- [Type Functions](type-functions.md): `type`, `is_*`, and `as_*` conversion helpers

## Module Function Families

- [Graphics Functions](graphics-functions.md): drawing and plotting functions exported by graphics modules
- [Tensor Functions](tensor-functions.md): tensor constructors, shape helpers, math operations, and statistics exported by the `tensor` module

Module-scoped functions must be imported before use unless the program has
already exposed them in the current scope.
