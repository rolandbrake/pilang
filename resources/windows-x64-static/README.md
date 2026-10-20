# Windows static build kit

This folder contains the headers and static libraries used by `make release`
on Windows x64. They are bundled so contributors can build Pilang and create
standalone Windows packages without first compiling SDL and its dependencies.

To refresh this kit after changing its dependency versions, run:

```powershell
make static-deps
```

The release workflow uses these files to create a static `pilang.exe`. They
are build-time inputs only and are excluded from the downloadable runtime.
