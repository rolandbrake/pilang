# tensor Module

The `tensor` module provides multidimensional numeric arrays and numerical
operations.

```swift
import tensor:t
```

## `tensor.zeros(shape...)`

Creates a tensor filled with zeros. Shape can be passed as numbers or a list.

```swift
println(t.zeros(2, 3))
println(t.zeros([2, 3]))
```

## `tensor.ones(shape...)`

Creates a tensor filled with ones.

```swift
println(t.ones(2, 2))
```

## `tensor.eye(rows, cols = rows)`

Creates an identity matrix.

```swift
println(t.eye(3))
```

## `tensor.rand(shape...)`

Creates a tensor with random values.

```swift
println(t.rand(2, 2))
```

## `tensor.randn(shape...)`

Creates a tensor with normally distributed random values.

```swift
println(t.randn(2, 2))
```

## `tensor.randint(shape..., min, max)`

Creates a tensor with random integer values.

```swift
println(t.randint([2, 2], 0, 10))
```

## `tensor.from(value)`

Creates a tensor from a compatible numeric list or matrix-like value.

```swift
let m = t.from([[1, 2], [3, 4]])
println(m)
```

## `tensor.fill(shape, value)`

Creates a tensor filled with one value.

```swift
println(t.fill([2, 3], 7))
```

## `tensor.shape(value)`

Returns the tensor shape as a list.

```swift
let m = t.ones(2, 3)
println(t.shape(m)) // [2, 3]
```

## `tensor.ndim(value)`

Returns the number of dimensions.

```swift
println(t.ndim(t.ones(2, 3))) // 2
```

## `tensor.size(value)`

Returns the total number of elements.

```swift
println(t.size(t.ones(2, 3))) // 6
```

## `tensor.reshape(value, shape)`

Returns a tensor with the same data and a new shape.

```swift
let v = t.from([1, 2, 3, 4])
println(t.reshape(v, [2, 2]))
```

## `tensor.slice(value, ...)`

Returns a slice of a tensor.

```swift
let m = t.from([[1, 2], [3, 4]])
println(t.slice(m, 0, 1))
```

## `tensor.concat(a, b, axis = nil)`

Concatenates tensors.

```swift
println(t.concat(t.ones(1, 2), t.zeros(1, 2)))
```

## `tensor.transpose(value)`

Transposes a tensor.

```swift
println(t.transpose(t.from([[1, 2], [3, 4]])))
```

## `tensor.flatten(value)`

Flattens a tensor to one dimension.

```swift
println(t.flatten(t.from([[1, 2], [3, 4]]))) // [1, 2, 3, 4]
```

## `tensor.expand_dims(value, axis)`

Adds a dimension.

```swift
println(t.expand_dims(t.from([1, 2, 3]), 0))
```

## `tensor.squeeze(value)`

Removes dimensions of size `1`.

```swift
println(t.squeeze(t.reshape(t.from([1, 2, 3]), [1, 3, 1])))
```

## `tensor.is_tensor(value)`

Returns whether a value is a tensor.

```swift
println(t.is_tensor(t.ones(2, 2))) // true
```

## `tensor.is_matrix(value)`

Returns whether a value is a matrix.

```swift
println(t.is_matrix(t.ones(2, 2))) // true
```

## `tensor.is_vector(value)`

Returns whether a value is a vector.

```swift
println(t.is_vector(t.from([1, 2, 3]))) // true
```

## `tensor.is_scalar(value)`

Returns whether a value is a scalar tensor.

```swift
println(t.is_scalar(t.from(10)))
```

## `tensor.add(a, b)`

Adds tensors or tensor-compatible values.

```swift
println(t.add(t.ones(2, 2), t.ones(2, 2)))
```

## `tensor.sub(a, b)`

Subtracts `b` from `a`.

```swift
println(t.sub(t.ones(2, 2), t.fill([2, 2], 0.5)))
```

## `tensor.mult(a, b)`

Multiplies elementwise.

```swift
println(t.mult(t.ones(2, 2), 5))
```

## `tensor.div(a, b)`

Divides elementwise.

```swift
println(t.div(t.fill([2, 2], 10), 2))
```

## `tensor.exp(value)`

Applies exponential elementwise.

```swift
println(t.exp(t.from([0, 1])))
```

## `tensor.log(value)`

Applies natural log elementwise.

```swift
println(t.log(t.from([1, 2, 3])))
```

## `tensor.sqrt(value)`

Applies square root elementwise.

```swift
println(t.sqrt(t.from([4, 9, 16])))
```

## `tensor.abs(value)`

Applies absolute value elementwise.

```swift
println(t.abs(t.from([-1, 2, -3])))
```

## `tensor.clip(value, min, max)`

Clamps tensor elements between `min` and `max`.

```swift
println(t.clip(t.from([-1, 2, 10]), 0, 5))
```

## `tensor.sign(value)`

Returns element signs.

```swift
println(t.sign(t.from([-5, 0, 8])))
```

## `tensor.apply(value, fn)`

Applies a function to each element.

```swift
println(t.apply(t.from([1, 2, 3]), x -> x * x))
```

## `tensor.matmult(a, b)`

Performs matrix multiplication.

```swift
let a = t.from([[1, 2], [3, 4]])
let b = t.eye(2)
println(t.matmult(a, b))
```

## `tensor.dot(a, b)`

Computes a dot product.

```swift
println(t.dot(t.from([1, 2]), t.from([3, 4]))) // 11
```

## `tensor.cross(a, b)`

Computes a cross product.

```swift
println(t.cross(t.from([1, 0, 0]), t.from([0, 1, 0])))
```

## `tensor.solve(a, b)`

Solves a linear system.

```swift
println(t.solve(t.from([[2, 0], [0, 2]]), t.from([4, 8])))
```

## `tensor.inv(value)`

Returns a matrix inverse.

```swift
println(t.inv(t.from([[1, 0], [0, 1]])))
```

## `tensor.det(value)`

Returns a matrix determinant.

```swift
println(t.det(t.from([[1, 2], [3, 4]])))
```

## `tensor.svd(value)`

Computes singular value decomposition.

```swift
println(t.svd(t.from([[1, 2], [3, 4]])))
```

## `tensor.eig(value)`

Computes eigen information for a matrix.

```swift
println(t.eig(t.from([[1, 0], [0, 2]])))
```

## `tensor.norm(value)`

Returns a vector or matrix norm.

```swift
println(t.norm(t.from([3, 4]))) // 5
```

## `tensor.rank(value)`

Returns matrix rank.

```swift
println(t.rank(t.eye(3)))
```

## `tensor.trace(value)`

Returns the sum of the diagonal.

```swift
println(t.trace(t.eye(3))) // 3
```

## `tensor.pinv(value)`

Returns the pseudoinverse.

```swift
println(t.pinv(t.eye(2)))
```

## `tensor.sum(value)`

Returns the sum of all elements.

```swift
println(t.sum(t.from([1, 2, 3]))) // 6
```

## `tensor.mean(value)`

Returns the mean of all elements.

```swift
println(t.mean(t.from([1, 2, 3]))) // 2
```

## `tensor.min(value)`

Returns the minimum element.

```swift
println(t.min(t.from([3, 1, 2]))) // 1
```

## `tensor.max(value)`

Returns the maximum element.

```swift
println(t.max(t.from([3, 1, 2]))) // 3
```

## `tensor.prod(value)`

Returns the product of all elements.

```swift
println(t.prod(t.from([2, 3, 4]))) // 24
```

## `tensor.argmax(value)`

Returns the index of the maximum element.

```swift
println(t.argmax(t.from([3, 9, 2]))) // 1
```

## `tensor.argmin(value)`

Returns the index of the minimum element.

```swift
println(t.argmin(t.from([3, 9, 2]))) // 2
```

## `tensor.any(value)`

Returns whether any element is truthy/non-zero.

```swift
println(t.any(t.from([0, 0, 1]))) // true
```

## `tensor.all(value)`

Returns whether all elements are truthy/non-zero.

```swift
println(t.all(t.from([1, 1, 0]))) // false
```

## `tensor.reduce(value, fn, initial = nil)`

Reduces tensor elements with a function.

```swift
println(t.reduce(t.from([1, 2, 3]), (a, b) -> a + b, 0))
```

## `tensor.var(value)`

Returns variance.

```swift
println(t.var(t.from([1, 2, 3])))
```

## `tensor.std(value)`

Returns standard deviation.

```swift
println(t.std(t.from([1, 2, 3])))
```

## `tensor.median(value)`

Returns median.

```swift
println(t.median(t.from([3, 1, 2]))) // 2
```

## `tensor.percentile(value, p)`

Returns the percentile value.

```swift
println(t.percentile(t.from([1, 2, 3, 4]), 50))
```

## `tensor.mode(value)`

Returns the most frequent value.

```swift
println(t.mode(t.from([1, 2, 2, 3]))) // 2
```

## `tensor.covariance(a, b)`

Returns covariance between two tensors or vectors.

```swift
println(t.covariance(t.from([1, 2, 3]), t.from([2, 4, 6])))
```

## `tensor.correlation(a, b)`

Returns correlation between two tensors or vectors.

```swift
println(t.correlation(t.from([1, 2, 3]), t.from([2, 4, 6])))
```

## `tensor.zscore(value)`

Returns z-scores for the values.

```swift
println(t.zscore(t.from([1, 2, 3])))
```

## `tensor.shuffle(value)`

Returns or applies a shuffled order of tensor values.

```swift
println(t.shuffle(t.from([1, 2, 3, 4])))
```
