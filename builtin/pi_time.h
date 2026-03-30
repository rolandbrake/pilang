#ifndef PI_TIME_H
#define PI_TIME_H

#include <unistd.h>
#include <time.h>
#include <math.h>

#include "../pi_value.h"
#include "../pi_vm.h"

Value pi_sleep(vm_t *vm, int argc, Value *argv);
Value _pi_time(vm_t *vm, int argc, Value *argv);

// Returns the current local time as a Time object.
// time.now() -> Time
Value tm_now(vm_t *vm, int argc, Value *argv);

// Returns the current local time as a Time object.
// time.utc() -> Time
Value tm_utc(vm_t *vm, int argc, Value *argv);

// Returns seconds since Unix epoch (1970-01-01 00:00:00 UTC).
// time.unix() -> int
Value tm_unix(vm_t *vm, int argc, Value *argv);

// Returns milliseconds since Unix epoch.
// time.millis() -> int
Value tm_millis(vm_t *vm, int argc, Value *argv);

// High-resolution monotonic clock in seconds. Best for measuring elapsed time.
// time.clock() -> float
Value tm_clock(vm_t *vm, int argc, Value *argv);

// Parses a time string with a format. Uses strftime-style tokens.
// time.parse(str: str, format: str) -> Time
Value tm_parse(vm_t *vm, int argc, Value *argv);

// Constructs a Time from components.
// time.of(year, month, day, h=0, m=0, s=0) -> Time
Value tm_of(vm_t *vm, int argc, Value *argv);

// Formats a Time using strftime-style format string.
// time.format(t: Time, fmt: str) -> str
Value tm_format(vm_t *vm, int argc, Value *argv);

// Returns ISO 8601 string (e.g. 2025-01-15T09:00:00Z).
// time.iso(t: Time) -> str
Value tm_iso(vm_t *vm, int argc, Value *argv);

// Calls cb once after ms milliseconds.
// time.timer(ms: int, cb: fn) -> Timer
Value tm_timer(vm_t *vm, int argc, Value *argv);

// Calls cb repeatedly every ms milliseconds.
// time.interval(ms: int, cb: fn) -> Timer
Value tm_interval(vm_t *vm, int argc, Value *argv);

#endif // PI_TIME_H