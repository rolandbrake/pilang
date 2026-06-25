# 8. Modules

Modules split Pilang programs into reusable files and libraries. A module can be
a user-written `.pi` file or a built-in module provided by the runtime.

Importing a module loads it once, caches it, and exposes its public symbols
through a module object or through selected local names.

## Key Ideas

- `import math` loads a module and binds it to a name.
- `import math, random` imports multiple modules with one `import` keyword.
- `import math.{floor, ceil}` imports selected exports.
- `import math.{floor:f}` imports an export with an alias.
- `import math:mt.{cos, sin}` imports `math` as `mt` and selected exports.
- `import math.*` imports all public exports into the current module/global scope.
- Top-level names in a user module are exported automatically.
- Names beginning with `_` are private and cannot be imported from outside.

Imported module objects expose metadata fields such as `name`, `path`,
`is_main`, and `exports`.

## Sections

- [8.1 Importing Modules](8.1-importing-modules.md)
- [8.2 Exporting Symbols](8.2-exporting-symbols.md)
- [8.3 Module Paths](8.3-module-paths.md)
- [8.4 Built-in Modules](8.4%20Built-in%20Modules/README.md)
- [8.5 User Modules](8.5-user-modules.md)
