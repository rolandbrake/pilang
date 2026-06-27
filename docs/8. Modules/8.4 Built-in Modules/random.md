# random Module

The `random` module provides random number helpers backed by Pilang's shared PRNG.

```swift
import random:r

r.seed(123)
println(r.uniform())
println(r.uniform(10, 20))
println(r.randint(1, 6))
println(r.choice(["red", "green", "blue"]))
println(r.shuffle([1, 2, 3]))
```

## Functions

- `random.seed(value)`: seeds the random number generator.
- `random.rand()`: returns a float in `[0, 1)`.
- `random.uniform()`: returns a float in `[0, 1)`.
- `random.uniform(min, max)`: returns a float from `min` up to `max`.
- `random.randint(max)`: returns an integer from `0` through `max`.
- `random.randint(min, max)`: returns an integer from `min` through `max`.
- `random.randi(...)`: alias for `random.randint(...)`.
- `random.normal(mean = 0, stddev = 1)`: returns a normally distributed value.
- `random.choice(list)`: returns one item from a non-empty list.
- `random.shuffle(list)`: returns a shuffled copy of a list.
