#ifndef PI_DRAW_H
#define PI_DRAW_H

#include "../src/pi_vm.h"
#include "../src/pi_value.h"
#include "../src/pi_object.h"

// Create a new drawing canvas/window.
Value dw_canvas(VM *vm, int argc, Value *argv);

// Start the application main loop.
Value dw_run(VM *vm, int argc, Value *argv);

#ifdef __EMSCRIPTEN__
void dw_stopWebLoop(void);
#endif

// Clear the canvas with the current/background color.
Value dw_clear(VM *vm, int argc, Value *argv);

// Draw a single pixel.
Value dw_pixel(VM *vm, int argc, Value *argv);

// Draw a line between two points.
Value dw_line(VM *vm, int argc, Value *argv);

// Draw a Triangle.
Value dw_triangle(VM *vm, int argc, Value *argv);

// Draw a rectangle.
Value dw_rect(VM *vm, int argc, Value *argv);

// Draw a polygon with multiple vertices.
Value dw_polygon(VM *vm, int argc, Value *argv);

// Draw a circle.
Value dw_circle(VM *vm, int argc, Value *argv);

// Draw text on the canvas.
Value dw_text(VM *vm, int argc, Value *argv);

// Draw an image/texture.
Value dw_image(VM *vm, int argc, Value *argv);

// Save the current transform/render state.
Value dw_push(VM *vm, int argc, Value *argv);

// Restore the previously saved transform/render state.
Value dw_pop(VM *vm, int argc, Value *argv);

// Apply a translation transform.
Value dw_translate(VM *vm, int argc, Value *argv);

// Apply a scaling transform.
Value dw_scale(VM *vm, int argc, Value *argv);

// Apply a rotation transform.
Value dw_rotate(VM *vm, int argc, Value *argv);

// Set the current alpha/transparency value.
Value dw_alpha(VM *vm, int argc, Value *argv);

// Register an event handler/callback.
Value dw_on(VM *vm, int argc, Value *argv);

// Unregister an event handler/callback.
Value dw_off(VM *vm, int argc, Value *argv);

// Poll pending input and window events.
Value dw_poll(VM *vm, int argc, Value *argv);

// Wait for the next event.
Value dw_wait(VM *vm, int argc, Value *argv);

// Get current mouse state or position.
Value dw_mouse(VM *vm, int argc, Value *argv);

// Show or hide the mouse cursor.
Value dw_cursor(VM *vm, int argc, Value *argv);

// Get current keyboard state.
Value dw_key(VM *vm, int argc, Value *argv);

// Check whether the window is still running/open.
Value dw_isRunning(VM *vm, int argc, Value *argv);

// Present the rendered frame to the screen.
Value dw_present(VM *vm, int argc, Value *argv);

// Register the per-frame animation callback.
Value dw_animate(VM *vm, int argc, Value *argv);

// Close the canvas/window.
Value dw_close(VM *vm, int argc, Value *argv);

// Set or get the window title.
Value dw_title(VM *vm, int argc, Value *argv);

// Resize the window.
Value dw_resize(VM *vm, int argc, Value *argv);

// Enable or disable fullscreen mode.
Value dw_fullscreen(VM *vm, int argc, Value *argv);

// Get the current window size.
Value dw_size(VM *vm, int argc, Value *argv);

// Get the current frames-per-second value.
Value dw_fps(VM *vm, int argc, Value *argv);

// Release native drawing resources owned by a canvas. Safe to call repeatedly.
void dw_cleanupContext(PiContext *ctx);

#endif // PI_DRAW_H
