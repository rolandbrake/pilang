# draw3d Module

`draw3d` is PiLang's native OpenGL 3.3 renderer for indexed triangle meshes.
It owns an independent window; it does not render into a `draw` canvas and is
currently unavailable in the web build.

```swift
import draw3d
```

## Quick start

Create a canvas, upload a mesh once, then clear and draw it in an animation
callback. This triangle is the smallest complete `draw3d` program:

```swift
import draw3d

ctx = draw3d.canvas(640, 480, "Triangle")
triangle = draw3d.mesh(ctx, {
    position: [[-1, -1, 0], [1, -1, 0], [0, 1, 0]],
    color: [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
    indices: [0, 1, 2]
})

fun frame(canvas) {
    draw3d.clear(canvas, 0x101820)
    draw3d.draw(canvas, triangle)
}

draw3d.animate(ctx, frame)
draw3d.run(ctx)
```

## `draw3d.canvas(width, height, title = "PiLang 3D")`

Creates an independent, resizable OpenGL window. Width and height must be
positive numbers. The canvas provides a perspective camera, depth testing, and
back-face culling by default. Press Escape or close the window to end `run`.

## `draw3d.mesh(canvas, data)`

Uploads an indexed triangle mesh to the GPU and returns a mesh handle.

`data` must contain three lists:

| Field | Required contents |
| --- | --- |
| `position` | One `[x, y, z]` number list per vertex. |
| `color` | One `[red, green, blue]` number list per vertex. Values normally range from `0` to `1`. |
| `indices` | Integer vertex indices grouped in threes; every three values form one triangle. |

`position` and `color` must contain the same number of vertices. Meshes support
up to 65,535 vertices. Triangles are front-facing when their indices wind
counter-clockwise as viewed from outside the mesh.

A mesh belongs to the canvas passed to `mesh`; it cannot be drawn on a
different `draw3d` canvas. Its GPU resources are released when that canvas is
closed.

### Indexed cube

There are no shape-specific primitives. A cube is ordinary mesh data:

```swift
cube = draw3d.mesh(ctx, {
    position: [[-1, -1, -1], [1, -1, -1], [-1, 1, -1], [1, 1, -1],
               [-1, -1,  1], [1, -1,  1], [-1, 1,  1], [1, 1,  1]],
    color: [[1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1, 0],
            [1, 0, 1], [0, 1, 1], [1, 1, 1], [.4, .4, .4]],
    indices: [4, 5, 7, 4, 7, 6, 0, 2, 3, 0, 3, 1,
              1, 3, 7, 1, 7, 5, 0, 4, 6, 0, 6, 2,
              2, 6, 7, 2, 7, 3, 0, 1, 5, 0, 5, 4]
})
```

## `draw3d.draw(canvas, mesh, transform)`

Draws a mesh. Omit the optional transform, or pass a map with these optional
fields:

```swift
draw3d.draw(ctx, cube, {
    position: [0, 0, 0],
    rotation: [0, angle, 0]
})
```

`position` and `rotation` are each three-number lists. Rotation is in radians,
ordered as X, Y, then Z. Missing fields default to `[0, 0, 0]`.

## Frame loop

### `draw3d.clear(canvas, color = 0x000000)`

Clears the color and depth buffers. Colors are RGB hexadecimal numbers.

### `draw3d.animate(canvas, callback)`

Registers a callback that receives the canvas once per frame. Call `clear` and
one or more `draw` operations in the callback.

### `draw3d.run(canvas)`

Runs the event and rendering loop until the window closes or `draw3d.close` is
called.

### `draw3d.close(canvas)`

Stops the active frame loop and releases the canvas resources after the current
frame.
