# Mathematical Functions

Global math functions cover common numeric operations and random numbers.

## Numeric Helpers

- `abs(value)`: absolute value
- `min(a, b, ...)`: smallest value
- `max(a, b, ...)`: largest value
- `pow(base, exponent)`: exponentiation
- `round(value)`: rounded number

```pilang
println(abs(-10))       // 10
println(min(4, 2, 8))   // 2
println(max(4, 2, 8))   // 8
println(pow(2, 5))      // 32
println(round(3.6))     // 4
```

## Random Numbers

- `seed(value)`: sets the random seed
- `rand()`: returns a random number
- `rand_n(limit)`: returns a random number constrained by `limit`

```pilang
seed(123)

println(rand())
println(rand_n(10))
```

## Constants

- `INF`: infinity
- `NAN`: not-a-number

```pilang
println(INF)
println(NAN)
```

More specialized math functions, such as trigonometry and square roots, are
provided by the `math` module.
