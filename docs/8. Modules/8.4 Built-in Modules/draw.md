# draw Module

The `draw` module provides canvas drawing, events, _transforms, and drawing state.

```swift
import draw
```

## `draw.canvas(width, height, title = nil)`

Creates a drawing canvas or window.

```swift
draw.canvas(640, 480, "Demo")
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

## `draw.on_frame(callback)`

Registers a callback for frame updates.

```swift
draw.on_frame(() -> {
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

## `draw.text(text, x, y, ...)`

Draws text.

```swift
draw.text("Hello", 20, 30)
```

## `draw.image(path, x, y, ...)`

Draws an image.

```swift
draw.image("sprite.png", 100, 100)
```

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
