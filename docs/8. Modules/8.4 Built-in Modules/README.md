# 8.4 Built-in Modules

Built-in modules are provided by the Pilang runtime. Import them with the same
syntax used for user modules.

```pilang
import math
import tensor.{zeros, shape}
import sys.{EXIT_SUCCESS}
```

## Modules

- [col](col.md): collection helpers
- [draw](draw.md): canvas drawing and events
- [fs](fs.md): files, directories, and paths
- [func](func.md): higher-order function helpers
- [io](io.md): input, output, prompts, and formatting
- [lang](lang.md): language and operator constants
- [math](math.md): math constants and numeric helpers
- [os](os.md): operating-system helpers
- [plot](plot.md): charts and plotting
- [stats](stats.md): statistics helpers
- [string](string.md): string utilities
- [sys](sys.md): process and runtime information
- [tensor](tensor.md): tensors and numerical arrays
- [time](time.md): clocks, dates, timers, and intervals
- [type](type.md): type checks and conversions

Built-in module exports are public unless documented otherwise.
