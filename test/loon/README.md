<p align="center">
  <img src= "./loon.png" width = 250/>
</p>

</br>
</br>

# LOON

LOON (Lightweight Object Outline Notation) is Pilang's compact,
indentation-based notation for JSON-compatible data. Parse a document with:

```pi
import loon
let value = loon.parse("name: \"Ada\"")
```

Objects become maps, arrays become lists, and `null` becomes `nil`. Parse
errors include a line and column.

## Values and keys

```loon
text: "hello nworld"
integer: -42
decimal: 2.5
enabled: true
missing: null
"display name": "Ada"
"key": "quoted key"
```

Strings support `\"`, `\\`, `\/`, `\n`, `\r`, and `\t`. Bare keys may contain
letters, digits, `_`, and `-`; quote every other key.

## Objects

Use two spaces per nesting level:

```loon
window:
  title: "Example"
  options:
    fullscreen: false
empty: {}
flat: {name: "Ada", active: true}
```

For nested values inside an array, use a braced block object:

```loon
{
  name: "deep"
  meta:
    rank: 2
}
```

## Arrays

```loon
inline: [1, "two", true, null]
empty: []
items:
  [
    "first"
    {name: "flat"}
    [4, 5]
    {
      name: "deep"
      meta:
        rank: 2
    }
  ]
```

## Tables

Tables are arrays of maps with a fixed header. Every row must have the same
number of values as the header.

```loon
rows("name", "age"):
  "Ada", 10
  "Grace", 22
```

Root tables are also valid:

```loon
(name, age):
  "Ada", 10
  "Grace", 22
```

Duplicate headers, uneven rows, tabs used for indentation, and unexpected
trailing content are rejected.

## Fixtures

`basic.loon`/`basic.pi` cover the full supported notation. `table.*` and
`block_object.*` are focused regression fixtures. Run `make loon-test`.
