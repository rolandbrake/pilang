# image Package

The `image` package is Pilang's core image-processing API. It loads images into
native image objects, transforms image dimensions, displays and saves images,
and converts between images and tensors for numerical and machine-learning
workflows.

![Pilang image processing example](../grayscale.png)

```pilang
import image

let img = image.load("imgs/baboon.bmp")
println(image.width(img), image.height(img), image.channels(img))

let small = image.resize(img, 256, 256)
image.save(small, "out.bmp")
```

## Package Modules

- [`image`](image.md): image loading, metadata, geometry, display, and tensor conversion
- [`image.filters`](image.filters.md): convolution, edge detection, thresholding, and filter kernels
- [`image.color`](image.color.md): grayscale, RGB/RGBA, and HSV conversions

Import child modules explicitly:

```pilang
import image
import image.filters
import image.color
```

## Loading And Saving

### `image.load(path)`

Loads an image file and returns an image object. The loader uses SDL_image, so
common formats such as BMP, PNG, and JPG are supported when the native build has
those SDL_image backends.

```pilang
let img = image.load("imgs/lenna.png")
```

### `image.save(img, path)`

Saves an image to disk. The current implementation saves through SDL's BMP path,
so use a `.bmp` extension for predictable results.

```pilang
image.save(img, "result.bmp")
```

## Metadata

### `image.width(img)`

Returns image width in pixels.

### `image.height(img)`

Returns image height in pixels.

### `image.channels(img)`

Returns the number of bytes per pixel in the current image surface.

```pilang
println(image.width(img))
println(image.height(img))
println(image.channels(img))
```

## Geometry

### `image.resize(img, width, height)`

Returns a resized copy of the image.

```pilang
let resized = image.resize(img, 320, 240)
```

### `image.crop(img, x, y, width, height)`

Returns a cropped copy. The rectangle must be inside the image bounds.

```pilang
let face = image.crop(img, 80, 60, 160, 160)
```

### `image.flip(img, axis)`

Returns a flipped copy. `axis` is `"x"` for horizontal flip or `"y"` for
vertical flip.

```pilang
let mirror = image.flip(img, "x")
let upside_down = image.flip(img, "y")
```

## Display

### `image.show(img, title = "display image")`

Opens a simple SDL window and displays the image until the user closes the
window or presses a key.

```pilang
image.show(img, "Preview")
```

For chart-style display with axes, ticks, subplots, and heatmaps, use
`plot.imshow(chart, img)`.

## Tensor Conversion

### `image.img2tensor(img, normalize = false)`

Converts an image to a tensor with shape `[height, width, channels]`. If
`normalize` is true, channel values are scaled to `0..1`; otherwise they remain
in `0..255`.

```pilang
import tensor

let t = image.img2tensor(img, true)
println(tensor.shape(t))
```

### `image.tensor2img(tensor, normalize = false)`

Converts a tensor with shape `[height, width, channels]` back to an image.
`channels` must be from `1` to `4`. If `normalize` is true, values are treated
as `0..1` and scaled to `0..255`. If `normalize` is false and all values are
already in `0..1`, the converter auto-scales them to image range.

```pilang
let t = image.img2tensor(img, true)
let copy = image.tensor2img(t, true)
```

## Full Workflow

```pilang
import image
import image.color
import image.filters

let img = image.load("imgs/baboon.bmp")
let gray = image.color.rgb2gray(img)
let edges = image.filters.canny(gray, 40, 120)

image.save(edges, "edges.bmp")
```
