# image.filters Module

The `image.filters` module contains image filtering operations. Most functions
return a new image and leave the input image unchanged.

![Pilang edge detection example](../edge%20detection.png)

```swift
import image
import image.filters

let img = image.load("imgs/baboon.bmp")
let edges = image.filters.canny(img, 50, 150)
image.show(edges, "Edges")
```

## General Convolution

### `image.filters.filter(img, kernel)`

Applies a convolution kernel to an image. `kernel` can be either a numeric
builtin kernel id or a nested numeric list.

```swift
let sharp = image.filters.filter(img, image.filters.KERNEL_SHARPEN)

let custom = [
    [0, -1, 0],
    [-1, 5, -1],
    [0, -1, 0]
]
let also_sharp = image.filters.filter(img, custom)
```

The convolution preserves the source alpha channel and clamps RGB output to
`0..255`.

### `image.filters.kernel(name)`

Returns the numeric id for a named builtin kernel.

```swift
let k = image.filters.kernel("emboss")
let out = image.filters.filter(img, k)
```

Accepted names:

- `"identity"`
- `"sharpen"`
- `"edge"` or `"edge8"`
- `"emboss"`
- `"gaussian"` or `"gaussian3"`
- `"sobel"`, `"sobel_x"`, or `"sobelx"`
- `"sobel_y"` or `"sobely"`

### `image.filters.box_kernel(size = 3)`

Returns a nested numeric list for a normalized box blur kernel. `size` is
clamped to `1..51`.

```swift
let blur5 = image.filters.box_kernel(5)
let out = image.filters.filter(img, blur5)
```

`boxKernel` is an alias for `box_kernel`.

## Builtin Kernel Constants

Kernel constants are numeric ids in the range `0..KERNEL_COUNT - 1`.

| Constant | Meaning |
| --- | --- |
| `KERNEL_IDENTITY` | Leaves pixels unchanged |
| `KERNEL_SHARPEN` | Sharpening kernel |
| `KERNEL_EDGE` | 8-neighbor edge detector |
| `KERNEL_EMBOSS` | Emboss effect |
| `KERNEL_GAUSSIAN` | 3x3 Gaussian blur |
| `KERNEL_SOBEL` | Alias for `KERNEL_SOBEL_X` |
| `KERNEL_SOBEL_X` | Horizontal Sobel kernel |
| `KERNEL_SOBEL_Y` | Vertical Sobel kernel |
| `KERNEL_COUNT` | Number of builtin kernels |

## Convenience Filters

### `image.filters.invert(img)`

Inverts RGB channels and preserves alpha.

### `image.filters.brightness(img, delta)`

Adds `delta` to RGB channels and clamps to `0..255`.

```swift
let brighter = image.filters.brightness(img, 25)
```

### `image.filters.contrast(img, factor)`

Adjusts contrast around midpoint `128`.

```swift
let higher = image.filters.contrast(img, 1.25)
```

### `image.filters.blur(img, radius = 1)`

Applies a simple blur with kernel size `radius * 2 + 1`.

### `image.filters.sharpen(img)`

Applies the built-in sharpen kernel.

### `image.filters.sobel(img)`

Returns a grayscale edge-magnitude image using Sobel gradients.

### `image.filters.threshold(img, thresh = 128)`

Converts the image to black and white based on luminance.

```swift
let mask = image.filters.threshold(img, 100)
```

### `image.filters.canny(img, low = 50, high = 150)`

Runs a Canny-style edge detector: grayscale conversion, Gaussian blur,
gradient magnitude, non-maximum suppression, and hysteresis thresholding.

![Pilang Canny edge detection](edge%20detection.png)

```swift
let edges = image.filters.canny(img, 40, 120)
```

## Example

```swift
import image
import image.filters
import plot
import draw

let img = image.load("imgs/baboon.bmp")
let embossed = image.filters.filter(img, image.filters.KERNEL_EMBOSS)

let ctx = draw.canvas(900, 420, "Filters")
let a = plot.chart(ctx)
plot.subplot(a, 1, 2, 1)
plot.imshow(a, img)
plot.title(a, "Original")

let b = plot.chart(ctx)
plot.subplot(b, 1, 2, 2)
plot.imshow(b, embossed)
plot.title(b, "Emboss")

plot.show(a)
plot.show(b)
draw.run(ctx)
```
