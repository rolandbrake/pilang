#ifndef PI_DRAW_H
#define PI_DRAW_H

#include "../pi_vm.h"
#include "../pi_value.h"
#include "../pi_object.h"

// Create a new drawing canvas/window.
Value dw_canvas(vm_t *vm, int argc, Value *argv);

// Start the application main loop.
Value dw_run(vm_t *vm, int argc, Value *argv);

// Clear the canvas with the current/background color.
Value dw_clear(vm_t *vm, int argc, Value *argv);

// Draw a single pixel.
Value dw_pixel(vm_t *vm, int argc, Value *argv);

// Draw a line between two points.
Value dw_line(vm_t *vm, int argc, Value *argv);

// Draw a triangle.
Value dw_triangle(vm_t *vm, int argc, Value *argv);

// Draw a rectangle.
Value dw_rect(vm_t *vm, int argc, Value *argv);

// Draw a polygon with multiple vertices.
Value dw_polygon(vm_t *vm, int argc, Value *argv);

// Draw a circle.
Value dw_circle(vm_t *vm, int argc, Value *argv);

// Draw text on the canvas.
Value dw_text(vm_t *vm, int argc, Value *argv);

// Draw an image/texture.
Value dw_image(vm_t *vm, int argc, Value *argv);

// Save the current transform/render state.
Value dw_push(vm_t *vm, int argc, Value *argv);

// Restore the previously saved transform/render state.
Value dw_pop(vm_t *vm, int argc, Value *argv);

// Apply a translation transform.
Value dw_translate(vm_t *vm, int argc, Value *argv);

// Apply a scaling transform.
Value dw_scale(vm_t *vm, int argc, Value *argv);

// Apply a rotation transform.
Value dw_rotate(vm_t *vm, int argc, Value *argv);

// Set the current alpha/transparency value.
Value dw_alpha(vm_t *vm, int argc, Value *argv);

// Register an event handler/callback.
Value dw_on(vm_t *vm, int argc, Value *argv);

// Unregister an event handler/callback.
Value dw_off(vm_t *vm, int argc, Value *argv);

// Poll pending input and window events.
Value dw_poll(vm_t *vm, int argc, Value *argv);

// Wait for the next event.
Value dw_wait(vm_t *vm, int argc, Value *argv);

// Get current mouse state or position.
Value dw_mouse(vm_t *vm, int argc, Value *argv);

// Get current keyboard state.
Value dw_key(vm_t *vm, int argc, Value *argv);

// Check whether the window is still running/open.
Value dw_isRunning(vm_t *vm, int argc, Value *argv);

// Present the rendered frame to the screen.
Value dw_present(vm_t *vm, int argc, Value *argv);

// Set or invoke the per-frame callback.
Value dw_onFrame(vm_t *vm, int argc, Value *argv);

// Close the canvas/window.
Value dw_close(vm_t *vm, int argc, Value *argv);

// Set or get the window title.
Value dw_title(vm_t *vm, int argc, Value *argv);

// Resize the window.
Value dw_resize(vm_t *vm, int argc, Value *argv);

// Enable or disable fullscreen mode.
Value dw_fullscreen(vm_t *vm, int argc, Value *argv);

// Get the current window size.
Value dw_size(vm_t *vm, int argc, Value *argv);

// Get the current frames-per-second value.
Value dw_fps(vm_t *vm, int argc, Value *argv);

#endif // PI_DRAW_H