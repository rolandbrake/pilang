# PiLang CHIP-8

A baseline CHIP-8 emulator written in PiLang. It supports standard raw `.ch8`
ROMs loaded at address `0x200`, the 64 by 32 display, font sprites, timers,
input, and the original CHIP-8 opcode set.

Create the included demonstration ROM, then run it from the repository root:

```powershell
pilang run chip8.pi <file-name.ch8>
```

Run any compatible ROM by replacing the final path with its `.ch8` file.

The keypad maps to a convenient keyboard layout:

```
1 2 3 4        1 2 3 C
Q W E R   ->   4 5 6 D
A S D F        7 8 9 E
Z X C V        A 0 B F
```

Arrow keys are also supported: left/right map to CHIP-8 `7`/`9` , while up/down map to `5`/`8`.

Close the window or press Escape to quit.
