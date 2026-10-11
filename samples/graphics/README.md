# Graphics samples

These samples use PiLang's current SDL-backed `draw` module. They are intended
to explore the language and API; the 3D examples perform projection in PiLang
and are not an OpenGL renderer.

- `shapes_2d.pi` — transforms, alpha, circles, polygons, and text.
- `particles_2d.pi` — a procedural 2D particle field.
- `cube_3d.pi` — a rotating, filled software-projected cube.
- `terrain_3d.pi` — an animated wireframe height field.

Run one from the repository root, for example:

```powershell
.\bin\pilang.exe .\samples\graphics\cube_3d.pi
```
