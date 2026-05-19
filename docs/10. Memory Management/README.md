# 10. Memory Management

Pilang manages heap objects automatically with garbage collection. You do not
manually free strings, lists, maps, functions, tensors, modules, or class
instances.

The practical rule is simple: an object stays alive while it can still be reached
from running code, globals, modules, closures, or other live objects. Once it is
unreachable, the garbage collector may reclaim it.

## Sections

- [10.1 Garbage Collection](10.1-garbage-collection.md)
- [10.2 Object Lifetime](10.2-object-lifetime.md)
- [10.3 Closures and References](10.3-closures-and-references.md)
