# stats Module

The `stats` module provides statistics for numeric lists.

```pilang
import stats
```

All functions expect a non-empty list of numbers.

## `stats.mean(values)`

Returns the arithmetic mean.

```pilang
println(stats.mean([2, 4, 6])) // 4
```

## `stats.avg(values)`

Alias for `stats.mean`.

```pilang
println(stats.avg([2, 4, 6])) // 4
```

## `stats.var(values)`

Returns population variance.

```pilang
println(stats.var([2, 4, 6])) // 2.666...
```

## `stats.dev(values)`

Returns standard deviation.

```pilang
println(stats.dev([2, 4, 6]))
```

## `stats.median(values)`

Returns the middle value after sorting. For an even-length list, returns the
average of the two middle values.

```pilang
println(stats.median([3, 1, 2]))    // 2
println(stats.median([1, 2, 3, 4])) // 2.5
```

## `stats.mode(values)`

Returns the most frequent numeric value. If there is a tie, the first most
frequent value after sorting is returned.

```pilang
println(stats.mode([1, 2, 2, 3])) // 2
```
