# lang Module

The `lang` module exposes language-level constants. The main use is comparing
operator codes passed to object magic methods such as `compute`.

```pilang
import lang
```

## Arithmetic Operator Codes

- `lang.OP_ADD`: `+`
- `lang.OP_SUB`: `-`
- `lang.OP_MUL`: `*`
- `lang.OP_DIV`: `/`
- `lang.OP_MOD`: `%`
- `lang.OP_POW`: `**`
- `lang.OP_DOT`: dot-product style operator where supported

```pilang
compute(op, other) {
    if op == lang.OP_ADD {
        return this.value + other
    }
}
```

## Logical Operator Codes

- `lang.OP_LAND`: logical and
- `lang.OP_LOR`: logical or
- `lang.OP_IS`: identity/type-style `is`

```pilang
println(lang.OP_LAND)
```

## Bitwise and Shift Operator Codes

- `lang.OP_BAND`: bitwise and
- `lang.OP_BOR`: bitwise or
- `lang.OP_BXOR`: bitwise xor
- `lang.OP_SHL`: shift left
- `lang.OP_SHR`: shift right
- `lang.OP_USHR`: unsigned shift right

```pilang
println(lang.OP_BXOR)
```

## Unary Operator Codes

- `lang.OP_POS`: unary `+`
- `lang.OP_NEG`: unary `-`
- `lang.OP_BNOT`: unary `~`

```pilang
compute(op) {
    if op == lang.OP_NEG {
        return -this.value
    }
}
```

## Comparison Operator Codes

- `lang.OP_EQ`: `==`
- `lang.OP_NE`: `!=`
- `lang.OP_GT`: `>`
- `lang.OP_LT`: `<`
- `lang.OP_GE`: `>=`
- `lang.OP_LE`: `<=`

```pilang
println(lang.OP_EQ)
```

See [Magic Methods](../../7.%20Objects%20and%20Classes/7.8-magic-methods.md)
and [Operator Overloading](../../4.%20Operators/4.6-operator-overloading.md).
