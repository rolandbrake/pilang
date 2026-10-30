#ifndef GC_H
#define GC_H
#include "pi_object.h"
#include "pi_vm.h"

void *reallocate(void *ptr, size_t o_size, size_t n_size);

void mark_constants(VM *vm);

void mark_value(Value val);
void mark_object(Object *obj);
void mark_roots(VM *vm);
void mark_globals(VM *vm);
void mark_iters(VM *vm);

void sweep(VM *vm);

void free_object(Object *obj);
void free_value(Value *val);

void run_gc(VM *vm);
void gc_trimHeap(void);

size_t gc_objectBytes(Object *obj);
size_t gc_recountBytes(VM *vm);

#endif // GC_H
