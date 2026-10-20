# Build dependencies

Each platform has its own dependency kit. `make release` on Windows x64 uses
`windows-x64/`, which contains the SDL headers and static libraries required to
build Pilang without downloading or compiling those dependencies first.

These are build-time inputs only: the release ZIP contains a static
`pilang.exe`, not these files or SDL DLLs. Future platform kits belong beside
`windows-x64/`.

`linux-x64/` is generated and validated by the manual **Build Linux static
dependencies** GitHub Actions workflow before it is added here. Its release
binary must pass `ldd` with no shared-library dependencies.
