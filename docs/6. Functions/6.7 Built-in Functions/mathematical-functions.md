# Mathematical Functions

Global math functions cover common numeric operations and random numbers.

## Numeric Helpers

- `abs(value)`: absolute value
- `min(a, b, ...)`: smallest value
- `max(a, b, ...)`: largest value
- `pow(base, exponent)`: exponentiation
- `round(value)`: rounded number

```swift
println(abs(-10))       // 10
println(min(4, 2, 8))   // 2
println(max(4, 2, 8))   // 8
println(pow(2, 5))      // 32
println(round(3.6))     // 4
```

## Random Numbers

- `seed(value)`: sets the random seed
- `rand()`: returns a random float in `[0.0, 1.0)`
- `rand(max)`: returns a random integer from `0` through `max`
- `rand(min, max)`: returns a random integer from `min` through `max`
- `rand_n(size)`: returns a list containing `size` random floats

```swift
seed(123)

println(rand())
println(rand_n(10))
```

## Constants

- `INF`: infinity
- `NAN`: not-a-number

```swift
println(INF)
println(NAN)
```

More specialized math functions, such as trigonometry and square roots, are
provided by the `math` module.
