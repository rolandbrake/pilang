# draw Module

The `draw` module provides canvas drawing, events, _transforms, and drawing state.

```swift
import draw
```

## `draw.canvas(width, height, title = nil, icon = nil, show_launch = true)`

Creates a drawing canvas or window.

```swift
draw.canvas(640, 480, "Demo")
```

Pass an image path after the title to replace the default PiLang window icon.

```swift
draw.canvas(640, 480, "Demo", "assets/icon.png")
```

For exported Windows packages, Pilang shows its launch screen before creating
the first canvas. It uses the canvas dimensions, and is shown once per app by
default. Pass `false` as the final argument to disable it:

```swift
draw.canvas(640, 480, "Demo", nil, false)
```

The optional arguments may also be passed by name, so omitted options do not
need placeholder values:

```swift
draw.canvas(640, 480, title="Demo", show_launch=false)
```

## `draw.run(callback = nil)`

Runs the drawing loop. Pass a callback when you want frame-driven drawing.

```swift
draw.run(() -> {
    draw.clear()
    draw.circle(100, 100, 30)
    draw.present()
})
```

## `draw.clear(...)`

Clears the canvas.

```swift
draw.clear()
```

## `draw.pixel(x, y, ...)`

Draws one pixel.

```swift
draw.pixel(10, 10)
```

## `draw.line(x1, y1, x2, y2, ...)`

Draws a line.

```swift
draw.line(10, 10, 200, 120)
```

## `draw.triangle(...)`

Draws a triangle from three points.

```swift
draw.triangle(20, 20, 80, 20, 50, 70)
```

## `draw.rect(x, y, width, height, ...)`

Draws a rectangle.

```swift
draw.rect(20, 20, 120, 60)
```

## `draw.polygon(points, ...)`

Draws a polygon.

```swift
draw.polygon([[20, 20], [80, 20], [100, 70], [40, 90]])
```

## `draw.circle(x, y, radius, ...)`

Draws a circle.

```swift
draw.circle(320, 240, 80)
```

## `draw.present()`

Presents the current frame.

```swift
draw.present()
```

## `draw.animate(callback)`

Registers a callback for frame updates.

```swift
draw.animate(() -> {
    draw.clear()
    draw.present()
})
```

## `draw.on(event, callback)`

Registers an event callback.

```swift
draw.on("key", event -> println(event))
```

## `draw.off(event)`

Removes event callbacks for an event name.

```swift
draw.off("key")
```

## `draw.poll()`

Polls pending drawing/window events.

```swift
draw.poll()
```

## `draw.wait()`

Waits for an event.

```swift
draw.wait()
```

## `draw.key(canvas, key_code)`

Returns whether a key is currently held. Use a `draw.KEY_*` constant.

```swift
if draw.key(ctx, draw.KEY_A)
    println("A is held")
```

## `draw.mouse(canvas)`

Returns the current pointer state as a map with `x`, `y`, `left`, `middle`,
and `right` fields.

```swift
let mouse = draw.mouse(ctx)
if mouse.left
    println(mouse.x, mouse.y)
```

Mouse input is also available through `mousedown`, `mouseup`, `mousemove`,
and `scroll` event handlers. Mouse button events provide `x`, `y`, `button`,
and `pressed`; move events also provide `dx` and `dy`.

## `draw.cursor(canvas, visible)`

Shows or hides the system mouse cursor over the canvas.

```swift
draw.cursor(ctx, false) // hide cursor
```

## `draw.text(text, x, y, ...)`

Draws text.

```swift
draw.text("Hello", 20, 30)
```

The text argument accepts any PiLang value and formats it with the same rules
as `str`, so counters can be drawn directly:

```swift
draw.text(canvas, 20, 30, score)
```

## `draw.image(path, x, y, ...)`

Draws an image.

```swift
draw.image("sprite.png", 100, 100)
```

Each path is decoded and uploaded once per canvas, then its texture is reused
for later calls. This makes it suitable to draw the same sprites every frame;
cached textures are released when the canvas closes.

## `draw.is_running()`

Returns whether the drawing loop/window is still running.

```swift
while draw.is_running() {
    draw.poll()
}
```

## `draw.close()`

Closes the drawing window or canvas.

```swift
draw.close()
```

## `draw.push()`

Saves the current drawing state.

```swift
draw.push()
```

## `draw.pop()`

Restores the most recently saved drawing state.

```swift
draw.pop()
```

## `draw.translate(x, y)`

Moves the drawing origin.

```swift
draw.push()
draw.translate(100, 50)
draw.rect(0, 0, 40, 40)
draw.pop()
```

## `draw.scale(x, y = x)`

Scales subsequent drawing.

```swift
draw.push()
draw.scale(2, 2)
draw.circle(50, 50, 10)
draw.pop()
```

## `draw.rotate(angle)`

Rotates subsequent drawing.

```swift
draw.push()
draw.rotate(45)
draw.rect(0, 0, 80, 20)
draw.pop()
```

## `draw.alpha(value)`

Sets drawing alpha/transparency.

```swift
draw.alpha(0.5)
draw.circle(100, 100, 40)
```
