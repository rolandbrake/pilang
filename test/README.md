Pilang tests are organized by language area:

- `core/`: basic language behavior and regressions
- `matrix/`: dense matrix math, broadcasting, indexing, slicing
- `module/`: imports, aliasing, and module privacy
- `object/`: prototype/object behavior
- `type/`: runtime type coverage
- `examples/`: larger algorithm/program smoke tests from the test root

Conventions:

- Files inside folders starting with `_` are fixtures and are not executed directly.
- Test files with `_fail` in the name are expected to fail.
- All other `.pi` files are expected to pass.

Run the suite with:

```bash
python tools/tests.py
```
