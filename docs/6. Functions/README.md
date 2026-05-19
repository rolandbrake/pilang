# 6. Functions

Functions are first-class values in Pilang. They can be declared with a name,
stored in variables, passed to other functions, returned from functions, and
captured by closures.

Pilang supports three main ways to create functions:

- named declarations with `fun name(...) { ... }`
- anonymous functions with `fun(...) { ... }`
- arrow functions with `x -> expr` or `(x, y) -> expr`

Every function call creates its own local scope. Parameters are local to that
call, and a function that reaches the end without returning a value returns
`nil`.

## Sections

- [6.1 Function Declaration](6.1-function-declaration.md)
- [6.2 Anonymous Functions](6.2-anonymous-functions.md)
- [6.3 Arrow Functions](6.3-arrow-functions.md)
- [6.4 Arguments](6.4-arguments.md)
- [6.5 Closures](6.5-closures.md)
- [6.6 Recursion](6.6-recursion.md)
- [6.7 Built-in Functions](6.7%20Built-in%20Functions/README.md)
