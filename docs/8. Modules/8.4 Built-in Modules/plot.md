# plot Module

The `plot` module provides charting and plotting helpers.

```pilang
import plot
```

## `plot.chart(kind, data, ...)`

Creates a generic chart.

```pilang
plot.chart("line", [1, 2, 3])
plot.show()
```

## `plot.func(function, start, end, ...)`

Plots a function over a range.

```pilang
plot.func(x -> x * x, -10, 10)
plot.show()
```

## `plot.scatter(x, y, ...)`

Creates a scatter plot.

```pilang
plot.scatter([1, 2, 3], [2, 4, 9])
plot.show()
```

## `plot.bar(labels, values, ...)`

Creates a bar chart.

```pilang
plot.bar(["A", "B"], [10, 20])
plot.show()
```

## `plot.line(x, y, ...)`

Creates a line plot.

```pilang
plot.line([1, 2, 3], [2, 4, 8])
plot.show()
```

## `plot.hist(values, ...)`

Creates a histogram.

```pilang
plot.hist([1, 1, 2, 3, 3, 3])
plot.show()
```

## `plot.step(x, y, ...)`

Creates a step plot.

```pilang
plot.step([1, 2, 3], [10, 20, 15])
plot.show()
```

## `plot.heatmap(values, ...)`

Creates a heatmap from matrix-like data.

```pilang
plot.heatmap([[1, 2], [3, 4]])
plot.show()
```

## `plot.show()`

Displays the current plot.

```pilang
plot.show()
```

## `plot.title(text)`

Sets the chart title.

```pilang
plot.title("Growth")
```

## `plot.xlabel(text)`

Sets the x-axis label.

```pilang
plot.xlabel("time")
```

## `plot.ylabel(text)`

Sets the y-axis label.

```pilang
plot.ylabel("value")
```

## `plot.tick(...)`

Configures axis ticks.

```pilang
plot.tick("x", [1, 2, 3])
```

## `plot.grid(value = true)`

Turns grid rendering on or off.

```pilang
plot.grid(true)
```

## `plot.axes(value = true)`

Turns axes rendering on or off.

```pilang
plot.axes(true)
```

## `plot.legend(...)`

Configures or shows a legend.

```pilang
plot.legend(["actual", "expected"])
```
