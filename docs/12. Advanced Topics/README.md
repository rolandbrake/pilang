# 12. Advanced Topics

This chapter explains how Pilang works below the surface. It is useful when you want to debug compiled code, write native built-ins, embed Pilang in another program, tune performance, or organize larger module trees.

Most programs do not need these details. You can write normal Pilang code using the syntax, functions, classes, and modules described in the earlier chapters. Advanced topics matter when you want to understand why the runtime behaves a certain way or when you are extending the language itself.

## Main runtime stages

A Pilang file moves through these stages:

1. Source text is scanned into tokens.
2. Tokens are parsed into language structures.
3. The compiler emits bytecode, constants, names, and debug metadata.
4. The VM executes bytecode using a stack, call frames, globals, modules, and managed heap objects.
5. The garbage collector reclaims unreachable heap objects.

## Sections

- [12.1 VM Architecture](12.1-vm-architecture.md)
- [12.2 Bytecode](12.2-bytecode.md)
- [12.3 Embedding Pilang](12.3-embedding-pilang.md)
- [12.4 Performance Tips](12.4-performance-tips.md)
- [12.5 Packaging System](12.5-packaging-system.md)
