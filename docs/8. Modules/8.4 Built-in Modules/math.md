# math Module

The `math` module provides numeric constants and mathematical helpers.

```pilang
import math
```

Many numeric functions accept either a single number or a list of numbers. When a
list is passed, the result is a new list.

## Constants

- `math.PI`: pi
- `math.E`: Euler's number
- `math.TAU`: tau, equal to `2 * PI`
- `math.PHI`: the golden ratio

```pilang
println(math.PI)
println(math.TAU)
```

## `math.floor(value)`

Rounds a number down, or every number in a list down.

```pilang
println(math.floor(3.9))       // 3
println(math.floor([1.2, 2.8])) // [1, 2]
```

## `math.ceil(value)`

Rounds a number up, or every number in a list up.

```pilang
println(math.ceil(3.1))       // 4
println(math.ceil([1.2, 2.8])) // [2, 3]
```

## `math.sqrt(value)`

Returns the square root of a number, or every number in a list.

```pilang
println(math.sqrt(25))      // 5
println(math.sqrt([4, 9]))  // [2, 3]
```

## `math.sin(value)`

Returns the sine of a value in radians, or every value in a list.

```pilang
println(math.sin(math.PI / 2)) // 1
```

## `math.cos(value)`

Returns the cosine of a value in radians, or every value in a list.

```pilang
println(math.cos(0)) // 1
```

## `math.tan(value)`

Returns the tangent of a value in radians, or every value in a list.

```pilang
println(math.tan(math.PI / 4))
```

## `math.asin(value)`

Returns inverse sine in radians. Inputs must be between `-1` and `1`.

```pilang
println(math.asin(1)) // PI / 2
```

## `math.acos(value)`

Returns inverse cosine in radians. Inputs must be between `-1` and `1`.

```pilang
println(math.acos(1)) // 0
```

## `math.atan(value)`

Returns inverse tangent in radians.

```pilang
println(math.atan(1))
```

## `math.deg(radians)`

Converts radians to degrees.

```pilang
println(math.deg(math.PI)) // 180
```

## `math.rad(degrees)`

Converts degrees to radians.

```pilang
println(math.rad(180)) // PI
```

## `math.sum(values)`

Returns the sum of a numeric list.

```pilang
println(math.sum([1, 2, 3, 4])) // 10
```

## `math.exp(value)`

Returns `E` raised to `value`, or maps a list.

```pilang
println(math.exp(1)) // E
```

## `math.log2(value)`

Returns the base-2 logarithm. Values must be positive.

```pilang
println(math.log2(8)) // 3
```

## `math.log10(value)`

Returns the base-10 logarithm. Values must be positive.

```pilang
println(math.log10(1000)) // 3
```

## `math.logE(value)`

Returns the natural logarithm.

```pilang
println(math.logE(math.E)) // 1
```

## `math.linspace(start, end, count)`

Returns `count` evenly spaced numbers from `start` to `end`, including both
endpoints.

```pilang
println(math.linspace(0, 1, 5)) // [0, 0.25, 0.5, 0.75, 1]
```

## `math.arange(start, end, step = 1)`

Returns numbers from `start` up to but not including `end`.

```pilang
println(math.arange(0, 5, 2)) // [0, 2, 4]
```
