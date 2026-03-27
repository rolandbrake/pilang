#include <sys/time.h> // Include at top
#include "pi_time.h"
#include "pi_builtin.h"

Value pi_sleep(vm_t *vm, int argc, Value *argv)
{

    // Get the sleep duration in milliseconds from the argument
    double ms = AS_NUM(argv[0]);
    if (ms < 0)
        ms = 0; // avoid negative sleep times
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000);
    req.tv_nsec = (long)((ms - (req.tv_sec * 1000)) * 1e6);
    nanosleep(&req, NULL);

    return NEW_NIL();
}

// calculate the current time in milliseconds since the epoch
Value _pi_time(vm_t *vm, int argc, Value *argv)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    double millis = (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
    return NEW_NUM(millis);
}

// Module Registration

static BuiltinFunc time_funcs[] = {
    {"sleep", pi_sleep},
    {"time", _pi_time},
};

DEFINE_BUILTIN_MODULE(module_time, "time", time_funcs, NULL);