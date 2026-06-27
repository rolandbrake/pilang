# Pilang Micrograd

This folder contains a tiny reverse-mode autodiff engine and a small neural-network library written in Pilang. It is inspired by micrograd, but the point here is also to show off Pilang itself: classes, operator hooks, closures, tuples, list comprehensions, callable objects, and dynamic computation graphs.

The implementation is intentionally scalar-first. Every neuron is built from many small `Value` objects, each one recording how it was produced and how gradients should flow backward through it. That makes the code excellent for learning, debugging, and experimenting with autodiff from the inside.

## Files

- `engine.pi`: defines `Value`, scalar arithmetic, ReLU, graph traversal, and `backward()`.
- `nn.pi`: defines `Module`, `Neuron`, `Layer`, and `MLP`.
- `main.pi`: trains a small multilayer perceptron on a toy classification dataset.

## Quick Run

From the repository root:

```powershell
.\release\pilang.exe .\ML\micrograd\main.pi
```

Or, if `pilang.exe` is on your path:

```powershell
pilang ML/micrograd/main.pi
```

## Scalar Autograd

`Value` overloads Pilang arithmetic through the language's object/operator protocol. Normal-looking math creates a dynamic graph:

```swift
import engine.{Value}

a = Value(-4.0)
b = Value(2.0)

c = a + b
d = a * b + b**3
c += c + 1
c += 1 + c + (-a)
d += d * 2 + (b + a).relu()
d += 3 * d + (b - a).relu()

e = c - d
f = e**2
g = f / 2.0
g += 10.0 / f

println("g = " + g.data)

g.backward()

println("dg/da = " + a.grad)
println("dg/db = " + b.grad)
```

Each operation creates a new `Value` and stores a small closure in `_backward`. Calling `backward()` builds a topological ordering of the graph, seeds the output gradient with `1`, and walks backward applying those closures.

## Neural Networks

`nn.pi` builds a tiny PyTorch-like API on top of `Value`.

```swift
import nn.{MLP}
import engine.{Value}

xs = [
    [2.0, 3.0, -1.0],
    [3.0, -1.0, 0.5],
    [0.5, 1.0, 1.0],
    [1.0, 1.0, -1.0],
]

ys = [1.0, -1.0, -1.0, 1.0]

model = MLP(3, [4, 4, 1])

for step in range(150) {
    ypred = [model(x) : x in xs]
    loss = Value(0)

    for i in range(len(ypred)) {
        diff = ypred[i] - ys[i]
        loss += diff * diff
    }

    model.zero_grad()
    loss.backward()

    for p in model.parameters()
        p.data += -0.01 * p.grad

    println("step " + str(step) + " loss " + str(loss.data))
}
```

The model API is small but useful:

- `Module.zero_grad()`: resets every parameter gradient.
- `Module.parameters()`: returns trainable `Value` objects.
- `Neuron(nin, nonlin = true)`: one weighted neuron with optional ReLU.
- `Layer(nin, nout, nonlin = true)`: a list of neurons.
- `MLP(nin, nouts)`: a stack of layers, for example `MLP(3, [4, 4, 1])`.

## Why This Is Interesting in Pilang

- `Value.compute(op, other)` lets user objects participate in operators like `+`, `-`, `*`, `/`, and `**`.
- Backward functions are closures that capture the parent nodes and output node.
- Tuples store graph parents: `Value(data, (left, right), '+')`.
- List comprehensions keep model construction compact: `[Neuron(nin) : _ in range(nout)]`.
- Callable objects make `model(x)` dispatch to `call(x)`.

## Practical Notes

This is an educational scalar engine, not a fast tensor engine. Small networks are the sweet spot. Very wide layers create large object graphs and will run slowly compared with tensor-based libraries.

For numerical experiments that need larger arrays, use the built-in `tensor` module. For learning how autodiff works, this folder is the more transparent path.

## License

MIT
