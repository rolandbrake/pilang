# Contributing to Pilang

Thanks for helping improve Pilang. Bug reports, documentation fixes, examples,
platform testing, and code contributions are all welcome.

## Before you start

1. Search existing issues and pull requests to avoid duplicate work.
2. Open an issue first for a large feature or behavior change.
3. Keep each pull request focused on one change.
4. Make sure to follow the naming convention for any change you want to make. [Style Guid](STYLE.md)

## Building and testing

Follow the platform instructions in the [README](README.md). On Windows x64 or
Linux x64, build the runtime with:

```sh
make release
```

Run the relevant tests or samples after your change. If your work affects a
specific platform, state the platform and commands you tested in the pull
request.

## Pull requests

- Use clear commit and pull-request titles.
- Update documentation when user-facing behavior changes.
- Do not include generated binaries, build output, or unrelated formatting.
- Describe what changed, why, and how you verified it.

## macOS help

macOS support needs community testing and a static dependency kit. If you can
help, open an issue with your macOS version, Intel or Apple Silicon architecture,
and complete build output.
