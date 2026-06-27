# Tensor Functions

Tensor helpers are exported by the `tensor` module. Import it before calling
module functions.

```swift
import tensor

let zeros = tensor.zeros([2, 3])
println(tensor.shape(zeros))
```

## Constructors

- `tensor.zeros(shape)`: tensor filled with zeros
- `tensor.ones(shape)`: tensor filled with ones
- `tensor.eye(size)`: identity matrix
- `tensor.rand(shape)`: random values
- `tensor.randn(shape)`: normally distributed random values
- `tensor.randint(shape, min, max)`: random integers
- `tensor.from(value)`: creates a tensor from a compatible value
- `tensor.fill(shape, value)`: fills a tensor with a value

## Shape Helpers

- `tensor.shape(value)`: shape of a tensor
- `tensor.ndim(value)`: number of dimensions
- `tensor.size(value)`: total element count
- `tensor.reshape(value, shape)`: reshape tensor data
- `tensor.slice(value, ...)`: slice tensor data
- `tensor.concat(a, b, axis = nil)`: concatenate tensors
- `tensor.transpose(value)`: transpose dimensions
- `tensor.flatten(value)`: flatten to one dimension
- `tensor.expand_dims(value, axis)`: add a dimension
- `tensor.squeeze(value)`: remove single-size dimensions

## Type Checks

- `tensor.is_tensor(value)`
- `tensor.is_matrix(value)`
- `tensor.is_vector(value)`
- `tensor.is_scalar(value)`

## Elementwise Math

- `tensor.add`, `tensor.sub`, `tensor.mult`, `tensor.div`
- `tensor.exp`, `tensor.log`, `tensor.sqrt`, `tensor.abs`
- `tensor.clip`, `tensor.sign`, `tensor.apply`

```swift
let a = tensor.ones([2, 2])
let b = tensor.mult(a, 5)

println(b)
```

## Linear Algebra

- `tensor.matmult(a, b)`
- `tensor.dot(a, b)`
- `tensor.cross(a, b)`
- `tensor.solve(a, b)`
- `tensor.inv(value)`
- `tensor.det(value)`
- `tensor.svd(value)`
- `tensor.eig(value)`
- `tensor.norm(value)`
- `tensor.rank(value)`
- `tensor.trace(value)`
- `tensor.pinv(value)`

## Reductions and Statistics

- `tensor.sum`, `tensor.mean`, `tensor.min`, `tensor.max`, `tensor.prod`
- `tensor.argmax`, `tensor.argmin`
- `tensor.any`, `tensor.all`
- `tensor.reduce`
- `tensor.var`, `tensor.std`, `tensor.median`, `tensor.percentile`, `tensor.mode`
- `tensor.covariance`, `tensor.correlation`, `tensor.zscore`
- `tensor.shuffle`

Tensors also work with arithmetic operators where supported. See
[Arithmetic Operators](../../4.%20Operators/4.1-arithmetic-operators.md).
