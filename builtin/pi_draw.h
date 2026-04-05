#ifndef PI_DRAW_H
#define PI_DRAW_H

#include "../pi_vm.h"
#include "../pi_value.h"
#include "../pi_object.h"

// create new canvas object
Value dw_canvas(vm_t *vm, int argc, Value *argv);

// start the main loop
Value dw_run(vm_t *vm, int argc, Value *argv);

Value dw_clear(vm_t *vm, int argc, Value *argv);
Value dw_pixel(vm_t *vm, int argc, Value *argv);
Value dw_line(vm_t *vm, int argc, Value *argv);
Value dw_triangle(vm_t *vm, int argc, Value *argv);
Value dw_rect(vm_t *vm, int argc, Value *argv);
Value dw_polygon(vm_t *vm, int argc, Value *argv);
Value dw_circle(vm_t *vm, int argc, Value *argv);

Value dw_text(vm_t *vm, int argc, Value *argv);
Value dw_image(vm_t *vm, int argc, Value *argv);

Value dw_push(vm_t *vm, int argc, Value *argv);
Value dw_pop(vm_t *vm, int argc, Value *argv);
Value dw_translate(vm_t *vm, int argc, Value *argv);
Value dw_scale(vm_t *vm, int argc, Value *argv);
Value dw_rotate(vm_t *vm, int argc, Value *argv);
Value dw_alpha(vm_t *vm, int argc, Value *argv);

Value dw_on(vm_t *vm, int argc, Value *argv);
Value dw_off(vm_t *vm, int argc, Value *argv);
Value dw_poll(vm_t *vm, int argc, Value *argv);
Value dw_wait(vm_t *vm, int argc, Value *argv);
Value dw_mouse(vm_t *vm, int argc, Value *argv);
Value dw_key(vm_t *vm, int argc, Value *argv);

Value dw_isRunning(vm_t *vm, int argc, Value *argv);
Value dw_present(vm_t *vm, int argc, Value *argv);
Value dw_onFrame(vm_t *vm, int argc, Value *argv);
Value dw_close(vm_t *vm, int argc, Value *argv);

Value dw_title(vm_t *vm, int argc, Value *argv);
Value dw_resize(vm_t *vm, int argc, Value *argv);
Value dw_fullscreen(vm_t *vm, int argc, Value *argv);
Value dw_size(vm_t *vm, int argc, Value *argv);
Value dw_fps(vm_t *vm, int argc, Value *argv);
#endif // PI_DRAW_H