# PiLang C Style Guide

This document defines naming conventions for PiLang's C source code.

## Functions

- Public functions with two name parts use `snake_case`.
  - `parse_token`
  - `list_append`
- Functions with three or more conceptual parts use the form
  `<theme>_<camelCaseRest>`.
  - `vm_callMethodNoArgs`
  - `tensor_broadcastBinary`
- File-private functions declared `static` use the same function convention,
  prefixed with one underscore.
  - `_vm_callMethodNoArgs`
  - `_parse_token`

Do not use names beginning with two underscores or an underscore followed by an
uppercase letter; those names are reserved by the C implementation.

## Types

- Struct, enum, union, and typedef names use `PascalCase`.
  - `VM`
  - `PiList`
  - `TensorSliceSpec`
  - `PiTokenType`
- Types in an established project namespace use
  `ACRONYM_PascalCase`. The acronym must be documented and used consistently
  by that subsystem.
  - `PX_ProjectBuild`
  - `PX_ModuleRecord`
  - `HT_Table`
  - `HT_Iterator`

## Constants

- Enum members, macros, and compile-time constants use `UPPER_SNAKE_CASE`.
  - `TOKEN_IDENTIFIER`
  - `OP_ADD`
  - `MAX_STACK_SIZE`

## Variables

- Local variables, parameters, struct fields, and non-constant globals use
  `snake_case`.
  - `target_offset`
  - `arg_count`
  - `current_module`

## Acronyms

Keep acronyms uppercase when they appear within an identifier.

- `VM`, not `Vm`
- `UTF8`, not `Utf8`
- `vm_collectGC`, not `vm_collectGc`

## Files and Headers

- C source and header filenames use `snake_case`.
  - `pi_vm.c`
  - `pi_list.h`
- Include guards use `UPPER_SNAKE_CASE`.
  - `PI_VM_H`

## Pointer Declarations

Place the asterisk beside the type.

```c
VM *vm;
PiList *list;
```

## Scope

This guide applies to the C implementation and its public C API. PiLang
language-level names are unchanged unless a separate compatibility decision is
made.
